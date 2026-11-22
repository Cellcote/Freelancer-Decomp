// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): the MSVC CRT names the code uses that the C library here does not have, with
// MSVC's behaviour where it differs (_vsnprintf's missing terminator, _splitpath's split). For
// macOS and Linux only, like the rest of src/include/posix (posix/windows.h includes it).

#pragma once

#if defined(_WIN32)
#error "posix/msvc_crt.h is for macOS and Linux; Windows has the MSVC CRT"
#endif

#include "platform.h"

#include <cctype>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <strings.h>
#include <unistd.h>

#define _MAX_PATH 260
#define _MAX_DRIVE 3
#define _MAX_DIR 256
#define _MAX_FNAME 256
#define _MAX_EXT 256

// _stricmp / _strnicmp: both sides lowered, as MSVC's in the C locale (so the order of '_'
// against letters is the same).
inline int _stricmp(char const* Left, char const* Right)
{
    return strcasecmp(Left, Right);
}
inline int _strnicmp(char const* Left, char const* Right, std::size_t Count)
{
    return strncasecmp(Left, Right, Count);
}
inline int _memicmp(void const* Left, void const* Right, std::size_t Count)
{
    unsigned char const* A = static_cast<unsigned char const*>(Left);
    unsigned char const* B = static_cast<unsigned char const*>(Right);
    for (std::size_t Index = 0; Index < Count; ++Index)
    {
        int const Difference = std::tolower(A[Index]) - std::tolower(B[Index]);
        if (Difference != 0)
            return Difference;
    }
    return 0;
}

// _vsnprintf / _snprintf: MSVC's, not C99's. When the text fills Count exactly there is no
// terminator; when it does not fit, Count characters are written, again without one, and the
// result is -1.
inline int _vsnprintf(char* Buffer, std::size_t Count, char const* Format, va_list Args)
{
    va_list Measure;
    va_copy(Measure, Args);
    int const Length = std::vsnprintf(nullptr, 0, Format, Measure);
    va_end(Measure);
    if (Length < 0)
        return -1;
    std::vector<char> Text(static_cast<std::size_t>(Length) + 1);
    std::vsnprintf(Text.data(), Text.size(), Format, Args);
    std::size_t const Size = static_cast<std::size_t>(Length);
    if (Size < Count)
    {
        std::memcpy(Buffer, Text.data(), Size + 1);
        return Length;
    }
    std::memcpy(Buffer, Text.data(), Count);
    return Size == Count ? Length : -1;
}
inline int _snprintf(char* Buffer, std::size_t Count, char const* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    int const Result = _vsnprintf(Buffer, Count, Format, Args);
    va_end(Args);
    return Result;
}

// _splitpath: drive ("C:"), directory (with its trailing separator), file name and extension
// (with its '.'; the last '.' after the last separator). Any output may be nullptr. '/' and '\'
// both separate.
inline void _splitpath(char const* Path, char* Drive, char* Directory, char* FileName,
                       char* Extension)
{
    auto Put = [](char* Out, std::size_t Limit, char const* Begin, char const* End)
    {
        if (Out == nullptr)
            return;
        std::size_t Length = static_cast<std::size_t>(End - Begin);
        if (Length > Limit - 1)
            Length = Limit - 1;
        std::memcpy(Out, Begin, Length);
        Out[Length] = 0;
    };
    char const* Cursor = Path;
    if (Cursor[0] != 0 && Cursor[1] == ':')
    {
        Put(Drive, _MAX_DRIVE, Cursor, Cursor + 2);
        Cursor += 2;
    }
    else
    {
        Put(Drive, _MAX_DRIVE, Cursor, Cursor);
    }
    char const* LastSeparator = nullptr;
    char const* LastDot = nullptr;
    for (char const* Scan = Cursor; *Scan != 0; ++Scan)
    {
        if (*Scan == '\\' || *Scan == '/')
            LastSeparator = Scan;
        else if (*Scan == '.')
            LastDot = Scan;
    }
    char const* const NameStart = LastSeparator != nullptr ? LastSeparator + 1 : Cursor;
    Put(Directory, _MAX_DIR, Cursor, NameStart);
    char const* const End = Cursor + std::strlen(Cursor);
    if (LastDot != nullptr && LastDot >= NameStart)
    {
        Put(FileName, _MAX_FNAME, NameStart, LastDot);
        Put(Extension, _MAX_EXT, LastDot, End);
    }
    else
    {
        Put(FileName, _MAX_FNAME, NameStart, End);
        Put(Extension, _MAX_EXT, End, End);
    }
}

// _makepath: the inverse. A directory without a trailing separator gets a '\', an extension
// without a '.' gets one.
inline void _makepath(char* Path, char const* Drive, char const* Directory, char const* FileName,
                      char const* Extension)
{
    std::string Text;
    if (Drive != nullptr && Drive[0] != 0)
    {
        Text += Drive[0];
        Text += ':';
    }
    if (Directory != nullptr && Directory[0] != 0)
    {
        Text += Directory;
        if (Text.back() != '\\' && Text.back() != '/')
            Text += '\\';
    }
    if (FileName != nullptr)
        Text += FileName;
    if (Extension != nullptr && Extension[0] != 0)
    {
        if (Extension[0] != '.')
            Text += '.';
        Text += Extension;
    }
    std::memcpy(Path, Text.c_str(), Text.size() + 1);
}

// _access: whether the path exists (Mode 0) or allows the access asked, resolved as the
// platform layer resolves paths (without regard to case, '\' separating).
inline int _access(char const* Path, int Mode)
{
    std::string const Resolved = Platform::Detail::ResolvePath(Path);
    return access(Resolved.c_str(),
                  Mode == 0 ? F_OK : ((Mode & 2) != 0 ? W_OK : 0) | ((Mode & 4) != 0 ? R_OK : 0));
}

inline char* _strlwr(char* Text)
{
    for (char* Cursor = Text; *Cursor != 0; ++Cursor)
        *Cursor = static_cast<char>(std::tolower(static_cast<unsigned char>(*Cursor)));
    return Text;
}
inline char* _strupr(char* Text)
{
    for (char* Cursor = Text; *Cursor != 0; ++Cursor)
        *Cursor = static_cast<char>(std::toupper(static_cast<unsigned char>(*Cursor)));
    return Text;
}
inline char* _strdup(char const* Text)
{
    return strdup(Text);
}
