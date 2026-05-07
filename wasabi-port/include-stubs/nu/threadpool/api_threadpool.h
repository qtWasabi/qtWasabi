// Stub overlay: upstream unconditionally includes <windows.h>.  VM
// layer never calls into the threadpool — it's exposed via api.h as
// `threadPoolApi` but vcpu.cpp doesn't reference it.
#pragma once
#include <bfc/platform/types.h>
#include <bfc/dispatch.h>

class ThreadID;

class api_threadpool : public Dispatchable {};
