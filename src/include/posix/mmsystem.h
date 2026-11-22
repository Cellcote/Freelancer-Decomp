// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): winmm's <mmsystem.h> on macOS and Linux (src/include/posix, non-Windows only):
// the multimedia timer and the wave format record. timeGetTime counts milliseconds from the
// first call on a monotonic clock (winmm counts from boot; callers only take differences, and
// both wrap at 2^32). timeBeginPeriod / timeEndPeriod have nothing to set here.

#pragma once

#if defined(_WIN32)
#error "posix/mmsystem.h is for macOS and Linux"
#endif

#include <windows.h>

#include <chrono>

typedef UINT MMRESULT;
#define MMSYSERR_NOERROR 0
#define TIMERR_NOERROR 0

inline DWORD timeGetTime()
{
    const auto Start = Platform::Detail::Shared().Start; // one zero for the whole process
    return (DWORD)std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - Start)
        .count();
}
inline MMRESULT timeBeginPeriod(UINT)
{
    return TIMERR_NOERROR;
}
inline MMRESULT timeEndPeriod(UINT)
{
    return TIMERR_NOERROR;
}

#ifndef _WAVEFORMATEX_
#define _WAVEFORMATEX_
#pragma pack(push, 1)
typedef struct tWAVEFORMATEX
{
    WORD wFormatTag;
    WORD nChannels;
    DWORD nSamplesPerSec;
    DWORD nAvgBytesPerSec;
    WORD nBlockAlign;
    WORD wBitsPerSample;
    WORD cbSize;
} WAVEFORMATEX;
#pragma pack(pop)
typedef WAVEFORMATEX* LPWAVEFORMATEX;
#endif
#define WAVE_FORMAT_PCM 1
