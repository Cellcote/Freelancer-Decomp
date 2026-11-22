// PORT(macOS): <io.h> on macOS and Linux: _access, _unlink are in posix/msvc_crt.h; the
// _findfirst family is here, on the platform layer's FindFirst.
#pragma once
#include "posix/msvc_crt.h"

#include <cstdint>
#include <ctime>

#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_SUBDIR 0x10
#define _A_ARCH 0x20

typedef unsigned long _fsize_t;
struct _finddata_t
{
    unsigned attrib;
    std::time_t time_create;
    std::time_t time_access;
    std::time_t time_write;
    _fsize_t size;
    char name[260];
};

namespace PosixMsvcCrt
{
inline std::time_t UnixTime(Platform::FileTime const& Time)
{
    const std::uint64_t Ticks = ((std::uint64_t)Time.dwHighDateTime << 32) | Time.dwLowDateTime;
    return (std::time_t)((long long)(Ticks / 10000000ULL) - 11644473600LL);
}
inline void ToFindData(Platform::FindData const& From, _finddata_t* To)
{
    To->attrib = From.dwFileAttributes & 0x37u;
    To->time_create = UnixTime(From.ftCreationTime);
    To->time_access = UnixTime(From.ftLastAccessTime);
    To->time_write = UnixTime(From.ftLastWriteTime);
    To->size = From.nFileSizeLow;
    std::memcpy(To->name, From.cFileName, sizeof(To->name));
}
}

// -1 when nothing matches.
inline intptr_t _findfirst(char const* Pattern, _finddata_t* Data)
{
    Platform::FindData Found;
    const Platform::Handle Search = Platform::FindFirst(Pattern, &Found);
    if (Search == Platform::InvalidHandle())
        return -1;
    PosixMsvcCrt::ToFindData(Found, Data);
    return (intptr_t)Search;
}
// 0, or -1 when there is no further match.
inline int _findnext(intptr_t Search, _finddata_t* Data)
{
    Platform::FindData Found;
    if (!Platform::FindNext((Platform::Handle)Search, &Found))
        return -1;
    PosixMsvcCrt::ToFindData(Found, Data);
    return 0;
}
inline int _findclose(intptr_t Search)
{
    return Platform::FindClose((Platform::Handle)Search) ? 0 : -1;
}
