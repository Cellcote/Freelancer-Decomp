// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): the registry calls on macOS and Linux (src/include/posix, non-Windows only;
// posix/windows.h includes it). There is no registry here yet: every key is absent, so opening
// one fails with ERROR_FILE_NOT_FOUND and the callers take the path they take on a machine
// without the key (defaults, the UuidCreate fallback). A settings file can stand in later.

#pragma once

#if defined(_WIN32)
#error "posix/winreg.h is for macOS and Linux"
#endif

typedef HKEY* PHKEY;
typedef DWORD REGSAM;
typedef LONG LSTATUS;

#define HKEY_CLASSES_ROOT ((HKEY)(ULONG_PTR)((LONG)0x80000000))
#define HKEY_CURRENT_USER ((HKEY)(ULONG_PTR)((LONG)0x80000001))
#define HKEY_LOCAL_MACHINE ((HKEY)(ULONG_PTR)((LONG)0x80000002))
#define HKEY_USERS ((HKEY)(ULONG_PTR)((LONG)0x80000003))
#define KEY_QUERY_VALUE 0x0001
#define KEY_SET_VALUE 0x0002
#define KEY_CREATE_SUB_KEY 0x0004
#define KEY_ENUMERATE_SUB_KEYS 0x0008
#define KEY_READ 0x20019
#define KEY_WRITE 0x20006
#define KEY_ALL_ACCESS 0xF003F
#define REG_NONE 0
#define REG_SZ 1
#define REG_EXPAND_SZ 2
#define REG_BINARY 3
#define REG_DWORD 4
#define REG_OPTION_NON_VOLATILE 0
#define REG_CREATED_NEW_KEY 1

inline LSTATUS RegOpenKeyExA(HKEY, LPCSTR, DWORD, REGSAM, PHKEY Result)
{
    if (Result != nullptr)
        *Result = nullptr;
    return ERROR_FILE_NOT_FOUND;
}
inline LSTATUS RegOpenKeyA(HKEY Key, LPCSTR SubKey, PHKEY Result)
{
    return RegOpenKeyExA(Key, SubKey, 0, KEY_READ, Result);
}
inline LSTATUS RegCreateKeyExA(HKEY, LPCSTR, DWORD, LPSTR, DWORD, REGSAM, LPSECURITY_ATTRIBUTES,
                               PHKEY Result, LPDWORD Disposition)
{
    if (Result != nullptr)
        *Result = nullptr;
    if (Disposition != nullptr)
        *Disposition = 0;
    return ERROR_ACCESS_DENIED;
}
inline LSTATUS RegQueryValueExA(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD)
{
    return ERROR_FILE_NOT_FOUND;
}
inline LSTATUS RegSetValueExA(HKEY, LPCSTR, DWORD, DWORD, const BYTE*, DWORD)
{
    return ERROR_INVALID_HANDLE;
}
inline LSTATUS RegCloseKey(HKEY)
{
    return ERROR_SUCCESS;
}
inline LSTATUS RegDeleteValueA(HKEY, LPCSTR)
{
    return ERROR_FILE_NOT_FOUND;
}
inline LSTATUS RegDeleteKeyA(HKEY, LPCSTR)
{
    return ERROR_FILE_NOT_FOUND;
}

// GetUserNameA: the login name ($USER, else "player"). *Size counts the terminator, as on
// Windows; too small a buffer fails with ERROR_INSUFFICIENT_BUFFER and the size needed.
#ifndef ERROR_INSUFFICIENT_BUFFER
#define ERROR_INSUFFICIENT_BUFFER 122
#endif
inline BOOL GetUserNameA(LPSTR Buffer, LPDWORD Size)
{
    const char* Name = std::getenv("USER");
    if (Name == nullptr || Name[0] == 0)
        Name = "player";
    const DWORD Needed = (DWORD)std::strlen(Name) + 1;
    if (Size == nullptr || Buffer == nullptr || *Size < Needed)
    {
        if (Size != nullptr)
            *Size = Needed;
        PosixWin32::LastError() = ERROR_INSUFFICIENT_BUFFER;
        return FALSE;
    }
    std::memcpy(Buffer, Name, Needed);
    *Size = Needed;
    return TRUE;
}
