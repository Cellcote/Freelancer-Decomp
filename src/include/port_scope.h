// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// PORT(tools): scope guards for the port's own programs (FLRegress, FLDedicated, DPNetInteropTest).
// Retail modules keep retail's hand-written cleanup.
//
// Each guard undoes one step when its scope ends, however it ends. Written right after the step
// succeeds, it keeps the step next to its teardown, and an early return undoes exactly what was
// done before it, in reverse order.

#if defined(_WIN32)
#include <objbase.h>
#endif

// Runs Cleanup when the scope ends:
//     const TScopeExit ShutdownDALib([] { DALib::Shutdown(nullptr); });
template <typename FCleanup>
class TScopeExit
{
public:
    explicit TScopeExit(FCleanup InCleanup) : Cleanup(InCleanup) {}
    ~TScopeExit() { Cleanup(); }
    TScopeExit(const TScopeExit&) = delete;
    TScopeExit& operator=(const TScopeExit&) = delete;

private:
    FCleanup Cleanup;
};

#if defined(_WIN32)
// COM on this thread for the scope: CoUninitialize only when CoInitializeEx succeeded (S_OK or
// S_FALSE, "already initialised", which still needs its CoUninitialize).
class FComInit
{
public:
    explicit FComInit(DWORD Model = COINIT_APARTMENTTHREADED)
        : bInitialised(SUCCEEDED(CoInitializeEx(nullptr, Model)))
    {
    }
    ~FComInit()
    {
        if (bInitialised)
        {
            CoUninitialize();
        }
    }
    FComInit(const FComInit&) = delete;
    FComInit& operator=(const FComInit&) = delete;

private:
    bool bInitialised;
};
#endif
