// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): loads the client modules the non-Windows build makes (src/PortTest/CMakeLists.txt)
// the way the game does, by their Windows names, and checks that their DllMains ran: dacom's
// COManager exists, and the components the other modules register at load are there. The exit
// code is the number of failures.

#include "dacom.h"
#include "dpaddr.h"
#include "dplay8.h"
#include "dalib_net.h"
#include "engine/engbase.h"
#include "luaprofile.h"
#include "platform.h"
#include "readfile.h"
#include "render/IRenderPipeline.h"
#include "render/shading.h"

#if defined(FL_TEST_RPGPU)
#include <SDL3/SDL.h>
#endif

#include <intrin.h>
#include <xmmintrin.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
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

// DPNetAdapter's IDirectPlay8Server, closed by its own message handler on the network thread
// (an enumeration query's handler here). The teardown waits until the network loop has left the
// server: the port is let go, and the object can be initialised and host again.
IDirectPlay8Server* G_ClosingServer = nullptr;
std::atomic<int> G_EnumQueries{0};

HRESULT WINAPI CloseOnEnumQuery(PVOID, DWORD MessageId, PVOID)
{
    if (MessageId == DPN_MSGID_ENUM_HOSTS_QUERY && G_EnumQueries++ == 0)
        G_ClosingServer->Close(0);
    return S_OK;
}

// The same, dropping the last reference instead: NetLoop deletes the object once it is out.
IDirectPlay8Server* G_ReleasedServer = nullptr;
std::atomic<int> G_ReleasingQueries{0};

HRESULT WINAPI ReleaseOnEnumQuery(PVOID, DWORD MessageId, PVOID)
{
    if (MessageId == DPN_MSGID_ENUM_HOSTS_QUERY && G_ReleasingQueries++ == 0)
        G_ReleasedServer->Release();
    return S_OK;
}

HRESULT WINAPI IgnoreMessages(PVOID, DWORD, PVOID)
{
    return S_OK;
}

// Waits up to two seconds for Done.
template <typename FDone>
bool WaitFor(FDone Done)
{
    for (int Try = 0; Try < 200; ++Try)
    {
        if (Done())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return Done();
}

// An enumeration query ([MC-DPLHP] 2.2.1, no application GUID) to Port on this machine.
void SendEnumQuery(unsigned short Port)
{
    int const Client = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    unsigned char const Query[] = {0x00, 0x02, 0x00, 0x00, 0x02};
    sockaddr_in To{};
    To.sin_family = AF_INET;
    To.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    To.sin_port = htons(Port);
    sendto(Client, Query, sizeof(Query), 0, reinterpret_cast<const sockaddr*>(&To), sizeof(To));
    close(Client);
}

HRESULT HostOn(IDirectPlay8Server* Server, IDirectPlay8Address* Device, unsigned short Port)
{
    DWORD const Value = Port;
    Device->AddComponent(DPNA_KEY_PORT, &Value, sizeof(Value), DPNA_DATATYPE_DWORD);
    DPN_APPLICATION_DESC Desc{};
    Desc.dwSize = sizeof(Desc);
    Desc.dwMaxPlayers = 4;
    return Server->Host(&Desc, &Device, 1, nullptr, nullptr, nullptr, 0);
}

void TestDPNetCloseFromHandler(Platform::Module DALib)
{
    auto const CreateServer = reinterpret_cast<HRESULT (*)(IDirectPlay8Server**)>(
        Platform::ModuleSymbol(DALib, "_ZN12DPNetAdapter12CreateServerEPP18IDirectPlay8Server"));
    auto const CreateAddress = reinterpret_cast<HRESULT (*)(IDirectPlay8Address**)>(
        Platform::ModuleSymbol(DALib, "_ZN12DPNetAdapter13CreateAddressEPP19IDirectPlay8Address"));
    Check(CreateServer != nullptr && CreateAddress != nullptr, "DALib exports DPNetAdapter");
    if (CreateServer == nullptr || CreateAddress == nullptr)
        return;
    IDirectPlay8Server* Server = nullptr;
    IDirectPlay8Address* Device = nullptr;
    Check(CreateServer(&Server) == S_OK && CreateAddress(&Device) == S_OK,
          "DPNetAdapter makes a server and an address");
    if (Server == nullptr || Device == nullptr)
        return;
    G_ClosingServer = Server;

    unsigned short const Port = FreeUdpPort();
    Check(Server->Initialize(nullptr, CloseOnEnumQuery, 0) == S_OK, "the server initialises");
    Check(HostOn(Server, Device, Port) == S_OK, "the server hosts");

    SendEnumQuery(Port); // its handler closes the server
    Check(WaitFor([] { return G_EnumQueries.load() > 0; }),
          "the enumeration query reaches the handler");
    Check(WaitFor([Port] { return CanBindUdp(Port); }),
          "a Close from the handler lets the port go once the network loop is out");

    // The closed object initialises and hosts again (its finished network thread is joined).
    HRESULT Initialised = DPNERR_ALREADYCLOSING;
    WaitFor(
        [&]
        {
            return (Initialised = Server->Initialize(nullptr, IgnoreMessages, 0)) !=
                   DPNERR_ALREADYCLOSING;
        });
    Check(Initialised == S_OK, "the server initialises again after that Close");
    unsigned short const Port2 = FreeUdpPort();
    Check(HostOn(Server, Device, Port2) == S_OK, "and hosts again");
    Check(Server->Close(0) == S_OK && CanBindUdp(Port2), "an ordinary Close lets the port go");
    Server->Release();

    // The handler drops the last reference: the server is not touched again here.
    IDirectPlay8Server* Released = nullptr;
    Check(CreateServer(&Released) == S_OK, "DPNetAdapter makes another server");
    if (Released != nullptr)
    {
        G_ReleasedServer = Released;
        unsigned short const Port3 = FreeUdpPort();
        Check(Released->Initialize(nullptr, ReleaseOnEnumQuery, 0) == S_OK &&
                  HostOn(Released, Device, Port3) == S_OK,
              "it initialises and hosts");
        SendEnumQuery(Port3); // its handler releases the server
        Check(WaitFor([] { return G_ReleasingQueries.load() > 0; }),
              "the enumeration query reaches the releasing handler");
        Check(WaitFor([Port3] { return CanBindUdp(Port3); }),
              "the last Release from the handler lets the port go");
    }
    Device->Release();
}

int __cdecl CountComponent(IDAComponent* Factory, const char* Name, void*, void* Context)
{
    if (Factory != nullptr && Name != nullptr)
        ++*static_cast<int*>(Context);
    return 1; // carry on
}
}

#if defined(FL_TEST_RPGPU)
// RPGPU (docs/re/rpgpu.md): its component made as the client makes it, through dacom with RP8's
// 0x14-byte aggregation record, reading the ini section that record names.
IRenderPipeline* MakeRpGpu(ICOManager* Manager, const char* Section)
{
    struct
    {
        FlULong Size;
        const char* Name;
        IDAComponent* Outer;
        IDAComponent** InnerOut;
        const char* Section;
    } Desc = {0x14, "SDLGPU", nullptr, nullptr, Section};
    IRenderPipeline* Pipeline = nullptr;
    if (Manager->CreateInstance((AGGDESC*)(&Desc), (void**)(&Pipeline)) != GR_OK)
        return nullptr;
    if (Pipeline != nullptr && Pipeline->Startup(nullptr) != GR_OK)
    {
        Pipeline->Release();
        return nullptr;
    }
    return Pipeline;
}

// One frame cleared to Argb on Window, then the frame read back through PrintScreen's BMP: true
// when every pixel is Argb's colour and the image is Width x Height (0: any size).
bool ClearPresentReadBack(IRenderPipeline* Pipeline, RpWindow Window, int Width, int Height,
                          std::uint32_t Argb, const std::string& Bmp, std::uint32_t* OutWidth,
                          std::uint32_t* OutHeight)
{
    int Mode[10] = {};
    Mode[2] = Width;
    Mode[3] = Height;
    Mode[4] = 32; // colour bits
    Mode[8] = 1;  // buffer count; interval 0, windowed
    int Out[10] = {};
    if (Pipeline->CreateBuffers(Window, Mode, Out) != GR_OK)
        return false;
    Pipeline->SetPipelineState(8, static_cast<int>(Argb)); // the clear colour
    bool bOk = Pipeline->BeginScene() == GR_OK && Pipeline->ClearBuffers(3, nullptr) == GR_OK &&
               Pipeline->EndScene() == GR_OK && Pipeline->SwapBuffers() == GR_OK;
    IRPTexture* Texture = nullptr;
    bOk = bOk && Pipeline->QueryInterface(Iids::RPTexture, (void**)(&Texture)) == GR_OK &&
          Texture != nullptr;
    bOk = bOk && Texture->PrintScreen(nullptr, Bmp.c_str()) == GR_OK;
    if (Texture != nullptr)
        Texture->Release();
    Pipeline->DestroyGpuResources();
    if (!bOk)
        return false;

    // A bottom-up 24-bit BMP: 54-byte header, rows padded to four bytes.
    std::vector<unsigned char> File;
    if (FILE* const Handle = fopen(Bmp.c_str(), "rb"))
    {
        unsigned char Chunk[4096];
        for (std::size_t Read; (Read = std::fread(Chunk, 1, sizeof(Chunk), Handle)) > 0;)
            File.insert(File.end(), Chunk, Chunk + Read);
        std::fclose(Handle);
    }
    std::remove(Bmp.c_str());
    auto const U32 = [&File](std::size_t At)
    {
        return static_cast<std::uint32_t>(File[At] | File[At + 1] << 8 | File[At + 2] << 16 |
                                          static_cast<std::uint32_t>(File[At + 3]) << 24);
    };
    if (File.size() < 54 || File[0] != 'B' || File[1] != 'M' || U32(10) != 54)
        return false;
    std::uint32_t const W = U32(18), H = U32(22);
    std::size_t const RowBytes = (static_cast<std::size_t>(W) * 3 + 3) & ~std::size_t(3);
    if (W == 0 || H == 0 ||
        (Width != 0 && (W != std::uint32_t(Width) || H != std::uint32_t(Height))) ||
        File.size() < 54 + RowBytes * H)
        return false;
    *OutWidth = W;
    *OutHeight = H;
    for (std::uint32_t Y = 0; Y < H; ++Y)
        for (std::uint32_t X = 0; X < W; ++X)
        {
            const unsigned char* const Pixel = &File[54 + Y * RowBytes + X * 3];
            if (Pixel[0] != (Argb & 0xFF) || Pixel[1] != ((Argb >> 8) & 0xFF) ||
                Pixel[2] != ((Argb >> 16) & 0xFF))
                return false;
        }
    return true;
}

// RPGPU without a device (NULL_DEVICE = 1), and, where SDL_GPU has a device that takes RPGPU's
// shaders, its SDL backend on a hidden SDL window whose id is the HWND (the client's window
// contract off Windows): a clear presents and reads back. Without a GPU (a headless Linux run,
// a CI runner without one) the SDL check is skipped, with a message.
void TestRpGpu(ICOManager* Manager, const std::string& Folder)
{
    int Count = 0;
    Check(Platform::ModuleLoad((Folder + "RPGPU.dll").c_str()) != nullptr, "RPGPU.dll loads");
    Manager->EnumerateComponents("SDLGPU", &CountComponent, &Count);
    Check(Count == 1, "RPGPU's DllMain registered SDLGPU");

    char Template[] = "/tmp/module_tests_rpgpu_XXXXXX";
    if (mkdtemp(Template) == nullptr)
    {
        Check(false, "scratch folder for RPGPU");
        return;
    }
    std::string const Root = Template;
    std::string const Ini = Root + "/rpgpu.ini";
    if (FILE* const File = fopen(Ini.c_str(), "wb"))
    {
        std::fputs("[RpGpuNull]\nNULL_DEVICE = 1\n[RpGpuSdl]\nNULL_DEVICE = 0\n", File);
        std::fclose(File);
    }
    IProfileParser* const Parser = AcquireProfileParser();
    Check(Parser != nullptr && Parser->LoadFile(Ini.c_str(), 0) == GR_OK,
          "dacom reads RPGPU's ini");
    if (Parser != nullptr)
        Parser->Release();

    if (IRenderPipeline* const Null = MakeRpGpu(Manager, "RpGpuNull"))
    {
        unsigned int Modes = 0;
        Check(Null->GetDisplayModeCount(&Modes) == GR_OK && Modes > 0,
              "RPGPU's Null backend lists display modes");
        std::uint32_t W = 0, H = 0;
        Check(ClearPresentReadBack(Null, reinterpret_cast<RpWindow>(std::uintptr_t(1)), 64, 32,
                                   0xFF336699u, Root + "/null.bmp", &W, &H),
              "RPGPU's Null backend presents a clear and reads it back");
        Null->ShutdownPipeline();
        Null->Release();
    }
    else
        Check(false, "dacom makes RPGPU's SDLGPU component (Null backend)");

    // The SDL backend: only where SDL has video and a GPU device for RPGPU's shader formats. On
    // macOS that is Metal (MSL): the hint makes RPGPU's device a Metal one, as SDL is shared.
#if defined(__APPLE__)
    char const* const Driver = "metal";
    SDL_GPUShaderFormat const Formats = SDL_GPU_SHADERFORMAT_MSL;
    SDL_SetHint(SDL_HINT_GPU_DRIVER, Driver);
#else
    char const* const Driver = nullptr;
    SDL_GPUShaderFormat const Formats =
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL;
#endif
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO))
        std::printf("module_tests: RPGPU's SDL backend not checked: no SDL video (%s)\n",
                    SDL_GetError());
    else
    {
        // A device of SDL's own first: SDL_GPUSupportsShaderFormats only says a driver is there,
        // not that it finds a GPU it can use.
        SDL_GPUDevice* const Probe = SDL_GPUSupportsShaderFormats(Formats, Driver)
                                         ? SDL_CreateGPUDevice(Formats, false, Driver)
                                         : nullptr;
        if (Probe != nullptr)
            SDL_DestroyGPUDevice(Probe);
#if defined(__APPLE__)
        // Metal hands out swap-chain images for a hidden window.
        SDL_WindowFlags const WindowFlags = SDL_WINDOW_HIDDEN;
#else
        // Vulkan hands out none for a hidden window (no frame to read back): a shown one.
        SDL_WindowFlags const WindowFlags = 0;
#endif
        if (Probe == nullptr)
            std::printf("module_tests: RPGPU's SDL backend not checked: no SDL_GPU device\n");
        else if (SDL_Window* const Window = SDL_CreateWindow("module_tests", 64, 32, WindowFlags))
        {
#if !defined(__APPLE__)
            // Until the window is on screen (exposed), for at most a second.
            bool bExposed = false;
            for (Uint64 const Start = SDL_GetTicks(); !bExposed && SDL_GetTicks() - Start < 1000;)
            {
                SDL_Event Event;
                while (SDL_PollEvent(&Event))
                    bExposed = bExposed || Event.type == SDL_EVENT_WINDOW_EXPOSED;
                SDL_Delay(10);
            }
#endif
            // The client's contract off Windows: its HWND is the SDL window's id.
            RpWindow const Hwnd =
                reinterpret_cast<RpWindow>(std::uintptr_t(SDL_GetWindowID(Window)));
            if (IRenderPipeline* const Gpu = MakeRpGpu(Manager, "RpGpuSdl"))
            {
                std::uint32_t W = 0, H = 0;
                bool const bOk =
                    ClearPresentReadBack(Gpu, Hwnd, 64, 32, 0xFF336699u, Root + "/sdl.bmp", &W, &H);
                Check(bOk, "RPGPU's SDL backend presents a clear on the client's SDL window and "
                           "reads it back");
                int PixelW = 0, PixelH = 0;
                SDL_GetWindowSizeInPixels(Window, &PixelW, &PixelH);
                Check(!bOk || (W == std::uint32_t(PixelW) && H == std::uint32_t(PixelH)),
                      "RPGPU's SDL frame is the window's size in pixels");
                if (bOk)
                    std::printf("module_tests: RPGPU's SDL backend (%s) cleared, presented and "
                                "read back %ux%u\n",
                                Driver != nullptr ? Driver : "SDL_GPU", W, H);
                Gpu->ShutdownPipeline();
                Gpu->Release();
                // RPGPU let go of the window and left it to its owner.
                Check(SDL_GetWindowFromID(SDL_GetWindowID(Window)) == Window,
                      "RPGPU's shutdown leaves the client's SDL window alive");
            }
            else
                Check(false, "dacom makes RPGPU's SDLGPU component (SDL backend)");
            SDL_DestroyWindow(Window);
        }
        else
            std::printf("module_tests: RPGPU's SDL backend not checked: no SDL window (%s)\n",
                        SDL_GetError());
        SDL_QuitSubSystem(SDL_INIT_VIDEO);
    }
#if defined(__APPLE__)
    SDL_ResetHint(SDL_HINT_GPU_DRIVER);
#endif
    std::system(("rm -rf '" + Root + "'").c_str());
}
#endif

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
    {
        TestDALibServer(DALib);
        TestDPNetCloseFromHandler(DALib);
    }

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
#if defined(FL_TEST_RPGPU)
        TestRpGpu(Manager, Folder);
#endif
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

    // ModulePath resolves a loaded module's handle to its file (GetModuleFileNameA), as
    // Content's StoryLauncher, rpclocal's Startup and DALib's DACOM.INI lookup ask of it.
    {
        Platform::Module const DALibHandle = Platform::ModuleLoad((Folder + "DALib.dll").c_str());
        char DALibPath[0x400] = {};
        FlULong const Length = Platform::ModulePath(DALibHandle, DALibPath, sizeof(DALibPath));
        std::string const Found(DALibPath);
        auto const EndsWith = [&Found](std::string const& Suffix)
        {
            return Found.size() >= Suffix.size() &&
                   Found.compare(Found.size() - Suffix.size(), Suffix.size(), Suffix) == 0;
        };
        Check(DALibHandle != nullptr && Length == Found.size() &&
                  (EndsWith("/DALib.dylib") || EndsWith("/DALib.so")),
              "ModulePath of a loaded module's handle");
        char Small[4] = {};
        Check(Platform::ModulePath(DALibHandle, Small, sizeof(Small)) == sizeof(Small) &&
                  Small[3] == 0,
              "ModulePath reports a truncated path with the buffer's size, as GetModuleFileNameA");
    }

    // Content loads (on Server) and exports the controller entry points Server looks up. Its
    // random mission generator is a static that reads RandomMissions\*.ini through
    // pub::GetDataPath() (DALib::Data) as the module loads, as retail's does, so the data path
    // is opened first, as FLServer has it before it loads Content: an empty folder here, where
    // every table is missing and skipped.
    {
        Platform::Module const DALibModule = Platform::ModuleLoad((Folder + "DALib.dll").c_str());
        auto const OpenData = reinterpret_cast<void (*)(const char*)>(
            Platform::ModuleSymbol(DALibModule, "_ZN5DALib8OpenDataEPKc"));
        auto const CloseData = reinterpret_cast<void (*)()>(
            Platform::ModuleSymbol(DALibModule, "_ZN5DALib9CloseDataEv"));
        // OpenData makes its file system through DALib::Dacom, which DALib::Startup sets.
        auto* const DALibDacom =
            static_cast<ICOManager**>(Platform::ModuleSymbol(DALibModule, "_ZN5DALib5DacomE"));
        char DataTemplate[] = "/tmp/module_tests_data_XXXXXX";
        bool const bDataFolder = mkdtemp(DataTemplate) != nullptr;
        Check(OpenData != nullptr && CloseData != nullptr && DALibDacom != nullptr && bDataFolder,
              "DALib's OpenData / CloseData / Dacom and a data folder for Content");
        if (OpenData != nullptr && CloseData != nullptr && DALibDacom != nullptr && bDataFolder &&
            Manager != nullptr)
        {
            ICOManager* const PreviousDacom = *DALibDacom;
            *DALibDacom = Manager;
            OpenData(DataTemplate);
            Platform::Module const Content = Platform::ModuleLoad((Folder + "Content.dll").c_str());
            Check(Content != nullptr, "Content.dll loads (with an empty data path)");
            Check(Platform::ModuleSymbol(Content, "CreateInstance") != nullptr &&
                      Platform::ModuleSymbol(Content, "DestroyInstance") != nullptr,
                  "Content exports CreateInstance and DestroyInstance");
            CloseData();
            *DALibDacom = PreviousDacom;
            rmdir(DataTemplate);
        }
    }

    // The client's modules: imeui, and RemoteServer, the client's in-process server, which
    // exports the server interface the client asks for (rpclocal's Startup).
    Check(Platform::ModuleLoad((Folder + "imeui.dll").c_str()) != nullptr, "imeui.dll loads");
    {
        Platform::Module const RemoteServer =
            Platform::ModuleLoad((Folder + "remoteserver.dll").c_str());
        Check(RemoteServer != nullptr, "remoteserver.dll loads");
        Check(Platform::ModuleSymbol(RemoteServer, "GetServerInterface") != nullptr &&
                  Platform::ModuleSymbol(RemoteServer, "Server") != nullptr,
              "RemoteServer exports GetServerInterface and Server");
    }

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
