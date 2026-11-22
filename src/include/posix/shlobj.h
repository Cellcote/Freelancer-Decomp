// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): <shlobj.h> on macOS and Linux (src/include/posix, non-Windows only):
// SHGetFolderPathA for the folders the game keeps files in. "My Documents" (CSIDL_PERSONAL) is
// ~/Documents; the local and roaming application-data folders are ~/Library/Application Support
// on macOS and $XDG_DATA_HOME (or ~/.local/share) elsewhere. Paths use '/'.

#pragma once

#include <windows.h>

#include <cstdlib>
#include <string>

typedef LONG HRESULT_SH;
#define CSIDL_APPDATA 0x001a
#define CSIDL_LOCAL_APPDATA 0x001c
#define CSIDL_PERSONAL 0x0005
#define CSIDL_MYPICTURES 0x0027
#define CSIDL_MYDOCUMENTS CSIDL_PERSONAL
#define CSIDL_FLAG_CREATE 0x8000
#define SHGFP_TYPE_CURRENT 0

inline HRESULT SHGetFolderPathA(HWND, int Folder, HANDLE, DWORD, LPSTR Path)
{
    const char* Home = std::getenv("HOME");
    if (Home == nullptr || Path == nullptr)
        return E_FAIL;
    std::string Result = Home;
    switch (Folder & 0xFF)
    {
    case CSIDL_PERSONAL:
        Result += "/Documents";
        break;
    case CSIDL_MYPICTURES:
        Result += "/Pictures";
        break;
    case CSIDL_APPDATA:
    case CSIDL_LOCAL_APPDATA:
#if defined(__APPLE__)
        Result += "/Library/Application Support";
#else
        if (const char* Data = std::getenv("XDG_DATA_HOME"); Data != nullptr && Data[0] != 0)
            Result = Data;
        else
            Result += "/.local/share";
#endif
        break;
    default:
        return E_FAIL;
    }
    if (Result.size() >= MAX_PATH)
        return E_FAIL;
    std::memcpy(Path, Result.c_str(), Result.size() + 1);
    return S_OK;
}
