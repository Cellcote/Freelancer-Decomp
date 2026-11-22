// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): <shlobj.h> on macOS and Linux (src/include/posix, non-Windows only):
// SHGetFolderPathA for the folders the game keeps files in. "My Documents" (CSIDL_PERSONAL) is
// ~/Documents; the local and roaming application-data folders are ~/Library/Application Support
// on macOS and $XDG_DATA_HOME (or ~/.local/share) elsewhere. Paths use '/'.

#pragma once

#include <windows.h>

#include <cerrno>
#include <cstdlib>
#include <string>

#include <sys/stat.h>

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
        // The XDG spec says a relative XDG_DATA_HOME is invalid and must be ignored.
        if (const char* Data = std::getenv("XDG_DATA_HOME"); Data != nullptr && Data[0] == '/')
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
    if ((Folder & CSIDL_FLAG_CREATE) != 0)
    {
        // Create the folder and any missing parents, as Windows does for CSIDL_FLAG_CREATE.
        for (std::size_t Slash = Result.find('/', 1);; Slash = Result.find('/', Slash + 1))
        {
            const std::string Part = Slash == std::string::npos ? Result : Result.substr(0, Slash);
            struct stat Info;
            if (stat(Part.c_str(), &Info) != 0)
            {
                if (mkdir(Part.c_str(), 0755) != 0 && errno != EEXIST)
                    return E_FAIL;
                if (stat(Part.c_str(), &Info) != 0)
                    return E_FAIL;
            }
            if (!S_ISDIR(Info.st_mode))
                return E_FAIL;
            if (Slash == std::string::npos)
                break;
        }
    }
    std::memcpy(Path, Result.c_str(), Result.size() + 1);
    return S_OK;
}
