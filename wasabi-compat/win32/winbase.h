// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Florian Kleber
#ifndef _WINBASE_H_INCLUDED_
#define _WINBASE_H_INCLUDED_
//
// winbase.h — the kernel32 surface ml_disc's drivemngr.cpp needs:
// threads, critical sections, user-mode APCs, alertable waits,
// logical-drive enumeration, perf counters, last-error, error mode.
//
// The threading + APC primitives are backed by REAL pthreads (see
// kernel32.cpp), not stubs: drivemngr runs a medium-poll worker
// thread and dispatches its drive-info jobs through QueueUserAPC +
// alertable SleepEx.  A no-op threading layer would deadlock that
// queue, so these are functional.
//

#include "basetsd.h"
#include "windef.h"

#ifdef __cplusplus
extern "C" {
#endif

// ── 64-bit integer types (LARGE_INTEGER is used by the perf-counter
// + file-size APIs below; not in windef.h, so define here, guarded). ──
#ifndef _LARGE_INTEGER_DEFINED
#define _LARGE_INTEGER_DEFINED 1
typedef long long          LONGLONG;
typedef unsigned long long ULONGLONG;
typedef union _LARGE_INTEGER {
    struct { DWORD LowPart; LONG  HighPart; } u;
    LONGLONG QuadPart;
} LARGE_INTEGER, *PLARGE_INTEGER;
typedef union _ULARGE_INTEGER {
    struct { DWORD LowPart; DWORD HighPart; } u;
    ULONGLONG QuadPart;
} ULARGE_INTEGER, *PULARGE_INTEGER;
#endif

// ── Global atoms (CGlobalAtom.h wraps these for window-prop keys) ──
#ifndef _ATOM_DEFINED
#define _ATOM_DEFINED 1
typedef WORD ATOM;
#endif
ATOM WINAPI GlobalAddAtomW(LPCWSTR name);
ATOM WINAPI GlobalDeleteAtom(ATOM atom);

// ── Wait return codes ───────────────────────────────────────────
#define WAIT_OBJECT_0        0x00000000
#define WAIT_ABANDONED       0x00000080
#define WAIT_TIMEOUT         0x00000102
#define WAIT_FAILED          0xFFFFFFFF
#define WAIT_IO_COMPLETION   0x000000C0
#define INFINITE             0xFFFFFFFF

// ── Thread routine + APC types ──────────────────────────────────
typedef DWORD (WINAPI *LPTHREAD_START_ROUTINE)(LPVOID lpParameter);
typedef void  (WINAPI *PAPCFUNC)(ULONG_PTR Parameter);
// compat-shim.h (force-included) also defines this; guard so windows.h
// can pull winbase.h without a redefinition clash.
#ifndef _SECURITY_ATTRIBUTES_DEFINED
#define _SECURITY_ATTRIBUTES_DEFINED 1
typedef struct _SECURITY_ATTRIBUTES {
    DWORD  nLength;
    LPVOID lpSecurityDescriptor;
    BOOL   bInheritHandle;
} SECURITY_ATTRIBUTES, *PSECURITY_ATTRIBUTES, *LPSECURITY_ATTRIBUTES;
#endif

// ── Threads ─────────────────────────────────────────────────────
HANDLE WINAPI CreateThread(LPSECURITY_ATTRIBUTES, SIZE_T,
                            LPTHREAD_START_ROUTINE, LPVOID,
                            DWORD dwCreationFlags, LPDWORD lpThreadId);
HANDLE WINAPI GetCurrentThread(void);
HANDLE WINAPI GetCurrentProcess(void);
DWORD  WINAPI GetCurrentThreadId(void);
DWORD  WINAPI GetCurrentProcessId(void);
BOOL   WINAPI TerminateThread(HANDLE, DWORD);
BOOL   WINAPI SetThreadPriority(HANDLE, int);
int    WINAPI GetThreadPriority(HANDLE);
DWORD  WINAPI ResumeThread(HANDLE);
DWORD  WINAPI SuspendThread(HANDLE);
BOOL   WINAPI GetExitCodeThread(HANDLE, LPDWORD);
BOOL   WINAPI DuplicateHandle(HANDLE, HANDLE, HANDLE, LPHANDLE,
                               DWORD, BOOL, DWORD);
DWORD  WINAPI GetWindowThreadProcessId(HWND, LPDWORD);

#define THREAD_PRIORITY_IDLE          (-15)
#define THREAD_PRIORITY_LOWEST        (-2)
#define THREAD_PRIORITY_BELOW_NORMAL  (-1)
#define THREAD_PRIORITY_NORMAL        0
#define THREAD_PRIORITY_ABOVE_NORMAL  1
#define THREAD_PRIORITY_HIGHEST       2
#define THREAD_PRIORITY_TIME_CRITICAL 15
#define CREATE_SUSPENDED              0x00000004
#define DUPLICATE_SAME_ACCESS         0x00000002

// ── User-mode APC + alertable waits ─────────────────────────────
DWORD  WINAPI QueueUserAPC(PAPCFUNC pfnAPC, HANDLE hThread, ULONG_PTR data);
DWORD  WINAPI SleepEx(DWORD ms, BOOL bAlertable);
DWORD  WINAPI WaitForSingleObject(HANDLE, DWORD ms);
DWORD  WINAPI WaitForSingleObjectEx(HANDLE, DWORD ms, BOOL bAlertable);
DWORD  WINAPI WaitForMultipleObjects(DWORD n, const HANDLE *, BOOL, DWORD ms);
DWORD  WINAPI WaitForMultipleObjectsEx(DWORD n, const HANDLE *, BOOL,
                                        DWORD ms, BOOL bAlertable);
DWORD  WINAPI MsgWaitForMultipleObjectsEx(DWORD n, const HANDLE *,
                                           DWORD ms, DWORD wakeMask,
                                           DWORD flags);
#define MWMO_ALERTABLE       0x0002
#define MWMO_INPUTAVAILABLE  0x0004
#define MWMO_WAITALL         0x0001
#define QS_ALLINPUT          0x04FF

// ── Events ──────────────────────────────────────────────────────
HANDLE WINAPI CreateEventW(LPSECURITY_ATTRIBUTES, BOOL manualReset,
                            BOOL initialState, LPCWSTR name);
HANDLE WINAPI CreateEventA(LPSECURITY_ATTRIBUTES, BOOL, BOOL, LPCSTR);
BOOL   WINAPI SetEvent(HANDLE);
BOOL   WINAPI ResetEvent(HANDLE);
#if defined(UNICODE) || defined(_UNICODE)
#  define CreateEvent CreateEventW
#else
#  define CreateEvent CreateEventA
#endif

// ── Critical sections (recursive pthread mutex) ─────────────────
typedef struct _CRITICAL_SECTION {
    void *opaque;   // backing pthread_mutex_t* allocated lazily
} CRITICAL_SECTION, *LPCRITICAL_SECTION, *PCRITICAL_SECTION;
void WINAPI InitializeCriticalSection(LPCRITICAL_SECTION);
BOOL WINAPI InitializeCriticalSectionAndSpinCount(LPCRITICAL_SECTION, DWORD);
void WINAPI DeleteCriticalSection(LPCRITICAL_SECTION);
void WINAPI EnterCriticalSection(LPCRITICAL_SECTION);
void WINAPI LeaveCriticalSection(LPCRITICAL_SECTION);
BOOL WINAPI TryEnterCriticalSection(LPCRITICAL_SECTION);

// ── Generic handle close (threads, events) ──────────────────────
// (CloseHandle is declared in winuser.h's file-IO block.)

// ── Last-error + error mode ─────────────────────────────────────
DWORD  WINAPI GetLastError(void);
void   WINAPI SetLastError(DWORD);
UINT   WINAPI SetErrorMode(UINT);
#define SEM_FAILCRITICALERRORS      0x0001
#define SEM_NOGPFAULTERRORBOX       0x0002
#define SEM_NOALIGNMENTFAULTEXCEPT  0x0004
#define SEM_NOOPENFILEERRORBOX      0x8000
#define ERROR_SUCCESS               0L
#define ERROR_FILE_NOT_FOUND        2L
#define ERROR_ACCESS_DENIED         5L
#define ERROR_INVALID_HANDLE        6L
#define ERROR_NOT_READY             21L
#define ERROR_INSUFFICIENT_BUFFER   122L
#define ERROR_MORE_DATA             234L
#define ERROR_IO_PENDING            997L

// ── Perf counters ───────────────────────────────────────────────
BOOL WINAPI QueryPerformanceCounter(LARGE_INTEGER *);
BOOL WINAPI QueryPerformanceFrequency(LARGE_INTEGER *);

// ── Logical-drive enumeration ───────────────────────────────────
DWORD WINAPI GetLogicalDrives(void);
UINT  WINAPI GetDriveTypeA(LPCSTR);
UINT  WINAPI GetDriveTypeW(LPCWSTR);
BOOL  WINAPI GetVolumeInformationA(LPCSTR root, LPSTR volName, DWORD volSz,
                                    LPDWORD serial, LPDWORD maxComp,
                                    LPDWORD flags, LPSTR fsName, DWORD fsSz);
BOOL  WINAPI GetVolumeInformationW(LPCWSTR root, LPWSTR volName, DWORD volSz,
                                    LPDWORD serial, LPDWORD maxComp,
                                    LPDWORD flags, LPWSTR fsName, DWORD fsSz);
DWORD WINAPI QueryDosDeviceW(LPCWSTR devName, LPWSTR target, DWORD max);
BOOL  WINAPI GetVolumeNameForVolumeMountPointW(LPCWSTR mount, LPWSTR vol, DWORD n);
#if defined(UNICODE) || defined(_UNICODE)
#  define GetDriveType        GetDriveTypeW
#  define GetVolumeInformation GetVolumeInformationW
#else
#  define GetDriveType        GetDriveTypeA
#  define GetVolumeInformation GetVolumeInformationA
#endif
#define DRIVE_UNKNOWN      0
#define DRIVE_NO_ROOT_DIR  1
#define DRIVE_REMOVABLE    2
#define DRIVE_FIXED        3
#define DRIVE_REMOTE       4
#define DRIVE_CDROM        5
#define DRIVE_RAMDISK      6

// ── DeviceIoControl (stubbed in G.1, libcdio-backed in G.4) ─────
typedef struct _OVERLAPPED {
    ULONG_PTR Internal;
    ULONG_PTR InternalHigh;
    union { struct { DWORD Offset; DWORD OffsetHigh; }; PVOID Pointer; };
    HANDLE    hEvent;
} OVERLAPPED, *LPOVERLAPPED;
BOOL WINAPI DeviceIoControl(HANDLE, DWORD ioControlCode,
                             LPVOID inBuf, DWORD inSz,
                             LPVOID outBuf, DWORD outSz,
                             LPDWORD bytesReturned, LPOVERLAPPED);

// ── MultiByte sleep (non-alertable Sleep already in winuser) ────

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _WINBASE_H_INCLUDED_
