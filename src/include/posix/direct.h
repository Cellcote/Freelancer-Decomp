// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): MSVC's <direct.h> on macOS and Linux (src/include/posix, non-Windows only): the
// directory calls, on resolved paths (without regard to case, '\' separating).

#pragma once

#include "platform.h"

#include <sys/stat.h>
#include <unistd.h>

inline int _mkdir(const char* Path)
{
    return mkdir(Platform::Detail::ResolvePath(Path).c_str(), 0755);
}
inline int _rmdir(const char* Path)
{
    return rmdir(Platform::Detail::ResolvePath(Path).c_str());
}
inline char* _getcwd(char* Buffer, int Size)
{
    return getcwd(Buffer, (std::size_t)Size);
}
inline int _chdir(const char* Path)
{
    return chdir(Platform::Detail::ResolvePath(Path).c_str());
}
