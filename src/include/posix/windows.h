// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for <windows.h> on macOS and Linux. This folder (src/include/posix) is
// on the include path of the non-Windows build only (cmake/Freelancer.cmake), so the Windows
// builds never see it and stay as they are.
//
// It has what the modules ported so far use, and grows with each one (docs/x64-client-plan.md,
// M2):
//
// - Types and calling conventions. The integer types keep their Windows sizes: DWORD and LONG
//   are 32-bit (FlULong / FlLong, fl_layout.h), and the calling conventions are empty (one
//   convention on arm64 and x86-64).
// - Structures that are already in the platform layer, under their Windows names: FILETIME,
//   WIN32_FIND_DATAA and SYSTEMTIME are Platform::FileTime, FindData and DateTime.
// - The MSVC CRT names the code uses (_stricmp, _splitpath, _access...), from posix/msvc_crt.h.
// - Small Win32 calls with an obvious meaning here (OutputDebugStringA writes to stderr,
//   SearchPathA looks along the path without regard to case), and the kernel32 file, mapping
//   and memory calls the retail code makes directly (posix/kernel32.h), over the platform layer.
// - COM's basics (posix/unknwn.h), as the SDK's <windows.h> brings them in through <ole2.h>:
//   GUID, IUnknown, the common HRESULTs, and CoCreateInstance, which finds no class (there is no
//   COM here).
// - FL_POSIX_DLLMAIN(DllMain): runs a module's DllMain as it loads and unloads (see below).

#pragma once

#if defined(_WIN32)
#error "posix/windows.h is the stand-in for macOS and Linux; Windows uses the SDK's windows.h"
#endif

#include "platform.h"
#include "posix/msvc_crt.h"

#include <climits>
#include <cstdarg>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include <dlfcn.h>
#include <strings.h>

// ---------------------------------------------------------------------------------------------
// Calling conventions and declaration attributes
// ---------------------------------------------------------------------------------------------

#define __stdcall
#define __cdecl
#define __fastcall
#define __thiscall
#define WINAPI
#define APIENTRY
#define CALLBACK
#define WINAPIV
#define __forceinline inline __attribute__((always_inline))

// ---------------------------------------------------------------------------------------------
// Types (Windows sizes: 32-bit DWORD and LONG)
// ---------------------------------------------------------------------------------------------

#define __int64 long long
#define __int32 int
#define __int16 short
#define __int8 char

typedef int BOOL;
typedef unsigned char BYTE;
typedef unsigned char UCHAR;
typedef unsigned short WORD;
typedef unsigned short USHORT;
typedef FlULong DWORD;
typedef FlULong ULONG;
typedef FlLong LONG;
typedef int INT;
typedef unsigned int UINT;
typedef char CHAR;
typedef short SHORT;
typedef float FLOAT;
typedef void VOID;
typedef std::int64_t LONGLONG; // int64_t, as on Windows (long on Linux, long long on macOS)
typedef std::uint64_t ULONGLONG;
typedef std::uint64_t DWORD64;
typedef std::intptr_t INT_PTR;
typedef std::uintptr_t UINT_PTR;
typedef std::intptr_t LONG_PTR;
typedef std::uintptr_t ULONG_PTR;
typedef std::uintptr_t DWORD_PTR;
typedef std::size_t SIZE_T;
typedef LONG HRESULT;

typedef BOOL* LPBOOL;
typedef BYTE* LPBYTE;
typedef WORD* LPWORD;
typedef DWORD* LPDWORD;
typedef LONG* LPLONG;
typedef void* LPVOID;
typedef void const* LPCVOID;
typedef char* LPSTR;
typedef char const* LPCSTR;
typedef char* PSTR;
typedef char const* PCSTR;
typedef void* PVOID;
typedef BYTE* PBYTE;
typedef DWORD* PDWORD;
typedef int* PINT;
typedef UINT* PUINT;
// Wide characters are the C library's wchar_t (32-bit here, 16-bit on Windows): the retail code
// passes L"" literals and wchar_t arrays where the Windows API takes WCHAR.
typedef wchar_t WCHAR;
typedef WCHAR* LPWSTR;
typedef WCHAR const* LPCWSTR;
typedef WCHAR* PWSTR;
typedef WCHAR const* PCWSTR;

typedef void* HANDLE;
typedef HANDLE* LPHANDLE;
typedef void* HMODULE;
typedef void* HINSTANCE;
typedef void* FARPROC;
typedef struct HWND__* HWND;
typedef struct HDC__* HDC;
typedef struct HICON__* HICON;
typedef struct HCURSOR__* HCURSOR;
typedef struct HBRUSH__* HBRUSH;
typedef struct HMENU__* HMENU;
typedef struct HKEY__* HKEY;

typedef UINT_PTR WPARAM;
typedef LONG_PTR LPARAM;
typedef LONG_PTR LRESULT;

typedef Platform::FileTime FILETIME;
typedef FILETIME* LPFILETIME;
typedef FILETIME* PFILETIME;
typedef Platform::FindData WIN32_FIND_DATAA;
typedef Platform::FindData WIN32_FIND_DATA;
typedef WIN32_FIND_DATAA* LPWIN32_FIND_DATAA;
typedef Platform::DateTime SYSTEMTIME;
typedef SYSTEMTIME* LPSYSTEMTIME;

union LARGE_INTEGER
{
    struct
    {
        DWORD LowPart;
        LONG HighPart;
    };
    LONGLONG QuadPart;
};
union ULARGE_INTEGER
{
    struct
    {
        DWORD LowPart;
        DWORD HighPart;
    };
    ULONGLONG QuadPart;
};

// The SDK's tag names too: retail headers declare `struct tagRECT;` and mangle it.
typedef struct tagRECT
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
} RECT;
typedef struct tagPOINT
{
    LONG x;
    LONG y;
} POINT;
typedef struct tagSIZE
{
    LONG cx;
    LONG cy;
} SIZE;
typedef RECT* LPRECT;
typedef POINT* LPPOINT;
typedef SIZE* LPSIZE;

// ---------------------------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------------------------

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define MAX_PATH 260
#define INFINITE 0xFFFFFFFFu
#define INVALID_HANDLE_VALUE (Platform::InvalidHandle())
#define INVALID_FILE_ATTRIBUTES ((DWORD)0xFFFFFFFFu)
#define FILE_ATTRIBUTE_READONLY 0x01
#define FILE_ATTRIBUTE_HIDDEN 0x02
#define FILE_ATTRIBUTE_SYSTEM 0x04
#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define FILE_ATTRIBUTE_ARCHIVE 0x20
#define FILE_ATTRIBUTE_NORMAL 0x80

#define DLL_PROCESS_DETACH 0
#define DLL_PROCESS_ATTACH 1
#define DLL_THREAD_ATTACH 2
#define DLL_THREAD_DETACH 3

#define S_OK ((HRESULT)0)
#define S_FALSE ((HRESULT)1)
#define E_FAIL ((HRESULT)0x80004005)
#define SUCCEEDED(Result) (((HRESULT)(Result)) >= 0)
#define FAILED(Result) (((HRESULT)(Result)) < 0)

// MessageBoxA's answers.
#define IDOK 1
#define IDCANCEL 2
#define IDABORT 3
#define IDRETRY 4
#define IDIGNORE 5
#define IDYES 6
#define IDNO 7

#define LOWORD(Value) ((WORD)(((DWORD_PTR)(Value)) & 0xFFFF))
#define HIWORD(Value) ((WORD)((((DWORD_PTR)(Value)) >> 16) & 0xFFFF))
#define LOBYTE(Value) ((BYTE)(((DWORD_PTR)(Value)) & 0xFF))
#define HIBYTE(Value) ((BYTE)((((DWORD_PTR)(Value)) >> 8) & 0xFF))
#define MAKEWORD(Low, High)                                                                        \
    ((WORD)(((BYTE)((DWORD_PTR)(Low) & 0xFF)) | ((WORD)((BYTE)((DWORD_PTR)(High) & 0xFF))) << 8))
#define MAKELONG(Low, High)                                                                        \
    ((LONG)(((WORD)((DWORD_PTR)(Low) & 0xFFFF)) | ((DWORD)((WORD)((DWORD_PTR)(High) & 0xFFFF)))    \
                                                      << 16))

// The wait results (WaitForSingleObject, MsgWaitForMultipleObjects).
#define WAIT_OBJECT_0 0
#define WAIT_ABANDONED 0x80
#define WAIT_TIMEOUT 0x102
#define WAIT_FAILED ((DWORD)0xFFFFFFFF)

#include "posix/kernel32.h" // the file, mapping and memory calls (uses the types above)
#include "posix/wingdi.h"  // GDI's text types and calls, which fail here (fonts come with M3)
#include "posix/winreg.h"  // the registry: every key absent
#include "posix/unknwn.h"   // COM's basics: GUID, IUnknown, the HRESULTs, CoCreateInstance
#include "posix/winuser.h" // user32: windows, messages, the cursor and keyboard (src/PosixUser32)
// The SDK's <windows.h> brings in winsock.h unless WIN32_LEAN_AND_MEAN: its address calls
// (inet_addr, inet_ntoa, htons...) are the C library's here.
#if !defined(WIN32_LEAN_AND_MEAN)
#include <arpa/inet.h>
#endif

// ---------------------------------------------------------------------------------------------
// Small Win32 calls
// ---------------------------------------------------------------------------------------------

// The debugger break: SIGTRAP, which a debugger stops on and which otherwise ends the process,
// as an unhandled breakpoint does on Windows.
#if defined(__clang__)
#define __debugbreak() __builtin_debugtrap()
#else
#define __debugbreak() ((void)std::raise(SIGTRAP))
#endif

// The debugger's output window: stderr here.
inline void OutputDebugStringA(LPCSTR Text)
{
    if (Text != nullptr)
        std::fputs(Text, stderr);
}

// wsprintfA: user32's sprintf, which writes at most 1024 characters with the terminator (and
// has no floating-point conversions, which its callers do not use). The length written.
inline int wsprintfA(LPSTR Buffer, LPCSTR Format, ...)
{
    va_list Args;
    va_start(Args, Format);
    int const Length = std::vsnprintf(Buffer, 1024, Format, Args);
    va_end(Args);
    return Length < 0 ? 0 : (Length > 1023 ? 1023 : Length);
}

// SetRect / SetRectEmpty / IsRectEmpty.
inline BOOL SetRect(LPRECT Rect, int Left, int Top, int Right, int Bottom)
{
    if (Rect == nullptr)
        return FALSE;
    Rect->left = Left;
    Rect->top = Top;
    Rect->right = Right;
    Rect->bottom = Bottom;
    return TRUE;
}
inline BOOL SetRectEmpty(LPRECT Rect)
{
    return SetRect(Rect, 0, 0, 0, 0);
}
inline BOOL IsRectEmpty(const RECT* Rect)
{
    return Rect == nullptr || Rect->right <= Rect->left || Rect->bottom <= Rect->top;
}

// MessageBoxA's type flags and FormatMessageA (the message tables are Windows'; the text here
// is the C library's strerror for an errno-style code).
#define MB_OK 0x00000000L
#define MB_OKCANCEL 0x00000001L
#define MB_ABORTRETRYIGNORE 0x00000002L
#define MB_YESNOCANCEL 0x00000003L
#define MB_YESNO 0x00000004L
#define MB_RETRYCANCEL 0x00000005L
#define MB_ICONHAND 0x00000010L
#define MB_ICONERROR MB_ICONHAND
#define MB_ICONQUESTION 0x00000020L
#define MB_ICONEXCLAMATION 0x00000030L
#define MB_ICONWARNING MB_ICONEXCLAMATION
#define MB_ICONASTERISK 0x00000040L
#define MB_ICONINFORMATION MB_ICONASTERISK
#define MB_TASKMODAL 0x00002000L
#define MB_TOPMOST 0x00040000L
#define LANG_NEUTRAL 0x00
#define LANG_ENGLISH 0x09
#define LANG_CHINESE 0x04
#define LANG_JAPANESE 0x11
#define LANG_KOREAN 0x12
#define SUBLANG_NEUTRAL 0x00
#define SUBLANG_DEFAULT 0x01
#define MAKELANGID(p, s) ((((WORD)(s)) << 10) | (WORD)(p))
#define PRIMARYLANGID(LangId) ((WORD)(LangId) & 0x3ff)
#define SUBLANGID(LangId) ((WORD)(LangId) >> 10)
#define FORMAT_MESSAGE_ALLOCATE_BUFFER 0x00000100
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x00000200
#define FORMAT_MESSAGE_FROM_STRING 0x00000400
#define FORMAT_MESSAGE_FROM_SYSTEM 0x00001000

typedef HANDLE HLOCAL;
inline HLOCAL LocalFree(HLOCAL Memory)
{
    std::free(Memory);
    return nullptr;
}
// FormatMessageA(FROM_SYSTEM): "Error <code>." (with ALLOCATE_BUFFER, *(char**)Buffer gets a
// malloc'd copy for LocalFree). The length written, or 0.
inline DWORD FormatMessageA(DWORD Flags, LPCVOID, DWORD Code, DWORD, LPSTR Buffer, DWORD Size,
                            void*)
{
    char Text[64];
    const int Length = std::snprintf(Text, sizeof(Text), "Error %u.\r\n", (unsigned)Code);
    if (Length <= 0 || Buffer == nullptr)
        return 0;
    if ((Flags & FORMAT_MESSAGE_ALLOCATE_BUFFER) != 0)
    {
        char* const Copy = static_cast<char*>(std::malloc((size_t)Length + 1));
        if (Copy == nullptr)
            return 0;
        std::memcpy(Copy, Text, (size_t)Length + 1);
        *reinterpret_cast<char**>(Buffer) = Copy;
        return (DWORD)Length;
    }
    if ((DWORD)Length >= Size)
        return 0;
    std::memcpy(Buffer, Text, (size_t)Length + 1);
    return (DWORD)Length;
}

// Thread attach and detach calls are never made here, so there is nothing to turn off.
inline BOOL DisableThreadLibraryCalls(HMODULE)
{
    return TRUE;
}

// SearchPathA with a Path list (';'-separated folders): the first folder that holds FileName,
// without regard to case. A module's Windows name (system.dll) also finds the module this
// platform builds (system.dylib / system.so), whose path is then the answer, for LoadLibraryA
// (Platform::ModuleLoad) to load. FilePart may be nullptr. The length written, or the size
// needed when Buffer is too small, or 0 when it is not found. A null Path (the system's search
// order) looks in the current folder only. Extension is not supported (callers pass nullptr).
inline DWORD SearchPathA(LPCSTR Path, LPCSTR FileName, LPCSTR Extension, DWORD BufferLength,
                         LPSTR Buffer, LPSTR* FilePart)
{
    (void)Extension;
    if (FileName == nullptr)
        return 0;
    std::string const Folders = Path != nullptr ? Path : ".";
    std::size_t Start = 0;
    while (Start <= Folders.size())
    {
        std::size_t End = Folders.find(';', Start);
        if (End == std::string::npos)
            End = Folders.size();
        std::string Candidate = Folders.substr(Start, End - Start);
        Start = End + 1;
        if (Candidate.empty())
            continue;
        if (Candidate.back() != '\\' && Candidate.back() != '/')
            Candidate += '\\';
        Candidate += FileName;
        auto IsFile = [](std::string const& Name)
        {
            DWORD const Attributes = Platform::PathAttributes(Name.c_str());
            return Attributes != Platform::kInvalidAttributes &&
                   (Attributes & Platform::kAttributeDirectory) == 0;
        };
        if (!IsFile(Candidate))
        {
            std::size_t const Length = Candidate.size();
            if (Length <= 4 || strcasecmp(Candidate.c_str() + Length - 4, ".dll") != 0)
                continue;
#if defined(__APPLE__)
            Candidate.replace(Length - 4, 4, ".dylib");
#else
            Candidate.replace(Length - 4, 4, ".so");
#endif
            if (!IsFile(Candidate))
                continue;
        }
        // The file's own spelling on disk (the search is without regard to case), so that the
        // path works on a case-sensitive file system too.
        Candidate = Platform::Detail::ResolvePath(Candidate.c_str());
        char Full[4096];
        char* Name = nullptr;
        DWORD const Length = Platform::FullPath(Candidate.c_str(), Full, sizeof(Full), &Name);
        if (Length == 0 || Length >= sizeof(Full))
            return 0;
        if (Length >= BufferLength || Buffer == nullptr)
            return Length + 1;
        std::memcpy(Buffer, Full, Length + 1);
        if (FilePart != nullptr)
            *FilePart = Name != nullptr ? Buffer + (Name - Full) : nullptr;
        return Length;
    }
    return 0;
}

// The version resource. A module here is a .dylib or .so without one (reading the retail
// resource DLLs comes with M3), so there is no version to report: GetFileVersionInfoSizeA
// returns 0, as Windows does for a file without a version resource.
struct VS_FIXEDFILEINFO
{
    DWORD dwSignature;
    DWORD dwStrucVersion;
    DWORD dwFileVersionMS;
    DWORD dwFileVersionLS;
    DWORD dwProductVersionMS;
    DWORD dwProductVersionLS;
    DWORD dwFileFlagsMask;
    DWORD dwFileFlags;
    DWORD dwFileOS;
    DWORD dwFileType;
    DWORD dwFileSubtype;
    DWORD dwFileDateMS;
    DWORD dwFileDateLS;
};
inline DWORD GetFileVersionInfoSizeA(LPCSTR, LPDWORD Handle)
{
    if (Handle != nullptr)
        *Handle = 0;
    return 0;
}
inline BOOL GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID)
{
    return FALSE;
}
inline BOOL VerQueryValueA(LPCVOID, LPCSTR, LPVOID*, UINT*)
{
    return FALSE;
}

// ---------------------------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------------------------

namespace PosixWin32
{
// The module that holds Address (a borrowed handle, as GetModuleHandleA's).
inline HMODULE ModuleOf(void const* Address)
{
    Dl_info Info;
    if (dladdr(Address, &Info) == 0 || Info.dli_fname == nullptr)
        return nullptr;
    return Platform::ModuleFind(Info.dli_fname);
}
}

// Runs Entry (a module's DllMain) with DLL_PROCESS_ATTACH when the module loads and
// DLL_PROCESS_DETACH when it unloads, as Windows does. Put it after DllMain, in the same source
// file: it is a static object, so it runs after that file's other statics are constructed (the
// CRT does the same before DllMain on Windows). The loader initialises a module's dependencies
// first, as Windows does. A module's other source files must not depend on DllMain having run
// during their own static initialisation.
#define FL_POSIX_DLLMAIN(Entry)                                                                    \
    namespace                                                                                      \
    {                                                                                              \
    struct FPosixDllMain                                                                           \
    {                                                                                              \
        HMODULE Module = PosixWin32::ModuleOf((void const*)&Entry);                                \
        FPosixDllMain()                                                                            \
        {                                                                                          \
            Entry(Module, DLL_PROCESS_ATTACH, nullptr);                                            \
        }                                                                                          \
        ~FPosixDllMain()                                                                           \
        {                                                                                          \
            Entry(Module, DLL_PROCESS_DETACH, nullptr);                                            \
        }                                                                                          \
    } GPosixDllMain;                                                                               \
    }

// GetVersionExA: Windows' version numbers for a current Windows (10.0), so code that asks for
// "newer than 4" takes the modern path.
typedef struct _OSVERSIONINFOA
{
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    CHAR szCSDVersion[128];
} OSVERSIONINFOA;
#define VER_PLATFORM_WIN32_NT 2
inline BOOL GetVersionExA(OSVERSIONINFOA* Info)
{
    if (Info == nullptr)
        return FALSE;
    Info->dwMajorVersion = 10;
    Info->dwMinorVersion = 0;
    Info->dwBuildNumber = 19041;
    Info->dwPlatformId = VER_PLATFORM_WIN32_NT;
    Info->szCSDVersion[0] = 0;
    return TRUE;
}

// FileTimeToSystemTime (UTC), and GetTimeFormatA / GetDateFormatA for the flags the code passes:
// TIME_NOSECONDS / TIME_NOTIMEMARKER / TIME_FORCE24HOURFORMAT, DATE_SHORTDATE (M/d/yyyy, the
// US system locale's) or DATE_LONGDATE. The format string argument is not supported (nullptr).
#define TIME_NOMINUTESORSECONDS 0x0001
#define TIME_NOSECONDS 0x0002
#define TIME_NOTIMEMARKER 0x0004
#define TIME_FORCE24HOURFORMAT 0x0008
#define DATE_SHORTDATE 0x0001
#define DATE_LONGDATE 0x0002
#define LOCALE_SYSTEM_DEFAULT 0x0800
#define LOCALE_USER_DEFAULT 0x0400
inline BOOL FileTimeToSystemTime(const FILETIME* Time, SYSTEMTIME* Out)
{
    if (Time == nullptr || Out == nullptr)
        return FALSE;
    const unsigned long long Ticks =
        ((unsigned long long)Time->dwHighDateTime << 32) | Time->dwLowDateTime;
    const long long Seconds = (long long)(Ticks / 10000000ULL) - 11644473600LL;
    const time_t Unix = (time_t)Seconds;
    struct tm Parts;
    if (gmtime_r(&Unix, &Parts) == nullptr)
        return FALSE;
    Out->wYear = (WORD)(Parts.tm_year + 1900);
    Out->wMonth = (WORD)(Parts.tm_mon + 1);
    Out->wDayOfWeek = (WORD)Parts.tm_wday;
    Out->wDay = (WORD)Parts.tm_mday;
    Out->wHour = (WORD)Parts.tm_hour;
    Out->wMinute = (WORD)Parts.tm_min;
    Out->wSecond = (WORD)Parts.tm_sec;
    Out->wMilliseconds = (WORD)((Ticks / 10000ULL) % 1000ULL);
    return TRUE;
}
inline int PosixFormatResult(LPSTR Out, int Size, const char* Text)
{
    const int Needed = (int)std::strlen(Text) + 1;
    if (Size == 0)
        return Needed;
    if (Out == nullptr || Size < Needed)
        return 0;
    std::memcpy(Out, Text, (size_t)Needed);
    return Needed;
}
inline int GetTimeFormatA(DWORD, DWORD Flags, const SYSTEMTIME* Time, LPCSTR, LPSTR Out, int Size)
{
    if (Time == nullptr)
        return 0;
    char Text[32];
    const bool b24 = (Flags & TIME_FORCE24HOURFORMAT) != 0;
    const unsigned Hour = b24 ? Time->wHour : (Time->wHour % 12 == 0 ? 12u : Time->wHour % 12u);
    const char* Marker =
        (Flags & TIME_NOTIMEMARKER) != 0 || b24 ? "" : (Time->wHour < 12 ? " AM" : " PM");
    if ((Flags & TIME_NOMINUTESORSECONDS) != 0)
        std::snprintf(Text, sizeof(Text), "%u%s", Hour, Marker);
    else if ((Flags & TIME_NOSECONDS) != 0)
        std::snprintf(Text, sizeof(Text), b24 ? "%02u:%02u%s" : "%u:%02u%s", Hour,
                      (unsigned)Time->wMinute, Marker);
    else
        std::snprintf(Text, sizeof(Text), b24 ? "%02u:%02u:%02u%s" : "%u:%02u:%02u%s", Hour,
                      (unsigned)Time->wMinute, (unsigned)Time->wSecond, Marker);
    return PosixFormatResult(Out, Size, Text);
}
inline int GetDateFormatA(DWORD, DWORD Flags, const SYSTEMTIME* Date, LPCSTR, LPSTR Out, int Size)
{
    if (Date == nullptr)
        return 0;
    static const char* const kDays[] = {"Sunday",   "Monday", "Tuesday", "Wednesday",
                                        "Thursday", "Friday", "Saturday"};
    static const char* const kMonths[] = {"January",   "February", "March",    "April",
                                          "May",       "June",     "July",     "August",
                                          "September", "October",  "November", "December"};
    char Text[64];
    if ((Flags & DATE_LONGDATE) != 0 && Date->wMonth >= 1 && Date->wMonth <= 12 &&
        Date->wDayOfWeek < 7)
        std::snprintf(Text, sizeof(Text), "%s, %s %u, %u", kDays[Date->wDayOfWeek],
                      kMonths[Date->wMonth - 1], (unsigned)Date->wDay, (unsigned)Date->wYear);
    else
        std::snprintf(Text, sizeof(Text), "%u/%u/%u", (unsigned)Date->wMonth, (unsigned)Date->wDay,
                      (unsigned)Date->wYear);
    return PosixFormatResult(Out, Size, Text);
}
