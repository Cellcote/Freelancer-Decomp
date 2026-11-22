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
// - FL_POSIX_DLLMAIN(DllMain): runs a module's DllMain as it loads and unloads (see below).

#pragma once

#if defined(_WIN32)
#error "posix/windows.h is the stand-in for macOS and Linux; Windows uses the SDK's windows.h"
#endif

#include "platform.h"
#include "posix/msvc_crt.h"

#include <climits>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
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
typedef long long LONGLONG;
typedef unsigned long long ULONGLONG;
typedef unsigned long long DWORD64;
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

struct RECT
{
    LONG left;
    LONG top;
    LONG right;
    LONG bottom;
};
struct POINT
{
    LONG x;
    LONG y;
};
typedef RECT* LPRECT;
typedef POINT* LPPOINT;

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
#define MAKELONG(Low, High) ((LONG)(((WORD)((DWORD_PTR)(Low) & 0xFFFF)) | ((DWORD)((WORD)((DWORD_PTR)(High) & 0xFFFF))) << 16))

#include "posix/kernel32.h" // the file, mapping and memory calls (uses the types above)

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

// There is no message box yet (the window comes with SDL, M3): the text goes to stderr, and the
// answer is the one that carries on (IDIGNORE for abort/retry/ignore, IDOK or IDYES otherwise).
inline int MessageBoxA(HWND, LPCSTR Text, LPCSTR Caption, UINT Type)
{
    std::fprintf(stderr, "[%s] %s\n", Caption != nullptr ? Caption : "", Text != nullptr ? Text : "");
    switch (Type & 0x0F)
    {
    case 0x02: // MB_ABORTRETRYIGNORE
        return IDIGNORE;
    case 0x03: // MB_YESNOCANCEL
    case 0x04: // MB_YESNO
        return IDYES;
    default:
        return IDOK;
    }
}

// No message loop yet (M3): nothing to post to.
inline void PostQuitMessage(int) {}

// Thread attach and detach calls are never made here, so there is nothing to turn off.
inline BOOL DisableThreadLibraryCalls(HMODULE) { return TRUE; }

// SearchPathA with a Path list (';'-separated folders): the first folder that holds FileName,
// without regard to case. A module's Windows name (system.dll) also finds the module this
// platform builds (system.dylib / system.so), whose path is then the answer, for LoadLibraryA
// (Platform::ModuleLoad) to load. FilePart may be nullptr. The length written, or the size
// needed when Buffer is too small, or 0 when it is not found. A null Path (the system's search
// order) looks in the current folder only. Extension is not supported (callers pass nullptr).
inline DWORD SearchPathA(LPCSTR Path, LPCSTR FileName, LPCSTR Extension, DWORD BufferLength, LPSTR Buffer,
                         LPSTR* FilePart)
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
        auto IsFile = [](std::string const& Name) {
            DWORD const Attributes = Platform::PathAttributes(Name.c_str());
            return Attributes != Platform::kInvalidAttributes && (Attributes & Platform::kAttributeDirectory) == 0;
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
inline BOOL GetFileVersionInfoA(LPCSTR, DWORD, DWORD, LPVOID) { return FALSE; }
inline BOOL VerQueryValueA(LPCVOID, LPCSTR, LPVOID*, UINT*) { return FALSE; }

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
#define FL_POSIX_DLLMAIN(Entry)                                                                     \
    namespace                                                                                       \
    {                                                                                               \
        struct FPosixDllMain                                                                        \
        {                                                                                           \
            HMODULE Module = PosixWin32::ModuleOf((void const*)&Entry);                             \
            FPosixDllMain() { Entry(Module, DLL_PROCESS_ATTACH, nullptr); }                         \
            ~FPosixDllMain() { Entry(Module, DLL_PROCESS_DETACH, nullptr); }                        \
        } GPosixDllMain;                                                                            \
    }
