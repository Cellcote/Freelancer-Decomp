// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): loads the client modules the non-Windows build makes (src/PortTests/CMakeLists.txt)
// the way the game does, by their Windows names, and checks that their DllMains ran: dacom's
// COManager exists, and the components the other modules register at load are there. The exit
// code is the number of failures.

#include "dacom.h"
#include "platform.h"
#include "readfile.h"

#include <cstdio>
#include <cstring>
#include <string>

#include <stdlib.h>
#include <sys/stat.h>

namespace
{
    int G_Failures = 0;

    void Check(bool bOk, const char* What)
    {
        if (!bOk)
        {
            std::printf("FAILED: %s\n", What);
            ++G_Failures;
        }
    }

    // ReadFile's DOS file system over a scratch folder, as DALib::OpenData makes it: a retail
    // path ('\\', another case) opens, reads, seeks and maps files on this disk.
    void TestReadFile(ICOManager* Manager)
    {
        char Template[] = "/tmp/module_tests_XXXXXX";
        if (mkdtemp(Template) == nullptr)
        {
            Check(false, "scratch folder for ReadFile");
            return;
        }
        std::string const Root = Template;
        mkdir((Root + "/Data").c_str(), 0755);
        if (FILE* const File = std::fopen((Root + "/Data/Ships.INI").c_str(), "wb"))
        {
            std::fputs("[Ship]\nnickname = li_elite\n", File);
            std::fclose(File);
        }
        std::string const Retail = Root + "\\DATA\\ships.ini";

        FFileSystemDesc Desc = {};
        Desc.Size = 0x34;
        Desc.ComponentName = "FileSystem";
        Desc.Implementation = "DOS";
        Desc.Path = Root.c_str();
        Desc.Access = 0x80000000; // GENERIC_READ
        Desc.ShareMode = 1;       // FILE_SHARE_READ
        Desc.Creation = 3;        // OPEN_EXISTING
        Desc.Flags = 0x08000080;
        Desc.Field30 = 0xFFFFFFFF;
        IFileSystem* FileSystem = nullptr;
        Check(Manager->CreateInstance((AGGDESC*)(&Desc), (void**)(&FileSystem)) == GR_OK && FileSystem != nullptr,
              "ReadFile makes a DOS file system");
        if (FileSystem == nullptr)
            return;

        FFileSystemDesc Open = Desc;
        Open.Path = Retail.c_str();
        int const File = FileSystem->OpenByDesc(&Open);
        Check(File != -1, "OpenByDesc opens a retail path");
        if (File != -1)
        {
            FlULong const Handle = static_cast<FlULong>(File);
            Check(FileSystem->GetSize(Handle, nullptr) == 27, "GetSize");
            char Buffer[64] = {};
            FlULong Read = 0;
            Check(FileSystem->Read(Handle, Buffer, 6, &Read, 0) != 0 && Read == 6 && std::memcmp(Buffer, "[Ship]", 6) == 0,
                  "Read");
            FlULong High = 0;
            FileSystem->SeekFile(Handle, 7, &High, 0);
            // A short read returns 0 (as retail) with what it read.
            Check(FileSystem->Read(Handle, Buffer, sizeof(Buffer), &Read, 0) == 0 && Read == 20 &&
                      std::memcmp(Buffer, "nickname", 8) == 0,
                  "SeekFile, then a short Read to the end");
            int const Mapping = FileSystem->CreateMapping(Handle, 0, 2, 0, 0, 0);
            Check(Mapping != -1, "CreateMapping");
            if (Mapping != -1)
            {
                std::intptr_t const View = FileSystem->MapFileView(static_cast<FlULong>(Mapping), 4, 0, 0, 0);
                Check(View != 0 && std::memcmp(reinterpret_cast<const void*>(View), "[Ship]\n", 7) == 0,
                      "MapFileView sees the file");
                FileSystem->UnmapFileView(0);
                FileSystem->CloseFile(static_cast<FlULong>(Mapping));
            }
            FILETIME Write = {};
            Check(FileSystem->GetFileTimes(Handle, nullptr, nullptr, &Write) != 0 && Write.dwHighDateTime != 0,
                  "GetFileTimes");
            Check(FileSystem->CloseFile(Handle) != 0, "CloseFile");
        }
        std::string const Missing = Root + "\\Data\\missing.ini";
        Open.Path = Missing.c_str();
        Check(FileSystem->OpenByDesc(&Open) == -1, "a missing file does not open");

        // Not FindFirst: the DOS file system's FindFirst takes JoinRoot's success for a failure
        // (ReadFile.cpp, slot 0x48), on Windows as here, so it finds nothing; to be checked
        // against retail. Platform::FindFirst itself is in port_tests.
        FileSystem->Release();
        std::system(("rm -rf '" + Root + "'").c_str());
    }

    int __cdecl CountComponent(IDAComponent* Factory, const char* Name, void*, void* Context)
    {
        if (Factory != nullptr && Name != nullptr)
            ++*static_cast<int*>(Context);
        return 1; // carry on
    }
}

int main(int argc, char** argv)
{
    // The modules are next to this test (its folder is the first argument).
    std::string const Folder = argc > 1 ? std::string(argv[1]) + "/" : std::string("./");

    // A Windows name, in another case than the file on disk: the game's own spelling.
    Platform::Module const System = Platform::ModuleLoad((Folder + "SYSTEM.DLL").c_str());
    Check(System != nullptr, "system.dll loads (and dacom.dll with it)");
    Platform::Module const Math = Platform::ModuleLoad((Folder + "x86math.dll").c_str());
    Check(Math != nullptr, "x86math.dll loads");
    Platform::Module const Files = Platform::ModuleLoad((Folder + "ReadFile.dll").c_str());
    Check(Files != nullptr, "ReadFile.dll loads");

    Platform::Module const Dacom = Platform::ModuleFind((Folder + "dacom.dylib").c_str()) != nullptr
                                       ? Platform::ModuleFind((Folder + "dacom.dylib").c_str())
                                       : Platform::ModuleFind((Folder + "dacom.so").c_str());
    Check(Dacom != nullptr, "dacom is loaded as system's dependency");
    auto const Acquire = reinterpret_cast<ICOManager* (*)()>(Platform::ModuleSymbol(Dacom, "DACOM_Acquire"));
    Check(Acquire != nullptr, "dacom exports DACOM_Acquire");
    ICOManager* const Manager = Acquire != nullptr ? Acquire() : nullptr;
    Check(Manager != nullptr, "DACOM_Acquire returns the COManager (made by dacom's DllMain)");
    if (Manager != nullptr)
    {
        int Count = 0;
        Manager->EnumerateComponents("SystemContainer", &CountComponent, &Count);
        Check(Count == 1, "system's DllMain registered SystemContainer");
        Count = 0;
        Manager->EnumerateComponents("FileSystem", &CountComponent, &Count);
        Check(Count == 3, "ReadFile's DllMain registered its three file systems (DOS, UTF, MEM)");
        if (Files != nullptr)
            TestReadFile(Manager);
        Manager->Release();
    }

    auto const Stricmp = reinterpret_cast<int (*)(const char*, const char*)>(Platform::ModuleSymbol(Dacom, "stricmp"));
    Check(Stricmp != nullptr && Stricmp("Freelancer", "FREELANCER") == 0, "dacom exports stricmp");

    // SearchPathA finds a module by its Windows name (DACOM's AddLibrary looks it up so).
    char Found[1024] = {};
    char* FilePart = nullptr;
    DWORD const FoundLength = SearchPathA(Folder.c_str(), "SYSTEM.DLL", nullptr, sizeof(Found), Found, &FilePart);
    Check(FoundLength > 0 && FilePart != nullptr && Platform::ModuleFind(Found) != nullptr,
          "SearchPathA finds system.dll as the module this platform built");
    Check(SearchPathA(Folder.c_str(), "nothing.dll", nullptr, sizeof(Found), Found, nullptr) == 0,
          "SearchPathA: a missing module");

    // The MSVC CRT stand-ins (posix/msvc_crt.h) the modules use.
    char Drive[_MAX_DRIVE], Directory[_MAX_DIR], Name[_MAX_FNAME], Extension[_MAX_EXT];
    _splitpath("C:\\Freelancer\\EXE\\dacom.dll", Drive, Directory, Name, Extension);
    Check(std::strcmp(Drive, "C:") == 0 && std::strcmp(Directory, "\\Freelancer\\EXE\\") == 0 &&
              std::strcmp(Name, "dacom") == 0 && std::strcmp(Extension, ".dll") == 0,
          "_splitpath");
    _splitpath("a.b/file", nullptr, Directory, Name, Extension);
    Check(std::strcmp(Directory, "a.b/") == 0 && std::strcmp(Name, "file") == 0 && Extension[0] == 0,
          "_splitpath: a '.' in a folder is no extension");
    char Path[_MAX_PATH];
    _makepath(Path, "C", "\\data", "ships", "ini");
    Check(std::strcmp(Path, "C:\\data\\ships.ini") == 0, "_makepath");
    char Buffer[4] = {'x', 'x', 'x', 'x'};
    Check(_snprintf(Buffer, 4, "%d", 12) == 2 && std::strcmp(Buffer, "12") == 0, "_snprintf that fits");
    Check(_snprintf(Buffer, 4, "%d", 1234) == 4 && std::memcmp(Buffer, "1234", 4) == 0, "_snprintf that fills: no terminator");
    Check(_snprintf(Buffer, 4, "%d", 123456) == -1 && std::memcmp(Buffer, "1234", 4) == 0, "_snprintf that overflows: -1");
    Check(_stricmp("abc_", "ABCD") < 0 && _strnicmp("FreeX", "freeY", 4) == 0, "_stricmp lowers both sides (so _ sorts before letters)");

    // The kernel32 stand-ins (posix/kernel32.h).
    FILETIME Epoch = {};
    Check(DosDateTimeToFileTime(0x21, 0, &Epoch) && Epoch.dwHighDateTime == 0x01A8E79Fu && Epoch.dwLowDateTime == 0xE1D58000u,
          "DosDateTimeToFileTime: 1980-01-01");
    Check(!DosDateTimeToFileTime(0x20, 0, &Epoch), "DosDateTimeToFileTime refuses day 0");
    Check(CreateFileA("/nonexistent/x", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) == INVALID_HANDLE_VALUE &&
              GetLastError() == ERROR_FILE_NOT_FOUND,
          "CreateFileA: a missing file sets ERROR_FILE_NOT_FOUND");
    void* const Pages = VirtualAlloc(nullptr, 0x3000, MEM_COMMIT, PAGE_READWRITE);
    Check(Pages != nullptr && static_cast<unsigned char*>(Pages)[0x2FFF] == 0 && VirtualFree(Pages, 0, MEM_RELEASE) &&
              !VirtualFree(Pages, 0, MEM_RELEASE),
          "VirtualAlloc / VirtualFree");

    if (G_Failures == 0)
        std::printf("module_tests: all passed\n");
    return G_Failures;
}
