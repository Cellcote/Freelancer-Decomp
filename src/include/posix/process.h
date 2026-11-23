// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the MSVC CRT's <process.h> on macOS and Linux (src/include/posix,
// non-Windows only). The process id is the C library's. The programs the client starts with
// _spawnl (Flasher.exe) are Windows programs, so _spawnl fails as for a missing file (-1,
// errno ENOENT).

#pragma once

#if defined(_WIN32)
#error "posix/process.h is the stand-in for macOS and Linux; Windows uses the MSVC CRT's"
#endif

#include <cerrno>
#include <unistd.h>

#define _P_WAIT 0
#define _P_NOWAIT 1

inline int _getpid()
{
    return static_cast<int>(getpid());
}

inline long _spawnl(int, const char*, const char*, ...)
{
    errno = ENOENT;
    return -1;
}
