// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
//
// kernel32.cpp — pthread-backed implementation of the Win32 kernel32
// threading / critical-section / user-mode-APC / alertable-wait
// surface that ml_disc's drivemngr.cpp depends on.
//
// Why real (not stubbed): drivemngr runs a medium-poll worker thread
// and dispatches each drive-info job as a user-mode APC queued onto a
// worker, picked up when that worker enters an alertable SleepEx.
// Stubbing the APC queue to no-ops would silently drop every job
// (no enumeration) or deadlock the poll loop.  So we implement a
// faithful per-thread APC queue + alertable wait over pthreads.
//
// APC model:
//   - Each CreateThread-spawned thread owns an ApcQueue (mutex+cond+
//     deque).  A thread_local pointer `g_selfQueue` points at it.
//   - GetCurrentThread() returns the Win32 pseudo-handle (HANDLE)-2.
//   - QueueUserAPC(pfn, h, p): resolve h -> queue (pseudo -> g_selfQueue,
//     real handle -> that thread's queue), push (pfn,p), signal cond.
//   - SleepEx(ms, alertable=TRUE): if APCs pending, drain them and
//     return WAIT_IO_COMPLETION; else timed cond_wait; on wake, drain
//     if any arrived (WAIT_IO_COMPLETION) else return 0 on timeout.
//

#include "win32/winbase.h"
#include "win32/winuser.h"   // CloseHandle decl + Sleep

#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <errno.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace {

struct ApcQueue {
    pthread_mutex_t mtx;
    pthread_cond_t  cond;
    std::deque<std::pair<PAPCFUNC, ULONG_PTR>> items;
    ApcQueue() {
        pthread_mutex_init(&mtx, nullptr);
        pthread_cond_init(&cond, nullptr);
    }
    ~ApcQueue() {
        pthread_mutex_destroy(&mtx);
        pthread_cond_destroy(&cond);
    }
};

struct ThreadObject {
    pthread_t              tid{};
    DWORD                  winThreadId = 0;
    ApcQueue               apc;
    LPTHREAD_START_ROUTINE routine = nullptr;
    LPVOID                 param = nullptr;
    DWORD                  exitCode = 0;
    bool                   joined = false;
};

// Per-thread pointer to that thread's own APC queue (set by the
// trampoline).  Threads NOT created via CreateThread (e.g. the Qt
// main thread) have a null self-queue and fall back to plain sleep.
thread_local ApcQueue *g_selfQueue = nullptr;

// Map pthread_t -> ThreadObject* so QueueUserAPC against a real
// thread handle can find the target queue.  Guarded by g_threadsMu.
std::mutex g_threadsMu;
std::unordered_map<HANDLE, ThreadObject *> g_threadObjects;

std::atomic<DWORD> g_nextWinThreadId{1000};

void *thread_trampoline(void *arg) {
    auto *obj = static_cast<ThreadObject *>(arg);
    g_selfQueue = &obj->apc;
    DWORD rc = obj->routine ? obj->routine(obj->param) : 0;
    obj->exitCode = rc;
    return nullptr;
}

ApcQueue *resolveQueue(HANDLE h) {
    // Win32 pseudo-handle for "current thread" is (HANDLE)-2.
    if (h == reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2)) || h == nullptr)
        return g_selfQueue;
    std::lock_guard<std::mutex> lk(g_threadsMu);
    auto it = g_threadObjects.find(h);
    return it != g_threadObjects.end() ? &it->second->apc : nullptr;
}

// Drain all currently-queued APCs on `q` (called with q->mtx held;
// releases + reacquires around each user callback).  Returns true if
// at least one APC ran.
bool drainApcs(ApcQueue *q) {
    bool ran = false;
    while (!q->items.empty()) {
        auto job = q->items.front();
        q->items.pop_front();
        pthread_mutex_unlock(&q->mtx);
        if (job.first) job.first(job.second);
        ran = true;
        pthread_mutex_lock(&q->mtx);
    }
    return ran;
}

void timespec_in(struct timespec *ts, DWORD ms) {
    clock_gettime(CLOCK_REALTIME, ts);
    ts->tv_sec  += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) { ts->tv_sec++; ts->tv_nsec -= 1000000000L; }
}

}  // anonymous

extern "C" {

// ── Threads ─────────────────────────────────────────────────────
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T,
                            LPTHREAD_START_ROUTINE routine, LPVOID param,
                            DWORD /*flags*/, LPDWORD lpThreadId) {
    auto *obj = new ThreadObject();
    obj->routine = routine;
    obj->param   = param;
    obj->winThreadId = g_nextWinThreadId.fetch_add(1);
    if (lpThreadId) *lpThreadId = obj->winThreadId;
    HANDLE h = reinterpret_cast<HANDLE>(obj);  // handle == object ptr
    {
        std::lock_guard<std::mutex> lk(g_threadsMu);
        g_threadObjects[h] = obj;
    }
    if (pthread_create(&obj->tid, nullptr, thread_trampoline, obj) != 0) {
        std::lock_guard<std::mutex> lk(g_threadsMu);
        g_threadObjects.erase(h);
        delete obj;
        return nullptr;
    }
    return h;
}

HANDLE WINAPI GetCurrentThread(void) {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-2));
}
HANDLE WINAPI GetCurrentProcess(void) {
    return reinterpret_cast<HANDLE>(static_cast<intptr_t>(-1));
}
DWORD WINAPI GetCurrentThreadId(void) {
    return static_cast<DWORD>(
        reinterpret_cast<uintptr_t>((void *)pthread_self()) & 0xFFFFFFFFu);
}
DWORD WINAPI GetCurrentProcessId(void) {
    return static_cast<DWORD>(::getpid());
}
BOOL WINAPI TerminateThread(HANDLE h, DWORD) {
    ThreadObject *obj = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_threadsMu);
        auto it = g_threadObjects.find(h);
        if (it != g_threadObjects.end()) obj = it->second;
    }
    if (obj) pthread_cancel(obj->tid);
    return TRUE;
}
BOOL WINAPI SetThreadPriority(HANDLE, int) { return TRUE; }
int  WINAPI GetThreadPriority(HANDLE)      { return 0; }
DWORD WINAPI ResumeThread(HANDLE)          { return 0; }
DWORD WINAPI SuspendThread(HANDLE)         { return 0; }
BOOL WINAPI GetExitCodeThread(HANDLE h, LPDWORD code) {
    std::lock_guard<std::mutex> lk(g_threadsMu);
    auto it = g_threadObjects.find(h);
    if (it != g_threadObjects.end()) { if (code) *code = it->second->exitCode; return TRUE; }
    if (code) *code = 0;
    return TRUE;
}
BOOL WINAPI DuplicateHandle(HANDLE, HANDLE src, HANDLE, LPHANDLE out,
                             DWORD, BOOL, DWORD) {
    if (out) *out = src;  // same-process: alias the handle
    return TRUE;
}
DWORD WINAPI GetWindowThreadProcessId(HWND, LPDWORD pid) {
    if (pid) *pid = GetCurrentProcessId();
    return GetCurrentThreadId();
}

// ── User-mode APCs ──────────────────────────────────────────────
DWORD WINAPI QueueUserAPC(PAPCFUNC pfn, HANDLE hThread, ULONG_PTR data) {
    ApcQueue *q = resolveQueue(hThread);
    if (!q) return 0;
    pthread_mutex_lock(&q->mtx);
    q->items.emplace_back(pfn, data);
    pthread_cond_signal(&q->cond);
    pthread_mutex_unlock(&q->mtx);
    return 1;
}

DWORD WINAPI SleepEx(DWORD ms, BOOL bAlertable) {
    ApcQueue *q = g_selfQueue;
    if (!bAlertable || !q) {
        if (ms == INFINITE) { for (;;) ::usleep(1000 * 1000); }
        ::usleep((useconds_t)ms * 1000);
        return 0;
    }
    pthread_mutex_lock(&q->mtx);
    if (drainApcs(q)) { pthread_mutex_unlock(&q->mtx); return WAIT_IO_COMPLETION; }
    DWORD rc = 0;
    if (ms == 0) {
        rc = 0;  // nothing pending, zero timeout
    } else if (ms == INFINITE) {
        pthread_cond_wait(&q->cond, &q->mtx);
        rc = drainApcs(q) ? WAIT_IO_COMPLETION : 0;
    } else {
        struct timespec ts; timespec_in(&ts, ms);
        int w = pthread_cond_timedwait(&q->cond, &q->mtx, &ts);
        if (w == 0 && drainApcs(q)) rc = WAIT_IO_COMPLETION;
        else rc = 0;  // timed out (or spurious with no APC)
    }
    pthread_mutex_unlock(&q->mtx);
    return rc;
}

// ── Waits ───────────────────────────────────────────────────────
// For a thread handle: join (optionally alertable-draining while
// waiting).  For other handles: succeed immediately.
DWORD WINAPI WaitForSingleObject(HANDLE h, DWORD ms) {
    return WaitForSingleObjectEx(h, ms, FALSE);
}
DWORD WINAPI WaitForSingleObjectEx(HANDLE h, DWORD ms, BOOL bAlertable) {
    ThreadObject *obj = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_threadsMu);
        auto it = g_threadObjects.find(h);
        if (it != g_threadObjects.end()) obj = it->second;
    }
    if (obj) {
        if (ms == INFINITE) {
            if (!obj->joined) { pthread_join(obj->tid, nullptr); obj->joined = true; }
            return WAIT_OBJECT_0;
        }
        // Bounded: poll-join in small slices, draining APCs if alertable.
        DWORD waited = 0;
        while (waited < ms) {
            if (bAlertable && g_selfQueue) { SleepEx(10, TRUE); }
            else ::usleep(10 * 1000);
            waited += 10;
        }
        return WAIT_TIMEOUT;
    }
    if (bAlertable) return SleepEx(ms == INFINITE ? 0 : ms, TRUE);
    return WAIT_OBJECT_0;
}
DWORD WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *h, BOOL all, DWORD ms) {
    return WaitForMultipleObjectsEx(n, h, all, ms, FALSE);
}
DWORD WINAPI WaitForMultipleObjectsEx(DWORD n, const HANDLE *, BOOL,
                                       DWORD ms, BOOL bAlertable) {
    // drivemngr calls this with n==0 purely for the alertable sleep.
    if (n == 0) return SleepEx(ms, bAlertable);
    if (bAlertable) return SleepEx(ms, TRUE);
    ::usleep((useconds_t)(ms == INFINITE ? 1000 : ms) * 1000);
    return WAIT_OBJECT_0;
}
DWORD WINAPI MsgWaitForMultipleObjectsEx(DWORD n, const HANDLE *,
                                          DWORD ms, DWORD, DWORD flags) {
    // n==0 in drivemngr — a pure alertable timed wait.
    BOOL alertable = (flags & MWMO_ALERTABLE) ? TRUE : FALSE;
    if (n == 0) return SleepEx(ms, alertable);
    return alertable ? SleepEx(ms, TRUE) : WAIT_TIMEOUT;
}

// ── Events ──────────────────────────────────────────────────────
// Minimal: an Event handle is a heap flag; Set/Reset toggle it,
// Wait spins briefly.  drivemngr uses thread handles for sync, not
// events, so these are rarely exercised — keep simple + correct.
struct EventObject { std::atomic<bool> signaled; bool manual; };
HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES, BOOL manual, BOOL initial, LPCWSTR) {
    auto *e = new EventObject{ {initial != FALSE}, manual != FALSE };
    return reinterpret_cast<HANDLE>(e);
}
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES s, BOOL m, BOOL i, LPCSTR) {
    return CreateEventW(s, m, i, nullptr);
}
BOOL WINAPI SetEvent(HANDLE h) {
    if (h) reinterpret_cast<EventObject *>(h)->signaled = true;
    return TRUE;
}
BOOL WINAPI ResetEvent(HANDLE h) {
    if (h) reinterpret_cast<EventObject *>(h)->signaled = false;
    return TRUE;
}

// ── Critical sections (recursive pthread mutex) ─────────────────
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION cs) {
    if (!cs) return;
    auto *m = new pthread_mutex_t;
    pthread_mutexattr_t a;
    pthread_mutexattr_init(&a);
    pthread_mutexattr_settype(&a, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(m, &a);
    pthread_mutexattr_destroy(&a);
    cs->opaque = m;
}
BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION cs, DWORD) {
    InitializeCriticalSection(cs);
    return TRUE;
}
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs && cs->opaque) {
        auto *m = static_cast<pthread_mutex_t *>(cs->opaque);
        pthread_mutex_destroy(m);
        delete m;
        cs->opaque = nullptr;
    }
}
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs && cs->opaque) pthread_mutex_lock(static_cast<pthread_mutex_t *>(cs->opaque));
}
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs && cs->opaque) pthread_mutex_unlock(static_cast<pthread_mutex_t *>(cs->opaque));
}
BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION cs) {
    if (cs && cs->opaque)
        return pthread_mutex_trylock(static_cast<pthread_mutex_t *>(cs->opaque)) == 0;
    return FALSE;
}

// ── Last-error + error mode ─────────────────────────────────────
static thread_local DWORD g_lastError = 0;
DWORD WINAPI GetLastError(void)        { return g_lastError; }
void  WINAPI SetLastError(DWORD e)     { g_lastError = e; }
UINT  WINAPI SetErrorMode(UINT)        { return 0; }

// ── Perf counters ───────────────────────────────────────────────
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *li) {
    if (!li) return FALSE;
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    li->QuadPart = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    return TRUE;
}
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *li) {
    if (!li) return FALSE;
    li->QuadPart = 1000000000LL;  // we count in ns
    return TRUE;
}

// ── Logical drives — Linux has no drive letters ─────────────────
DWORD WINAPI GetLogicalDrives(void)        { return 0; }
UINT  WINAPI GetDriveTypeA(LPCSTR)         { return DRIVE_NO_ROOT_DIR; }
UINT  WINAPI GetDriveTypeW(LPCWSTR)        { return DRIVE_NO_ROOT_DIR; }
BOOL  WINAPI GetVolumeInformationA(LPCSTR, LPSTR v, DWORD vs, LPDWORD,
                                    LPDWORD, LPDWORD, LPSTR f, DWORD fs) {
    if (v && vs) v[0] = 0; if (f && fs) f[0] = 0; return FALSE;
}
BOOL  WINAPI GetVolumeInformationW(LPCWSTR, LPWSTR v, DWORD vs, LPDWORD,
                                    LPDWORD, LPDWORD, LPWSTR f, DWORD fs) {
    if (v && vs) v[0] = 0; if (f && fs) f[0] = 0; return FALSE;
}
DWORD WINAPI QueryDosDeviceW(LPCWSTR, LPWSTR t, DWORD n) {
    if (t && n) t[0] = 0; return 0;
}
BOOL WINAPI GetVolumeNameForVolumeMountPointW(LPCWSTR, LPWSTR v, DWORD n) {
    if (v && n) v[0] = 0; return FALSE;
}

// ── DeviceIoControl — G.1 no-op (libcdio replaces in G.4) ───────
BOOL WINAPI DeviceIoControl(HANDLE, DWORD, LPVOID, DWORD,
                             LPVOID, DWORD, LPDWORD br, LPOVERLAPPED) {
    if (br) *br = 0;
    g_lastError = ERROR_NOT_READY;
    return FALSE;
}

}  // extern "C"
