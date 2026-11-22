// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): loads the client modules the non-Windows build makes (src/PortTest/CMakeLists.txt)
// the way the game does, by their Windows names, and checks that their DllMains ran: dacom's
// COManager exists, and the components the other modules register at load are there. The exit
// code is the number of failures.

#include "dacom.h"
#include "dalib_net.h"
#include "engine/engbase.h"
#include "platform.h"
#include "readfile.h"
#include "render/shading.h"

#include <intrin.h>
#include <xmmintrin.h>

#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>

#include <netinet/in.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

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
    if (FILE* const File = fopen((Root + "/Data/Ships.INI").c_str(), "wb"))
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
    Check(Manager->CreateInstance((AGGDESC*)(&Desc), (void**)(&FileSystem)) == GR_OK &&
              FileSystem != nullptr,
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
        Check(FileSystem->Read(Handle, Buffer, 6, &Read, 0) != 0 && Read == 6 &&
                  std::memcmp(Buffer, "[Ship]", 6) == 0,
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
            std::intptr_t const View =
                FileSystem->MapFileView(static_cast<FlULong>(Mapping), 4, 0, 0, 0);
            Check(View != 0 && std::memcmp(reinterpret_cast<const void*>(View), "[Ship]\n", 7) == 0,
                  "MapFileView sees the file");
            FileSystem->UnmapFileView(0);
            FileSystem->CloseFile(static_cast<FlULong>(Mapping));
        }
        FILETIME Write = {};
        Check(FileSystem->GetFileTimes(Handle, nullptr, nullptr, &Write) != 0 &&
                  Write.dwHighDateTime != 0,
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

// The string a shading payload names: an id in the process's HandleValue table, held by dacom
// (render/shading.h). The test reads the table through dacom's export, as the modules do.
// bForget drops the id, as ShadingPayloadForget does when the string is freed.
const char* PayloadText(Platform::Module Dacom, FlULong Payload, bool bForget = false)
{
    auto const Shared = reinterpret_cast<HandleValue::Detail::FTable* (*)()>(
        Platform::ModuleSymbol(Dacom, "FlHandleValueShared"));
    if (Shared == nullptr || Payload == 0)
        return nullptr;
    HandleValue::Detail::FTable& Table = *Shared();
    std::lock_guard<std::mutex> Guard(Table.Lock);
    auto const Found = Table.Objects.find(Payload);
    if (Found == Table.Objects.end())
        return nullptr;
    const char* const Text = static_cast<const char*>(Found->second);
    if (bForget)
    {
        Table.Ids.erase(Found->second);
        Table.Objects.erase(Found);
    }
    return Text;
}

// flmaterials' Glass material: its declared parameters are FShadingPropertyRecords whose names
// are pointers and whose texture-name default is a string payload ("(None)").
void TestFlMaterials(ICOManager* Manager, Platform::Module Dacom)
{
    FAGGDESC14 Desc = {sizeof(FAGGDESC14), "GlassMaterial", nullptr, nullptr, nullptr};
    IMaterial* Material = nullptr;
    Check(Manager->CreateInstance((AGGDESC*)(&Desc), (void**)(&Material)) == GR_OK &&
              Material != nullptr,
          "flmaterials makes a GlassMaterial");
    if (Material == nullptr)
        return;
    FlULong Count = 0;
    Check(Material->GetParameterCount(&Count) == GR_OK && Count == 10,
          "GlassMaterial declares ten parameters");
    FShadingPropertyRecord Record = {};
    Check(Material->GetParameterDescriptor(6, &Record) == GR_OK, "GetParameterDescriptor(6)");
    Check(Record.Name != nullptr && std::strcmp(Record.Name, "Dt_name") == 0 &&
              Record.Description != nullptr && std::strcmp(Record.Description, "Texture Name") == 0,
          "the texture-name descriptor's tag and name");
    Check(Record.Current.Type == 7 && Record.Default.Type == 7 && Record.Saved.Type == 7,
          "the texture-name descriptor's values are strings");
    const char* const Text = PayloadText(Dacom, Record.Current.Payload0);
    Check(Text != nullptr && std::strcmp(Text, "(None)") == 0,
          "the texture-name default is \"(None)\", a string payload");
    // The descriptor copy owns its strings (ProcessHeap copies; HeapFree is free here).
    for (FShadingPropertyValue* Value : {&Record.Current, &Record.Default, &Record.Saved})
    {
        std::free(const_cast<char*>(PayloadText(Dacom, Value->Payload0, true)));
        Value->Payload0 = 0;
    }
    Check(Material->GetParameterDescriptor(0, &Record) == GR_OK && Record.Current.Type == 3,
          "the ambient colour descriptor is a three-float value");
    Check(Material->GetParameterDescriptor(10, &Record) != GR_OK,
          "there is no eleventh Glass parameter");
    Material->Release();
}

// EngBase's engine: a compound archetype and an instance of it, which are LowArena blocks
// whose encoded values key the engine's maps (the root-instance map walked here).
void TestEngine(ICOManager* Manager)
{
    AGGDESC Desc = {8, "Engine"};
    IEngine* Engine = nullptr;
    Check(Manager->CreateInstance(&Desc, (void**)(&Engine)) == GR_OK && Engine != nullptr,
          "EngBase makes an Engine");
    if (Engine == nullptr)
        return;
    FEngineName Name = {nullptr, 0}; // no CRC: the engine names it (anonymous compound)
    void* const Archetype = Engine->CreateCompoundArchetype(&Name);
    Check(Archetype != nullptr && Archetype != G_InvalidEngineHandle, "CreateCompoundArchetype");
    bool const bArchetype = Archetype != nullptr && Archetype != G_InvalidEngineHandle;
    void* const Parent = bArchetype ? Engine->CreateInstance(Archetype, nullptr) : nullptr;
    void* const Child = bArchetype ? Engine->CreateInstance(Archetype, nullptr) : nullptr;
    bool const bInstances = Parent != nullptr && Parent != G_InvalidEngineHandle &&
                            Child != nullptr && Child != G_InvalidEngineHandle;
    Check(bInstances, "CreateInstance");
    if (bInstances)
    {
        Check(Engine->GetInstanceArchetype(Parent) == Archetype, "the instance's archetype");
        // A joint makes Parent a root: the root map holds it under its encoded key.
        FlULong const Payload[G_EngineJointPayloadDwords] = {};
        Check(Engine->CreateJoint(Parent, Child, Payload) == 1, "CreateJoint");
        Check(Engine->GetInstanceParent(Child) == Parent, "the child's parent");
        Check(Engine->FindMappedInstance(G_InvalidEngineHandle) == Parent,
              "the root-instance map gives the parent back (its encoded key decodes)");
        Check(Engine->FindMappedInstance(Parent) == G_InvalidEngineHandle, "one root instance");
        Check(Engine->DestroyJoint(Parent, Child) == 1 &&
                  Engine->FindMappedInstance(G_InvalidEngineHandle) == G_InvalidEngineHandle,
              "DestroyJoint takes the root out of the map");
        Engine->DestroyInstance(Child);
        Engine->DestroyInstance(Parent);
    }
    if (Archetype != nullptr && Archetype != G_InvalidEngineHandle)
        Engine->ReleaseHandle(Archetype);
    Engine->Release();
}

// Whether a UDP socket can bind Port on every interface (DirectPlay's server binds so).
bool CanBindUdp(unsigned short Port)
{
    int const Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (Socket < 0)
        return false;
    sockaddr_in Local{};
    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = htonl(INADDR_ANY);
    Local.sin_port = htons(Port);
    bool const bBound = bind(Socket, reinterpret_cast<const sockaddr*>(&Local), sizeof(Local)) == 0;
    close(Socket);
    return bBound;
}

// A UDP port that is free now: the one the system picks for port 0.
unsigned short FreeUdpPort()
{
    int const Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (Socket < 0)
        return 0;
    sockaddr_in Local{};
    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t Size = sizeof(Local);
    unsigned short Port = 0;
    if (bind(Socket, reinterpret_cast<const sockaddr*>(&Local), sizeof(Local)) == 0 &&
        getsockname(Socket, reinterpret_cast<sockaddr*>(&Local), &Size) == 0)
        Port = ntohs(Local.sin_port);
    close(Socket);
    return Port;
}

// DALib's server, CDPServer, hosts through DPNet (DPNetAdapter.cpp): it holds its UDP port while
// hosting and lets it go when it stops. Its members are reached by their (Itanium) names, as this
// test links no module.
void TestDALibServer(Platform::Module DALib)
{
    auto const Construct =
        reinterpret_cast<void (*)(void*)>(Platform::ModuleSymbol(DALib, "_ZN9CDPServerC1Ev"));
    auto const Destroy =
        reinterpret_cast<void (*)(void*)>(Platform::ModuleSymbol(DALib, "_ZN9CDPServerD1Ev"));
    auto const BeginHosting = reinterpret_cast<bool (*)(void*, unsigned int)>(
        Platform::ModuleSymbol(DALib, "_ZN9CDPServer12BeginHostingEj"));
    auto const StopHosting = reinterpret_cast<void (*)(void*)>(
        Platform::ModuleSymbol(DALib, "_ZN9CDPServer11StopHostingEv"));
    Check(Construct != nullptr && Destroy != nullptr && BeginHosting != nullptr &&
              StopHosting != nullptr,
          "DALib exports CDPServer");
    if (Construct == nullptr || Destroy == nullptr || BeginHosting == nullptr ||
        StopHosting == nullptr)
        return;
    unsigned short const Port = FreeUdpPort();
    Check(Port != 0, "a free UDP port");
    if (Port == 0)
        return;
    alignas(alignof(std::max_align_t)) static unsigned char Storage[sizeof(CDPServer)];
    Construct(Storage);
    Check(BeginHosting(Storage, Port), "CDPServer::BeginHosting hosts through DPNet");
    Check(!CanBindUdp(Port), "the hosting server holds its UDP port");
    StopHosting(Storage);
    Check(CanBindUdp(Port), "StopHosting lets the port go");
    Destroy(Storage);
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
    Platform::Module const Zlib = Platform::ModuleLoad((Folder + "zlib.dll").c_str());
    Check(Zlib != nullptr && Platform::ModuleSymbol(Zlib, "uncompress") != nullptr,
          "zlib.dll loads and exports uncompress");
    Platform::Module const LuaProfile = Platform::ModuleLoad((Folder + "luaprofile.dll").c_str());
    Check(LuaProfile != nullptr, "luaprofile.dll loads");
    Platform::Module const Thorn = Platform::ModuleLoad((Folder + "Thorn.dll").c_str());
    Check(Thorn != nullptr, "Thorn.dll loads");
    Platform::Module const EngBase = Platform::ModuleLoad((Folder + "EngBase.dll").c_str());
    Check(EngBase != nullptr, "EngBase.dll loads");
    Check(Platform::ModuleLoad((Folder + "ximage.dll").c_str()) != nullptr, "ximage.dll loads");
    Platform::Module const DALib = Platform::ModuleLoad((Folder + "DALib.dll").c_str());
    Check(DALib != nullptr && Platform::ModuleSymbol(DALib, "CriticalWarningFn") != nullptr,
          "DALib.dll loads and exports its error hooks");
    if (DALib != nullptr)
        TestDALibServer(DALib);

    Platform::Module const Dacom = Platform::ModuleFind((Folder + "dacom.dylib").c_str()) != nullptr
                                       ? Platform::ModuleFind((Folder + "dacom.dylib").c_str())
                                       : Platform::ModuleFind((Folder + "dacom.so").c_str());
    Check(Dacom != nullptr, "dacom is loaded as system's dependency");
    auto const Acquire =
        reinterpret_cast<ICOManager* (*)()>(Platform::ModuleSymbol(Dacom, "DACOM_Acquire"));
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
        Count = 0;
        Manager->EnumerateComponents("IScriptEngine", &CountComponent, &Count);
        Check(Count >= 1, "Thorn's DllMain registered its script engine");
        for (const char* Component :
             {"Engine", "Animation", "Hardpoint", "ChannelArchetype", "BigImage"})
        {
            Count = 0;
            Manager->EnumerateComponents(Component, &CountComponent, &Count);
            Check(Count == 1, "EngBase's and ximage's DllMains registered their components");
        }
        // Shading registers the material, texture-library and batcher components as it loads.
        int Before = 0;
        Manager->EnumerateComponents(nullptr, &CountComponent, &Before);
        Check(Platform::ModuleLoad((Folder + "Shading.dll").c_str()) != nullptr,
              "Shading.dll loads");
        int After = 0;
        Manager->EnumerateComponents(nullptr, &CountComponent, &After);
        Check(After > Before, "Shading's DllMain registered its components");
        for (const char* Module : {"RendComp.dll", "alchemy.dll", "flmaterials.dll"})
        {
            Before = After;
            Check(Platform::ModuleLoad((Folder + Module).c_str()) != nullptr,
                  "a render module loads");
            After = 0;
            Manager->EnumerateComponents(nullptr, &CountComponent, &After);
            Check(After > Before, "the render module's DllMain registered its components");
        }
        for (const char* Component : {"GlassMaterial", "NomadMaterial", "FLAppearance"})
        {
            Count = 0;
            Manager->EnumerateComponents(Component, &CountComponent, &Count);
            Check(Count == 1, "flmaterials' DllMain registered its materials and FLAppearance");
        }
        TestFlMaterials(Manager, Dacom);
        // Deformable2 registers its one component, "Deformable", as it loads.
        Check(Platform::ModuleLoad((Folder + "Deformable2.dll").c_str()) != nullptr,
              "Deformable2.dll loads");
        Count = 0;
        Manager->EnumerateComponents("Deformable", &CountComponent, &Count);
        Check(Count == 1, "Deformable2's DllMain registered Deformable");
        if (EngBase != nullptr)
            TestEngine(Manager);
        if (Files != nullptr)
            TestReadFile(Manager);
        Manager->Release();
    }

    auto const Stricmp = reinterpret_cast<int (*)(const char*, const char*)>(
        Platform::ModuleSymbol(Dacom, "stricmp"));
    Check(Stricmp != nullptr && Stricmp("Freelancer", "FREELANCER") == 0, "dacom exports stricmp");

    // One LowArena per process: a block made in one module decodes in another, and a block freed
    // in one is reused by the other (arena_probe.cpp, two copies).
    Platform::Module const ProbeA = Platform::ModuleLoad((Folder + "arena_probe_a.dll").c_str());
    Platform::Module const ProbeB = Platform::ModuleLoad((Folder + "arena_probe_b.dll").c_str());
    auto const MakeA =
        reinterpret_cast<FlLong (*)(int)>(Platform::ModuleSymbol(ProbeA, "Probe_a_Make"));
    auto const ReadB =
        reinterpret_cast<int (*)(FlLong)>(Platform::ModuleSymbol(ProbeB, "Probe_b_Read"));
    auto const FreeB =
        reinterpret_cast<void (*)(FlLong)>(Platform::ModuleSymbol(ProbeB, "Probe_b_Free"));
    Check(MakeA != nullptr && ReadB != nullptr && FreeB != nullptr, "the arena probes load");
    if (MakeA != nullptr && ReadB != nullptr && FreeB != nullptr)
    {
        FlLong const Made = MakeA(4242);
        Check(Made > 0 && ReadB(Made) == 4242,
              "a LowArena block encoded in one module decodes in another");
        FreeB(Made);
        Check(MakeA(7) == Made, "a block freed in one module is reused by another");
    }
    auto const IdA =
        reinterpret_cast<FlULong (*)(const void*)>(Platform::ModuleSymbol(ProbeA, "Probe_a_Id"));
    auto const ObjectB = reinterpret_cast<const void* (*)(FlULong)>(
        Platform::ModuleSymbol(ProbeB, "Probe_b_Object"));
    if (IdA != nullptr && ObjectB != nullptr)
    {
        static int const Named = 0;
        Check(ObjectB(IdA(&Named)) == &Named,
              "a HandleValue id made in one module names the object in another");
    }

    // Common loads (with DALib, qhull and the physics it carries) and runs: a sphere inside another
    // overlaps by its own volume, two spheres apart by nothing.
    Platform::Module const Common = Platform::ModuleLoad((Folder + "Common.dll").c_str());
    Check(Common != nullptr, "Common.dll loads");
    auto const SphereVolume = reinterpret_cast<float (*)(float, float, float)>(
        Platform::ModuleSymbol(Common, "_Z21GetSphereIntersectVolfff"));
    Check(SphereVolume != nullptr && SphereVolume(10.0f, 1.0f, 1.0f) == 0.0f &&
              SphereVolume(0.0f, 1.0f, 5.0f) == 4.1887903f,
          "Common's GetSphereIntersectVol");

    // Server loads (on dacom, DALib and Common) and runs: destroying a phantom it never made
    // reports -2, as retail's does.
    Platform::Module const Server = Platform::ModuleLoad((Folder + "Server.dll").c_str());
    Check(Server != nullptr, "Server.dll loads");
    auto const PhantomDestroy = reinterpret_cast<int (*)(void*)>(
        Platform::ModuleSymbol(Server, "_ZN3pub7Phantom7DestroyEPv"));
    Check(PhantomDestroy != nullptr && PhantomDestroy(nullptr) == -2,
          "Server's pub::Phantom::Destroy");

    using FEventCreate = Platform::Handle (*)();
    using FEventSet = bool (*)(Platform::Handle);
    using FEventWait = FlULong (*)(Platform::Handle);
    using FThreadId = FlULong (*)();
    auto const EventCreateA =
        reinterpret_cast<FEventCreate>(Platform::ModuleSymbol(ProbeA, "Probe_a_EventCreate"));
    auto const EventSetB =
        reinterpret_cast<FEventSet>(Platform::ModuleSymbol(ProbeB, "Probe_b_EventSet"));
    auto const EventWaitA =
        reinterpret_cast<FEventWait>(Platform::ModuleSymbol(ProbeA, "Probe_a_EventWait"));
    auto const ThreadIdA =
        reinterpret_cast<FThreadId>(Platform::ModuleSymbol(ProbeA, "Probe_a_ThreadId"));
    auto const ThreadIdB =
        reinterpret_cast<FThreadId>(Platform::ModuleSymbol(ProbeB, "Probe_b_ThreadId"));
    Check(EventCreateA != nullptr && EventSetB != nullptr && EventWaitA != nullptr,
          "the probes export the event functions");
    if (EventCreateA != nullptr && EventSetB != nullptr && EventWaitA != nullptr)
    {
        Platform::Handle const Event = EventCreateA();
        Check(Event != nullptr && EventWaitA(Event) == Platform::kWaitTimeout && EventSetB(Event) &&
                  EventWaitA(Event) == Platform::kWaitSignaled &&
                  Platform::WaitOne(Event, 0) == Platform::kWaitSignaled,
              "an event made in one module is set in another and seen by all");
        Platform::CloseHandle(Event);
    }
    Check(ThreadIdA != nullptr && ThreadIdB != nullptr && ThreadIdA() == ThreadIdB() &&
              ThreadIdA() == Platform::CurrentThreadId(),
          "a thread has one id in every module");

    // SearchPathA finds a module by its Windows name (DACOM's AddLibrary looks it up so).
    char Found[1024] = {};
    char* FilePart = nullptr;
    DWORD const FoundLength =
        SearchPathA(Folder.c_str(), "SYSTEM.DLL", nullptr, sizeof(Found), Found, &FilePart);
    Check(FoundLength > 0 && FilePart != nullptr && strncasecmp(FilePart, "system.", 7) == 0 &&
              Platform::ModuleFind(Found) != nullptr,
          "SearchPathA finds system.dll as the module this platform built, by a path that opens");
    Check(SearchPathA(Folder.c_str(), "nothing.dll", nullptr, sizeof(Found), Found, nullptr) == 0,
          "SearchPathA: a missing module");

    // The MSVC CRT stand-ins (posix/msvc_crt.h) the modules use.
    char Drive[_MAX_DRIVE], Directory[_MAX_DIR], Name[_MAX_FNAME], Extension[_MAX_EXT];
    _splitpath("C:\\Freelancer\\EXE\\dacom.dll", Drive, Directory, Name, Extension);
    Check(std::strcmp(Drive, "C:") == 0 && std::strcmp(Directory, "\\Freelancer\\EXE\\") == 0 &&
              std::strcmp(Name, "dacom") == 0 && std::strcmp(Extension, ".dll") == 0,
          "_splitpath");
    _splitpath("a.b/file", nullptr, Directory, Name, Extension);
    Check(std::strcmp(Directory, "a.b/") == 0 && std::strcmp(Name, "file") == 0 &&
              Extension[0] == 0,
          "_splitpath: a '.' in a folder is no extension");
    char Path[_MAX_PATH];
    _makepath(Path, "C", "\\data", "ships", "ini");
    Check(std::strcmp(Path, "C:\\data\\ships.ini") == 0, "_makepath");
    char Buffer[4] = {'x', 'x', 'x', 'x'};
    Check(_snprintf(Buffer, 4, "%d", 12) == 2 && std::strcmp(Buffer, "12") == 0,
          "_snprintf that fits");
    Check(_snprintf(Buffer, 4, "%d", 1234) == 4 && std::memcmp(Buffer, "1234", 4) == 0,
          "_snprintf that fills: no terminator");
    Check(_snprintf(Buffer, 4, "%d", 123456) == -1 && std::memcmp(Buffer, "1234", 4) == 0,
          "_snprintf that overflows: -1");
    Check(_stricmp("abc_", "ABCD") < 0 && _strnicmp("FreeX", "freeY", 4) == 0,
          "_stricmp lowers both sides (so _ sorts before letters)");

    // The kernel32 stand-ins (posix/kernel32.h).
    FILETIME Epoch = {};
    Check(DosDateTimeToFileTime(0x21, 0, &Epoch) && Epoch.dwHighDateTime == 0x01A8E79Fu &&
              Epoch.dwLowDateTime == 0xE1D58000u,
          "DosDateTimeToFileTime: 1980-01-01");
    Check(!DosDateTimeToFileTime(0x20, 0, &Epoch), "DosDateTimeToFileTime refuses day 0");
    Check(CreateFileA("/nonexistent/x", GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr) ==
                  INVALID_HANDLE_VALUE &&
              GetLastError() == ERROR_FILE_NOT_FOUND,
          "CreateFileA: a missing file sets ERROR_FILE_NOT_FOUND");
    void* const Pages = VirtualAlloc(nullptr, 0x3000, MEM_COMMIT, PAGE_READWRITE);
    Check(Pages != nullptr && static_cast<unsigned char*>(Pages)[0x2FFF] == 0 &&
              VirtualFree(Pages, 0, MEM_RELEASE) && !VirtualFree(Pages, 0, MEM_RELEASE),
          "VirtualAlloc / VirtualFree");

    // SSE's scalar conversions (posix/xmmintrin.h on Apple Silicon, the real ones on x86).
    // The inputs are volatile so the compiler runs the conversions instead of folding them (GCC
    // folds an out-of-range constant as C++'s undefined conversion, not as the instruction).
    volatile float const Half = 2.5f, OneAndHalf = 3.5f, MinusHalf = -2.5f, Fraction = -1.7f,
                         Big = 3.0e9f;
    Check(_mm_cvtss_si32(_mm_set_ss(Half)) == 2 && _mm_cvtss_si32(_mm_set_ss(OneAndHalf)) == 4 &&
              _mm_cvtss_si32(_mm_set_ss(MinusHalf)) == -2,
          "_mm_cvtss_si32 rounds to nearest even");
    Check(_mm_cvtt_ss2si(_mm_set_ss(Fraction)) == -1 && _mm_cvtt_ss2si(_mm_set_ss(Big)) == INT_MIN,
          "_mm_cvtt_ss2si truncates; out of range is 0x80000000");
    unsigned long BitIndex = 0;
    Check(_BitScanReverse(&BitIndex, 0x00F0u) && BitIndex == 7 &&
              _BitScanForward(&BitIndex, 0x00F0u) && BitIndex == 4 &&
              !_BitScanForward(&BitIndex, 0u),
          "_BitScanReverse / _BitScanForward");

    if (G_Failures == 0)
        std::printf("module_tests: all passed\n");
    return G_Failures;
}
