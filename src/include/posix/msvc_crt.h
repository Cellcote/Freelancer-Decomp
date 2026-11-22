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
#include <cstdlib>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <cwchar>
#include <string>
#include <vector>

#include <strings.h>
#include <unistd.h>

#define _MAX_PATH 260

#define _copysign(x, y) std::copysign((double)(x), (double)(y))
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
// _wcsicmp: as MSVC's in the C locale, which lowers A-Z only; the difference of the first
// characters that differ.
inline int _wcsicmp(wchar_t const* Left, wchar_t const* Right)
{
    auto Lower = [](wchar_t Char) -> long
    {
        return (Char >= L'A' && Char <= L'Z') ? static_cast<long>(Char - L'A' + L'a')
                                              : static_cast<long>(Char);
    };
    for (;; ++Left, ++Right)
    {
        long const Difference = Lower(*Left) - Lower(*Right);
        if (Difference != 0 || *Left == 0)
            return static_cast<int>(Difference);
    }
}
// _wcsnicmp: _wcsicmp over at most Count characters.
inline int _wcsnicmp(wchar_t const* Left, wchar_t const* Right, std::size_t Count)
{
    auto Lower = [](wchar_t Char) -> long
    {
        return (Char >= L'A' && Char <= L'Z') ? static_cast<long>(Char - L'A' + L'a')
                                              : static_cast<long>(Char);
    };
    for (; Count != 0; --Count, ++Left, ++Right)
    {
        long const Difference = Lower(*Left) - Lower(*Right);
        if (Difference != 0 || *Left == 0)
            return static_cast<int>(Difference);
    }
    return 0;
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

namespace PosixMsvcCrt
{
// A wide printf format in MSVC's legacy meaning made C's: in MSVC's wide functions %s and %c
// take wide characters (C's %ls / %lc), and %S / %C, %hs / %hc take narrow ones (C's %s / %c).
inline std::wstring WideFormat(wchar_t const* Format)
{
    std::wstring Out;
    while (*Format != 0)
    {
        if (*Format != L'%')
        {
            Out += *Format++;
            continue;
        }
        Out += *Format++;
        if (*Format == L'%')
        {
            Out += *Format++;
            continue;
        }
        // Flags, width, precision.
        while (*Format != 0 && std::wcschr(L"-+ #0123456789.*", *Format) != nullptr)
            Out += *Format++;
        if ((*Format == L'h' || *Format == L'w' || *Format == L'l') &&
            (Format[1] == L's' || Format[1] == L'c'))
        {
            if (*Format != L'h')
                Out += L'l';
            ++Format;
            Out += *Format++;
            continue;
        }
        if (*Format == L's' || *Format == L'c')
        {
            Out += L'l';
            Out += *Format++;
            continue;
        }
        if (*Format == L'S' || *Format == L'C')
        {
            Out += static_cast<wchar_t>(*Format == L'S' ? L's' : L'c');
            ++Format;
            continue;
        }
    }
    return Out;
}
}

// _swprintf: MSVC's legacy swprintf, with no buffer size (the caller's buffer is large enough,
// as on Windows) and %s for a wide string. The length written, or -1.
inline int _swprintf(wchar_t* Buffer, wchar_t const* Format, ...)
{
    std::wstring const CFormat = PosixMsvcCrt::WideFormat(Format);
    va_list Args;
    va_start(Args, Format);
    int const Result = std::vswprintf(Buffer, 0x7FFFFFFF / sizeof(wchar_t), CFormat.c_str(), Args);
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

// _itoa / _ltoa / _ultoa: Value in Radix (2..36), lower-case digits; a negative value has a
// '-' only in radix 10 (otherwise its two's-complement bits), as MSVC.
inline char* _ultoa(unsigned long long Value, char* Buffer, int Radix)
{
    char Digits[72];
    int Length = 0;
    do
    {
        const unsigned Digit = (unsigned)(Value % (unsigned)Radix);
        Digits[Length++] = (char)(Digit < 10 ? '0' + Digit : 'a' + Digit - 10);
        Value /= (unsigned)Radix;
    } while (Value != 0);
    for (int Index = 0; Index < Length; ++Index)
        Buffer[Index] = Digits[Length - 1 - Index];
    Buffer[Length] = 0;
    return Buffer;
}
inline char* _itoa(int Value, char* Buffer, int Radix)
{
    if (Radix == 10 && Value < 0)
    {
        Buffer[0] = '-';
        _ultoa(0ull - (unsigned long long)(long long)Value, Buffer + 1, 10);
        return Buffer;
    }
    return _ultoa((unsigned int)Value, Buffer, Radix);
}
inline char* _ltoa(FlLong Value, char* Buffer, int Radix)
{
    return _itoa((int)Value, Buffer, Radix);
}

// The "secure" CRT functions the code uses, with MSVC's results for what the callers pass. A
// destination too small: strcpy_s / strncpy_s fail (an empty destination, ERANGE / EINVAL;
// MSVC also calls its invalid-parameter handler); with _TRUNCATE the copy is cut and STRUNCATE
// returned. sprintf_s fails (an empty destination, -1); _snprintf_s with _TRUNCATE cuts and
// returns -1.
#define _TRUNCATE ((std::size_t)-1)
#ifndef STRUNCATE
#define STRUNCATE 80
#endif
typedef int errno_t;
inline errno_t strncpy_s(char* Dest, std::size_t DestSize, char const* Source, std::size_t Count)
{
    if (Dest == nullptr || DestSize == 0)
        return EINVAL;
    if (Source == nullptr)
    {
        Dest[0] = 0;
        return EINVAL;
    }
    std::size_t Length = 0;
    while (Length < Count && Source[Length] != 0 && (Count != _TRUNCATE || Length < DestSize))
        ++Length;
    if (Length >= DestSize)
    {
        if (Count == _TRUNCATE)
        {
            std::memcpy(Dest, Source, DestSize - 1);
            Dest[DestSize - 1] = 0;
            return STRUNCATE;
        }
        Dest[0] = 0;
        return ERANGE;
    }
    std::memcpy(Dest, Source, Length);
    Dest[Length] = 0;
    return 0;
}
inline errno_t strcpy_s(char* Dest, std::size_t DestSize, char const* Source)
{
    if (Dest == nullptr || DestSize == 0)
        return EINVAL;
    if (Source == nullptr)
    {
        Dest[0] = 0;
        return EINVAL;
    }
    const std::size_t Length = std::strlen(Source);
    if (Length >= DestSize)
    {
        Dest[0] = 0;
        return ERANGE;
    }
    std::memcpy(Dest, Source, Length + 1);
    return 0;
}
template <std::size_t Size>
errno_t strcpy_s(char (&Dest)[Size], char const* Source)
{
    return strcpy_s(Dest, Size, Source);
}
template <std::size_t Size>
errno_t strncpy_s(char (&Dest)[Size], char const* Source, std::size_t Count)
{
    return strncpy_s(Dest, Size, Source, Count);
}
inline int vsprintf_s(char* Buffer, std::size_t Size, char const* Format, va_list Args)
{
    if (Buffer == nullptr || Size == 0)
        return -1;
    const int Length = std::vsnprintf(Buffer, Size, Format, Args);
    if (Length < 0 || (std::size_t)Length >= Size)
    {
        Buffer[0] = 0;
        return -1;
    }
    return Length;
}
inline int sprintf_s(char* Buffer, std::size_t Size, char const* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    const int Result = vsprintf_s(Buffer, Size, Format, Args);
    va_end(Args);
    return Result;
}
template <std::size_t Size>
int sprintf_s(char (&Buffer)[Size], char const* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    const int Result = vsprintf_s(Buffer, Size, Format, Args);
    va_end(Args);
    return Result;
}
inline int _vsnprintf_s(char* Buffer, std::size_t Size, std::size_t Count, char const* Format,
                        va_list Args)
{
    if (Buffer == nullptr || Size == 0)
        return -1;
    const std::size_t Limit = Count == _TRUNCATE ? Size - 1 : (Count < Size - 1 ? Count : Size - 1);
    va_list Copy;
    va_copy(Copy, Args);
    const int Length = std::vsnprintf(nullptr, 0, Format, Copy);
    va_end(Copy);
    if (Length < 0)
    {
        Buffer[0] = 0;
        return -1;
    }
    std::vector<char> Text((std::size_t)Length + 1);
    std::vsnprintf(Text.data(), Text.size(), Format, Args);
    if ((std::size_t)Length <= Limit)
    {
        std::memcpy(Buffer, Text.data(), (std::size_t)Length + 1);
        return Length;
    }
    if (Count != _TRUNCATE && Count >= Size)
    {
        Buffer[0] = 0; // the buffer is too small for Count: MSVC's invalid-parameter case
        return -1;
    }
    std::memcpy(Buffer, Text.data(), Limit);
    Buffer[Limit] = 0;
    return -1;
}
inline int _snprintf_s(char* Buffer, std::size_t Size, std::size_t Count, char const* Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    const int Result = _vsnprintf_s(Buffer, Size, Count, Format, Args);
    va_end(Args);
    return Result;
}
inline wchar_t* _wcsdup(wchar_t const* Text)
{
    const std::size_t Length = std::wcslen(Text) + 1;
    wchar_t* const Copy = static_cast<wchar_t*>(std::malloc(Length * sizeof(wchar_t)));
    if (Copy != nullptr)
        std::memcpy(Copy, Text, Length * sizeof(wchar_t));
    return Copy;
}

// _control87: the x87 control word. There is no x87 here (nor on x64 Windows, where MSVC ignores
// the precision bits), so nothing is set; the result is the default control word.
#define _MCW_PC 0x00030000
#define _PC_64 0x00000000
#define _PC_53 0x00010000
#define _PC_24 0x00020000
#define _MCW_RC 0x00000300
#define _RC_NEAR 0x00000000
#define _MCW_EM 0x0008001F
inline unsigned int _control87(unsigned int, unsigned int)
{
    return 0x0009001F;
}
inline unsigned int _controlfp(unsigned int New, unsigned int Mask)
{
    return _control87(New, Mask);
}

// _makepath_s: _makepath with the buffer's size. ERANGE (and an empty result) when the path
// does not fit, as MSVC's does.
inline errno_t _makepath_s(char* Path, std::size_t Size, char const* Drive, char const* Directory,
                           char const* FileName, char const* Extension)
{
    if (Path == nullptr || Size == 0)
        return EINVAL;
    char Built[4096];
    std::size_t Needed = 8; // drive, separators, '.', terminator
    for (char const* Part : {Directory, FileName, Extension})
        Needed += Part != nullptr ? std::strlen(Part) : 0;
    if (Needed > sizeof(Built))
    {
        Path[0] = 0;
        return ERANGE;
    }
    _makepath(Built, Drive, Directory, FileName, Extension);
    if (std::strlen(Built) + 1 > Size)
    {
        Path[0] = 0;
        return ERANGE;
    }
    std::memcpy(Path, Built, std::strlen(Built) + 1);
    return 0;
}
template <std::size_t Size>
errno_t _makepath_s(char (&Path)[Size], char const* Drive, char const* Directory,
                    char const* FileName, char const* Extension)
{
    return _makepath_s(Path, Size, Drive, Directory, FileName, Extension);
}

// fopen and _unlink take Windows paths ('\' separating, any case): resolved as the platform
// layer resolves them. fopen is a macro so that every fopen in a non-portable module resolves
// its path; this is the C library's fopen underneath.
namespace PosixMsvcCrt
{
inline FILE* OpenFile(char const* Path, char const* Mode)
{
    if (Path == nullptr || Mode == nullptr)
        return nullptr;
    return std::fopen(Platform::Detail::ResolvePath(Path).c_str(), Mode);
}
}
#define fopen(Path, Mode) PosixMsvcCrt::OpenFile((Path), (Mode))
inline errno_t fopen_s(FILE** File, char const* Path, char const* Mode)
{
    if (File == nullptr)
        return EINVAL;
    *File = PosixMsvcCrt::OpenFile(Path, Mode);
    return *File != nullptr ? 0 : (errno != 0 ? errno : ENOENT);
}
inline int _unlink(char const* Path)
{
    return unlink(Platform::Detail::ResolvePath(Path).c_str());
}

// sscanf_s for formats with numeric conversions only, where it is sscanf. A %s, %c or %[
// (which take a buffer size after the pointer in sscanf_s) is not supported and aborts.
template <class... Args>
int sscanf_s(char const* Text, char const* Format, Args... Arguments)
{
    for (char const* Cursor = Format; *Cursor != 0; ++Cursor)
    {
        if (*Cursor != '%')
            continue;
        ++Cursor;
        while (*Cursor != 0 && std::strchr("*0123456789hlLjzt", *Cursor) != nullptr)
            ++Cursor;
        if (*Cursor == 's' || *Cursor == 'c' || *Cursor == '[')
            std::abort();
        if (*Cursor == 0)
            break;
    }
    return std::sscanf(Text, Format, Arguments...);
}

// _vswprintf: MSVC's legacy vswprintf, with no buffer size and %s for a wide string (see
// _swprintf).
inline int _vswprintf(wchar_t* Buffer, wchar_t const* Format, va_list Args)
{
    std::wstring const CFormat = PosixMsvcCrt::WideFormat(Format);
    return std::vswprintf(Buffer, 0x7FFFFFFF / sizeof(wchar_t), CFormat.c_str(), Args);
}

// _tzset, _strtime ("hh:mm:ss") and _strdate ("mm/dd/yy"): the local time, into a buffer of at
// least 9 characters.
inline void _tzset()
{
    tzset();
}
inline char* _strtime(char* Buffer)
{
    const std::time_t Now = std::time(nullptr);
    struct tm Parts;
    localtime_r(&Now, &Parts);
    std::strftime(Buffer, 9, "%H:%M:%S", &Parts);
    return Buffer;
}
inline char* _strdate(char* Buffer)
{
    const std::time_t Now = std::time(nullptr);
    struct tm Parts;
    localtime_r(&Now, &Parts);
    std::strftime(Buffer, 9, "%m/%d/%y", &Parts);
    return Buffer;
}
