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
#include "render/RenderPipeline.h"
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
    FAGGDESC14 Desc = {kAggDesc14Size, "GlassMaterial", nullptr, nullptr, nullptr};
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
        // The client keeps instances and archetypes in 32-bit fields (engine/engbase.h).
        Check(EngineHandleFromId(EngineIdFromHandle(Parent)) == Parent &&
                  EngineHandleFromId(EngineIdFromHandle(Archetype)) == Archetype,
              "an instance's and an archetype's 32-bit ids decode to them");
        Check(EngineIdFromHandle(G_InvalidEngineHandle) == -1 &&
                  EngineHandleFromId(-1) == G_InvalidEngineHandle,
              "the invalid engine handle is -1 as an id");
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

// A pipeline texture handle (a LowArena block) as the 32-bit value a texture frame keeps, and
// back (render/RenderPipeline.h).
void TestPipelineTextureId()
{
    void* const Texture = LowArena::Allocate(32);
    Check(PipelineTextureFromId(PipelineTextureId(Texture)) == Texture,
          "a texture handle's 32-bit id decodes to it");
    Check(PipelineTextureId(nullptr) == 0 && PipelineTextureFromId(0) == nullptr,
          "no texture is 0");
    LowArena::Free(Texture, 32);
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

// RP8's vertex copy (render/rp8_vertex_copy.h) through RPGPU's IRPVertexBuffer::CopyVertices
// and IVertexBufferManager::CopyVertexData: a sphere's separate position / normal / texture
// streams, an indexed source into a format with a diffuse colour it does not have, and a
// pre-transformed format, which RP8's jump table copies nothing for.
void TestRpGpuVertexCopy(IRenderPipeline* Pipeline)
{
    IRPVertexBuffer* Buffers = nullptr;
    IVertexBufferManager* Manager = nullptr;
    Pipeline->QueryInterface(Iids::RPVertexBuffer, (void**)(&Buffers));
    Pipeline->QueryInterface(Iids::VertexBufferManager, (void**)(&Manager));
    Check(Buffers != nullptr && Manager != nullptr,
          "RPGPU has IRPVertexBuffer and IVertexBufferManager");
    if (Buffers == nullptr || Manager == nullptr)
    {
        if (Buffers != nullptr)
            Buffers->Release();
        if (Manager != nullptr)
            Manager->Release();
        return;
    }

    float const Positions[2][3] = {{1.0f, 2.0f, 3.0f}, {4.0f, 5.0f, 6.0f}};
    float const Normals[2][3] = {{0.0f, 0.0f, 1.0f}, {0.0f, 1.0f, 0.0f}};
    float const Texels[2][2] = {{0.25f, 0.5f}, {0.75f, 1.0f}};
    FlULong const Order[2] = {1, 0};
    auto const Stream = [](const void* Base, unsigned int Size, const FlULong* Indices)
    {
        FRpVertexStream Out = {};
        Out.Base = static_cast<const unsigned char*>(Base);
        Out.ByteSize = Size;
        Out.Stride = Size;
        Out.IndexArray = Indices;
        Out.Count = 2;
        return Out;
    };
    FRpVertexSrcDesc Sphere = {};
    Sphere.SrcFvf = 0x112; // XYZ | NORMAL | TEX1, as SphereMesh hands it over
    Sphere.SrcCount = 2;
    Sphere.Position = Stream(Positions, 0xC, nullptr);
    Sphere.Normal = Stream(Normals, 0xC, nullptr);
    Sphere.Tex[0] = Stream(Texels, 8, nullptr);
    // One vertex of a 0x112 buffer: position, normal, texture set 0.
    auto const Matches = [&](const unsigned char* Vertex, int Source)
    {
        return std::memcmp(Vertex, Positions[Source], 0xC) == 0 &&
               std::memcmp(Vertex + 0xC, Normals[Source], 0xC) == 0 &&
               std::memcmp(Vertex + 0x18, Texels[Source], 8) == 0;
    };
    auto const Zero = [](const unsigned char* Bytes, std::size_t Size)
    {
        for (std::size_t Index = 0; Index < Size; ++Index)
            if (Bytes[Index] != 0)
                return false;
        return true;
    };

    // A static buffer: copied unlocked at a byte offset, then in place into a lock.
    unsigned int* Handle = nullptr;
    Check(Buffers->CreateVb(0x112, 6, &Handle, 0) == GR_OK && Handle != nullptr,
          "RPGPU makes a vertex buffer");
    unsigned int Offset = 0x20; // bytes in, the vertex index out
    Check(Buffers->CopyVertices(Handle, &Offset, &Sphere, 0, 2) == GR_OK && Offset == 1,
          "CopyVertices locks at the byte offset and hands back the vertex index");
    void* Bits = nullptr;
    unsigned int LockAt = 0x80; // vertex 4
    Check(Buffers->LockVb(Handle, &LockAt, &Bits, 2) == GR_OK && LockAt == 4,
          "the buffer locks two vertices at vertex 4");
    unsigned int InLock = 1;
    Check(Buffers->CopyVertices(Handle, &InLock, &Sphere, 1, 1) == GR_OK && InLock == 1,
          "CopyVertices writes into a lock in place, at a vertex of the lock");
    unsigned int PastLock = 1;
    Check(Buffers->CopyVertices(Handle, &PastLock, &Sphere, 0, 2) != GR_OK,
          "a copy into a lock that would run past the locked vertices is refused");
    unsigned int StillLocked = 1;
    Check(Buffers->CopyVertices(Handle, &StillLocked, &Sphere, 1, 1) == GR_OK,
          "the lock stays open after a refused copy: an in-range copy goes in");
    Check(Buffers->ResizeVb(Handle, 0x112, 8) != GR_OK, "a locked buffer is not resized");
    Buffers->UnlockVb(Handle);
    unsigned int Whole = 0;
    if (Buffers->LockVb(Handle, &Whole, &Bits, 6) == GR_OK && Bits != nullptr)
    {
        const unsigned char* const Data = static_cast<const unsigned char*>(Bits);
        Check(Zero(Data, 0x20) && Matches(Data + 0x20, 0) && Matches(Data + 0x40, 1) &&
                  Zero(Data + 0x60, 0x40) && Matches(Data + 0xA0, 1),
              "the copies are RP8's: position, normal and texture at the vertices asked for, "
              "the rest left as it was");
        Buffers->UnlockVb(Handle);
    }
    else
        Check(false, "the vertex buffer locks whole");
    Buffers->DestroyVb((void**)(&Handle));

    // XYZ | NORMAL | TEX1 with a three-float set (0x10112, 0x24 bytes a vertex) takes table A's
    // 0x20-byte fixed layout: RP8 would start the source's vertex 1 four bytes before the lock.
    // RPGPU refuses that copy and writes nothing.
    FRpVertexSrcDesc Wide = Sphere;
    Wide.SrcFvf = 0x10112;
    Check(Buffers->CreateVb(0x10112, 2, &Handle, 0) == GR_OK && Handle != nullptr,
          "RPGPU makes a vertex buffer of a three-float texture set");
    unsigned int AtStart = 0;
    Check(Buffers->CopyVertices(Handle, &AtStart, &Wide, 1, 1) != GR_OK,
          "a copy RP8 would start before the buffer is refused");
    unsigned int WideAt = 0;
    if (Buffers->LockVb(Handle, &WideAt, &Bits, 2) == GR_OK && Bits != nullptr)
    {
        Check(Zero(static_cast<const unsigned char*>(Bits), 0x48), "a refused copy writes nothing");
        Buffers->UnlockVb(Handle);
    }
    else
        Check(false, "the three-float buffer locks after the refused copy");
    Buffers->DestroyVb((void**)(&Handle));

    // A locked pool does not grow (its lock's vertices would move); unlocked, it does.
    unsigned int* Pool = nullptr;
    unsigned int PoolFvf = 0, PoolCount = 0;
    if (Buffers->CreateVb(0x112, 1, &Pool, 1) == GR_OK && Pool != nullptr &&
        Buffers->GetVbInfo(Pool, &PoolFvf, &PoolCount) == GR_OK)
    {
        unsigned int PoolAt = 0;
        void* PoolBits = nullptr;
        unsigned int* Grown = nullptr;
        Check(Buffers->LockVb(Pool, &PoolAt, &PoolBits, 1) == GR_OK &&
                  Buffers->CreateVb(0x112, PoolCount + 1, &Grown, 1) != GR_OK,
              "a locked pool is not grown");
        Buffers->UnlockVb(Pool);
        Check(Buffers->CreateVb(0x112, PoolCount + 1, &Grown, 1) == GR_OK && Grown == Pool,
              "an unlocked pool grows");
    }
    else
        Check(false, "RPGPU hands out its 0x112 pool");

    // The pool through the manager, as SphereMesh draws: LockVertexMemory, CopyVertexData.
    FVertexMemoryLock Lock = {};
    if (Manager->LockVertexMemory(0x112, 2, &Lock) == GR_OK && Lock.Vertices != nullptr)
    {
        Manager->CopyVertexData(Lock.Vertices, Lock.Listing.Fvf, &Sphere, 0, 2);
        const unsigned char* const Data = static_cast<const unsigned char*>(Lock.Vertices);
        Check(Lock.Listing.Fvf == 0x112 && Matches(Data, 0) && Matches(Data + 0x20, 1),
              "CopyVertexData converts a sphere's streams into the vertex memory");
        Manager->UnlockVertexMemory(&Lock);
    }
    else
        Check(false, "RPGPU locks vertex memory");

    // Table A's entry for 0x112 is a fixed-layout converter: it reads the source at 12, 12 and
    // 8 bytes a vertex whatever the descriptor's strides say.
    // The backing holds a 16-byte stride's second position too, with other values there.
    float const PackedPositions[8] = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 9.0f, 9.0f};
    FRpVertexSrcDesc Padded = Sphere;
    Padded.Position = Stream(PackedPositions, 0xC, nullptr);
    Padded.Position.Stride = 16;
    unsigned char Fixed[2][0x20] = {};
    Manager->CopyVertexData(Fixed, 0x112, &Padded, 0, 2);
    Check(Matches(Fixed[0], 0) && Matches(Fixed[1], 1),
          "a same-format sphere copy reads RP8's literal strides, not the descriptor's");

    // Indexed (Flags 4) into XYZ | NORMAL | DIFFUSE | TEX1 (0x152): the diffuse bytes the
    // source has no stream for are left alone.
    FlULong const Identity[2] = {0, 1};
    FRpVertexSrcDesc Indexed = Sphere;
    Indexed.Flags = 4;
    Indexed.Position = Stream(Positions, 0xC, Order);
    Indexed.Normal = Stream(Normals, 0xC, Identity); // an order of its own
    Indexed.Tex[0] = Stream(Texels, 8, Order);
    unsigned char Out[2][0x24];
    std::memset(Out, 0xAB, sizeof(Out));
    Manager->CopyVertexData(Out, 0x152, &Indexed, 0, 2);
    unsigned char const Untouched[4] = {0xAB, 0xAB, 0xAB, 0xAB};
    bool bIndexedOk = true;
    for (int Vertex = 0; Vertex < 2; ++Vertex)
    {
        int const Other = 1 - Vertex;
        bIndexedOk = bIndexedOk && std::memcmp(Out[Vertex], Positions[Other], 0xC) == 0 &&
                     std::memcmp(Out[Vertex] + 0xC, Normals[Vertex], 0xC) == 0 &&
                     std::memcmp(Out[Vertex] + 0x18, Untouched, 4) == 0 &&
                     std::memcmp(Out[Vertex] + 0x1C, Texels[Other], 8) == 0;
    }
    Check(bIndexedOk, "an indexed copy follows each stream's own indices and skips the diffuse "
                      "colour");

    // XYZ | NORMAL | TEX1 with a one-float set (0x30112, 0x1C bytes a vertex): table A's
    // fixed layout writes 0x20 a vertex, past the vertices given; nothing is copied.
    FRpVertexSrcDesc Narrow = Sphere;
    Narrow.SrcFvf = 0x30112;
    unsigned char Short[2][0x1C];
    std::memset(Short, 0xAB, sizeof(Short));
    Manager->CopyVertexData(Short, 0x30112, &Narrow, 0, 2);
    bool bShortUntouched = true;
    for (std::size_t Index = 0; Index < sizeof(Short); ++Index)
        bShortUntouched = bShortUntouched && Short[Index / 0x1C][Index % 0x1C] == 0xAB;
    Check(bShortUntouched, "a fixed layout wider than its format copies nothing");

    // The generic converter (table A's entry for XYZ | TEX1, 0x102, 0x14 bytes a vertex) copies
    // a 16-byte source texture set only up to its vertex's end: 8 bytes, the next vertex's
    // bytes untouched. A destination without a position (0x10, a normal only: 0xC bytes, the
    // copy would write 0x18) copies nothing.
    float const WideTexels[4] = {0.5f, 0.25f, 7.0f, 7.0f};
    FRpVertexSrcDesc Long = {};
    Long.SrcFvf = 0x102;
    Long.Position = Stream(Positions, 0xC, nullptr);
    Long.Tex[0] = Stream(WideTexels, 16, nullptr);
    unsigned char Pair[2][0x14];
    std::memset(Pair, 0xAB, sizeof(Pair));
    Manager->CopyVertexData(Pair, 0x102, &Long, 0, 1);
    bool bPairOk = std::memcmp(Pair[0], Positions[0], 0xC) == 0 &&
                   std::memcmp(Pair[0] + 0xC, WideTexels, 8) == 0;
    for (std::size_t Index = 0; Index < 0x14; ++Index)
        bPairOk = bPairOk && Pair[1][Index] == 0xAB;
    Check(bPairOk, "a texture set wider than the rest of its vertex is cut at the vertex's end");
    FRpVertexSrcDesc NormalOnly = Sphere;
    NormalOnly.Flags = 0;
    std::memset(Pair, 0xAB, sizeof(Pair));
    Manager->CopyVertexData(Pair, 0x10, &NormalOnly, 0, 1);
    bool bNormalOnlyUntouched = true;
    for (std::size_t Index = 0; Index < sizeof(Pair); ++Index)
        bNormalOnlyUntouched = bNormalOnlyUntouched && Pair[Index / 0x14][Index % 0x14] == 0xAB;
    Check(bNormalOnlyUntouched, "a generic copy wider than its format copies nothing");

    // Two texture sets and no position (0x200, 0x10 bytes a vertex): the 12 position bytes
    // leave 4 for set 0 and none for set 1, whose copy would start past the record.
    FRpVertexSrcDesc NoPosition = {};
    NoPosition.SrcFvf = 0x200;
    NoPosition.Position = Stream(Positions, 0xC, nullptr);
    NoPosition.Tex[0] = Stream(Texels, 8, nullptr);
    NoPosition.Tex[1] = Stream(Texels, 8, nullptr);
    unsigned char Bare[2][0x10];
    std::memset(Bare, 0xAB, sizeof(Bare));
    Manager->CopyVertexData(Bare, 0x200, &NoPosition, 0, 1);
    bool bBareOk = std::memcmp(Bare[0], Positions[0], 0xC) == 0 &&
                   std::memcmp(Bare[0] + 0xC, Texels[0], 4) == 0;
    for (std::size_t Index = 0; Index < 0x10; ++Index)
        bBareOk = bBareOk && Bare[1][Index] == 0xAB;
    Check(bBareOk, "a texture set whose copy would start past its vertex writes nothing");

    // XYZRHW | TEX1 (0x104) from separate streams of the same format: RP8's jump table has
    // the empty converter there.
    FRpVertexSrcDesc Transformed = {};
    Transformed.SrcFvf = 0x104;
    Transformed.Position = Stream(Positions, 0xC, nullptr);
    Transformed.Tex[0] = Stream(Texels, 8, nullptr);
    std::memset(Out, 0xAB, sizeof(Out));
    Manager->CopyVertexData(Out, 0x104, &Transformed, 0, 2);
    bool bUntouched = true;
    for (std::size_t Index = 0; Index < sizeof(Out); ++Index)
        bUntouched = bUntouched && Out[Index / 0x24][Index % 0x24] == 0xAB;
    Check(bUntouched, "a pre-transformed format from separate streams copies nothing, as RP8");

    Buffers->Release();
    Manager->Release();
}

// A 24-bit BMP as PrintScreen writes it, read into top-down rows of B, G, R bytes.
struct FRpGpuBmp
{
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<unsigned char> Bgr;

    // 0xRRGGBB at (X, Y), Y counted from the top.
    std::uint32_t Pixel(std::uint32_t X, std::uint32_t Y) const
    {
        const unsigned char* const P = &Bgr[(static_cast<std::size_t>(Y) * Width + X) * 3];
        return static_cast<std::uint32_t>(P[2]) << 16 | static_cast<std::uint32_t>(P[1]) << 8 |
               P[0];
    }
};

bool ReadRpGpuBmp(const std::string& Path, FRpGpuBmp* Out)
{
    std::vector<unsigned char> File;
    if (FILE* const Handle = fopen(Path.c_str(), "rb"))
    {
        unsigned char Chunk[4096];
        for (std::size_t Read; (Read = std::fread(Chunk, 1, sizeof(Chunk), Handle)) > 0;)
            File.insert(File.end(), Chunk, Chunk + Read);
        std::fclose(Handle);
    }
    std::remove(Path.c_str());
    auto const U32 = [&File](std::size_t At)
    {
        return static_cast<std::uint32_t>(File[At] | File[At + 1] << 8 | File[At + 2] << 16 |
                                          static_cast<std::uint32_t>(File[At + 3]) << 24);
    };
    if (File.size() < 54 || File[0] != 'B' || File[1] != 'M' || U32(10) != 54)
        return false;
    std::uint32_t const W = U32(18), H = U32(22);
    std::size_t const RowBytes = (static_cast<std::size_t>(W) * 3 + 3) & ~std::size_t(3);
    if (W == 0 || H == 0 || File.size() < 54 + RowBytes * H)
        return false;
    Out->Width = W;
    Out->Height = H;
    Out->Bgr.resize(static_cast<std::size_t>(W) * H * 3);
    for (std::uint32_t Y = 0; Y < H; ++Y) // bottom-up in the file
        std::memcpy(&Out->Bgr[static_cast<std::size_t>(Y) * W * 3],
                    &File[54 + (H - 1 - Y) * RowBytes], static_cast<std::size_t>(W) * 3);
    return true;
}

bool NearColour(std::uint32_t Rgb, std::uint32_t Want, int Tolerance)
{
    for (int Shift = 0; Shift < 24; Shift += 8)
    {
        const int Have = static_cast<int>((Rgb >> Shift) & 0xFF);
        const int Expected = static_cast<int>((Want >> Shift) & 0xFF);
        if (Have < Expected - Tolerance || Have > Expected + Tolerance)
            return false;
    }
    return true;
}

// The draws the menu scene's materials ask of RPGPU (docs/re/rpgpu.md, "The menu scene"), each a
// frame of its own: cleared to blue, one pre-transformed quad over the whole target, then read
// back. On the Null backend the frame is only the clear, so the checks are that every call
// succeeds; with a device (bPixels) the centre pixel is the colour Direct3D 8 draws.
void TestRpGpuDraws(IRenderPipeline* Pipeline, RpWindow Window, bool bPixels,
                    const std::string& Root, const char* Backend)
{
    int Mode[10] = {};
    Mode[2] = 64;
    Mode[3] = 32;
    Mode[4] = 32;
    Mode[8] = 1;
    int Out[10] = {};
    if (Pipeline->CreateBuffers(Window, Mode, Out) != GR_OK)
    {
        Check(false, "RPGPU's draw checks create buffers");
        return;
    }
    IRPTexture* Texture = nullptr;
    if (Pipeline->QueryInterface(Iids::RPTexture, (void**)(&Texture)) != GR_OK ||
        Texture == nullptr)
    {
        Check(false, "RPGPU answers IRPTexture");
        Pipeline->DestroyGpuResources();
        return;
    }
    auto const Rs = [Pipeline](int State, int Value)
    { return Pipeline->SetRenderState(static_cast<RpRenderState>(State), Value) == GR_OK; };
    auto const Tss = [Pipeline](int Stage, int State, int Value)
    { return Pipeline->SetTextureStageState(Stage, State, Value) == GR_OK; };

    // One frame: Setup sets the state, then a quad with the given diffuse colour; the centre
    // pixel goes to *Centre (with a device).
    struct FVertex
    {
        float X, Y, Z, Rhw;
        std::uint32_t Diffuse;
        float U, V;
    };
    auto const Frame = [&](const char* What, std::uint32_t Diffuse, auto&& Setup,
                           std::uint32_t Want, int Tolerance)
    {
        std::string Message = std::string("RPGPU (") + Backend + "): " + What;
        Pipeline->ResetRenderStatesToDefaults();
        Pipeline->SetPipelineState(8, static_cast<int>(0xFF0000FFu)); // the clear colour: blue
        bool bOk = Pipeline->BeginScene() == GR_OK && Pipeline->ClearBuffers(3, nullptr) == GR_OK;
        bOk = bOk && Rs(7, 0) && Rs(137, 0); // ZENABLE, LIGHTING off
        // Stage 0 selects the diffuse colour and alpha unless Setup says otherwise.
        bOk = bOk && Tss(0, 1, 2) && Tss(0, 2, 0) && Tss(0, 4, 2) && Tss(0, 5, 0) && Tss(1, 1, 1) &&
              Tss(1, 4, 1);
        bOk = bOk && Setup();
        // Larger than any target (a Retina window's is twice its size in points).
        const FVertex Quad[4] = {{0, 0, 0.5f, 1, Diffuse, 0, 0},
                                 {4096, 0, 0.5f, 1, Diffuse, 1, 0},
                                 {0, 4096, 0.5f, 1, Diffuse, 0, 1},
                                 {4096, 4096, 0.5f, 1, Diffuse, 1, 1}};
        // D3DPT_TRIANGLESTRIP, D3DFVF_XYZRHW | DIFFUSE | TEX1.
        bOk = bOk && Pipeline->DrawPrimitive(5, 0x144, Quad, 4, nullptr) == GR_OK;
        bOk = bOk && Pipeline->EndScene() == GR_OK && Pipeline->SwapBuffers() == GR_OK;
        const std::string Bmp = Root + "/draw.bmp";
        bOk = bOk && Texture->PrintScreen(nullptr, Bmp.c_str()) == GR_OK;
        FRpGpuBmp Image;
        bOk = bOk && ReadRpGpuBmp(Bmp, &Image);
        Pipeline->SetTextureStageTexture(0, nullptr);
        if (!bOk)
        {
            Check(false, (Message + ": the frame draws and reads back").c_str());
            return;
        }
        if (!bPixels)
            return;
        const std::uint32_t Centre = Image.Pixel(Image.Width / 2, Image.Height / 2);
        if (!NearColour(Centre, Want, Tolerance))
            std::printf("module_tests: %s: centre 0x%06X, expected 0x%06X\n", Message.c_str(),
                        static_cast<unsigned>(Centre), static_cast<unsigned>(Want));
        Check(NearColour(Centre, Want, Tolerance), Message.c_str());
    };

    // D3DBLEND_BOTHSRCALPHA as SRCBLEND: source alpha and its inverse, DESTBLEND (ONE) ignored.
    Frame(
        "SRCBLEND BOTHSRCALPHA blends by source alpha", 0x80FF0000u,
        [&] { return Rs(27, 1) && Rs(19, 12) && Rs(20, 2); }, 0x80007F, 2);
    // ... and BOTHINVSRCALPHA, the other way round: alpha 0x40 keeps 0.75 of the source.
    Frame(
        "SRCBLEND BOTHINVSRCALPHA blends by inverse source alpha", 0x40FF0000u,
        [&] { return Rs(27, 1) && Rs(19, 13) && Rs(20, 2); }, 0xBF0040, 2);

    // A DXT1 texture (BC1 on the GPU where the device samples it): one 4x4 block, every texel
    // colour 0, pure red in R5G6B5.
    void* Dxt1 = nullptr;
    unsigned int Format = 10; // DXT1
    const unsigned char Block[8] = {0x00, 0xF8, 0x00, 0x00, 0, 0, 0, 0};
    unsigned int SourceFormat = 10;
    bool bDxt1 = Pipeline->CreateTexture(4, 4, &Format, 1, 0, &Dxt1) == GR_OK &&
                 Pipeline->SetTextureLevelData(Dxt1, 0, 4, 4, 8, &SourceFormat, Block, nullptr,
                                               nullptr) == GR_OK;
    Check(bDxt1, "RPGPU makes and fills a DXT1 texture");
    if (bDxt1)
    {
        Frame(
            "a DXT1 texture samples its block's colour", 0xFFFFFFFFu,
            [&] { return Pipeline->SetTextureStageTexture(0, Dxt1) == GR_OK && Tss(0, 2, 2); },
            0xFF0000, 0);
    }
    if (Dxt1 != nullptr)
        Pipeline->DestroyTexture(Dxt1);

    // An 8-bit palettized level (the TGA mips most of the scene's textures are) loaded into an
    // A8R8G8B8 texture: the palette's RGB triplets give the texels their colour.
    void* Paletted = nullptr;
    Format = 7; // A8R8G8B8
    unsigned char Indices[16];
    std::memset(Indices, 1, sizeof(Indices));
    unsigned char Palette[256 * 3] = {};
    Palette[3] = 0x20; // entry 1: R, G, B
    Palette[4] = 0xC0;
    Palette[5] = 0x60;
    SourceFormat = 1; // P8
    bool bP8 = Pipeline->CreateTexture(4, 4, &Format, 1, 0, &Paletted) == GR_OK &&
               Pipeline->SetTextureLevelData(Paletted, 0, 4, 4, 4, &SourceFormat, Indices, nullptr,
                                             Palette) == GR_OK;
    Check(bP8, "RPGPU loads a P8 level with its palette");
    if (bP8)
    {
        Frame(
            "a P8 level's palette colours its texels", 0xFFFFFFFFu,
            [&] { return Pipeline->SetTextureStageTexture(0, Paletted) == GR_OK && Tss(0, 2, 2); },
            0x20C060, 0);
    }
    if (Paletted != nullptr)
        Pipeline->DestroyTexture(Paletted);

    Texture->Release();
    Pipeline->DestroyGpuResources();
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
        TestRpGpuVertexCopy(Null);
        TestRpGpuDraws(Null, reinterpret_cast<RpWindow>(std::uintptr_t(1)), false, Root, "Null");
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
                    TestRpGpuDraws(Gpu, Hwnd, true, Root, Driver != nullptr ? Driver : "SDL_GPU");
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
        // A component descriptor's Size is retail's tag (0x14), also on 64-bit builds where the
        // structure is larger: Shading's factories take it, as SystemContainer fills it.
        {
            FAGGDESC14 Desc = {kAggDesc14Size, "MaterialAnimation", nullptr, nullptr, nullptr};
            IDAComponent* Animation = nullptr;
            Check(Manager->CreateInstance((AGGDESC*)(&Desc), (void**)(&Animation)) == GR_OK &&
                      Animation != nullptr,
                  "Shading makes MaterialAnimation from a descriptor tagged 0x14");
            if (Animation != nullptr)
                Animation->Release();
        }
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
        TestPipelineTextureId();
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

    // MarshalBuffer's wide strings are UTF-16 units on the wire, as Windows' wchar_t: a
    // character above U+FFFF goes as a surrogate pair, and reads back as one wchar_t.
    {
        using FCtor = void (*)(void*);
        using FUseWrite = void (*)(void*, unsigned char*, int);
        using FUseRead = void (*)(void*, unsigned char const*, int);
        using FPutWide = void (*)(void*, wchar_t const*);
        using FGetWide = wchar_t* (*)(void*);
        auto const Ctor =
            reinterpret_cast<FCtor>(Platform::ModuleSymbol(Common, "_ZN13MarshalBufferC1Ev"));
        auto const UseWrite = reinterpret_cast<FUseWrite>(
            Platform::ModuleSymbol(Common, "_ZN13MarshalBuffer16use_write_bufferEPhi"));
        auto const UseRead = reinterpret_cast<FUseRead>(
            Platform::ModuleSymbol(Common, "_ZN13MarshalBuffer15use_read_bufferEPKhi"));
        auto const PutWide = reinterpret_cast<FPutWide>(
            Platform::ModuleSymbol(Common, "_ZN13MarshalBuffer11put_wstringEPKw"));
        auto const GetWide = reinterpret_cast<FGetWide>(
            Platform::ModuleSymbol(Common, "_ZN13MarshalBuffer11get_wstringEv"));
        Check(Ctor != nullptr && UseWrite != nullptr && UseRead != nullptr && PutWide != nullptr &&
                  GetWide != nullptr,
              "Common exports MarshalBuffer's wide string calls");
        if (Ctor != nullptr && UseWrite != nullptr && UseRead != nullptr && PutWide != nullptr &&
            GetWide != nullptr)
        {
            alignas(std::max_align_t) unsigned char Buffer[256] = {};
            alignas(std::max_align_t) unsigned char Wire[64] = {};
            Ctor(Buffer);
            UseWrite(Buffer, Wire, sizeof(Wire));
            wchar_t const Text[] = {L'A', static_cast<wchar_t>(0x1F600), 0};
            PutWide(Buffer, Text);
            unsigned char const Expected[] = {3, 0, 0, 0, 'A', 0, 0x3D, 0xD8, 0x00, 0xDE};
            Check(std::memcmp(Wire, Expected, sizeof(Expected)) == 0,
                  "MarshalBuffer::put_wstring writes UTF-16 units");
            Ctor(Buffer);
            UseRead(Buffer, Wire, sizeof(Expected));
            wchar_t* const Read = GetWide(Buffer);
            Check(Read != nullptr && Read[0] == L'A' && Read[1] == static_cast<wchar_t>(0x1F600) &&
                      Read[2] == 0,
                  "MarshalBuffer::get_wstring reads them back");
            free(Read);
        }
    }

    // Server loads (on dacom, DALib and Common) and runs: destroying a phantom it never made
    // reports -2, as retail's does.
    Platform::Module const Server = Platform::ModuleLoad((Folder + "Server.dll").c_str());
    Check(Server != nullptr, "Server.dll loads");
    auto const PhantomDestroy = reinterpret_cast<int (*)(void*)>(
        Platform::ModuleSymbol(Server, "_ZN3pub7Phantom7DestroyEPv"));
    Check(PhantomDestroy != nullptr && PhantomDestroy(nullptr) == -2,
          "Server's pub::Phantom::Destroy");

    // pub::DLL_LoadLibrary takes a path from '/' as it is (Content's StoryLauncher hands it its own
    // module path), and gives nullptr for a file that is not there.
    {
        auto const LoadLibraryFn = reinterpret_cast<Platform::Module (*)(const char*)>(
            Platform::ModuleSymbol(Server, "_ZN3pub15DLL_LoadLibraryEPKc"));
        Check(LoadLibraryFn != nullptr, "Server exports pub::DLL_LoadLibrary");
        char ServerPath[0x400] = {};
        Platform::ModulePath(Server, ServerPath, sizeof(ServerPath));
        if (LoadLibraryFn != nullptr)
        {
            Platform::Module const Again = LoadLibraryFn(ServerPath);
            Check(ServerPath[0] == '/' && Again == Server,
                  "pub::DLL_LoadLibrary loads an absolute POSIX path as it is");
            Platform::ModuleFree(Again);
            Check(LoadLibraryFn("/nonexistent/Missing.dll") == nullptr,
                  "pub::DLL_LoadLibrary gives nullptr for a missing file");
            std::string const Overlong = "/" + std::string(0x200, 'a') + "/Missing.dll";
            Check(LoadLibraryFn(Overlong.c_str()) == nullptr,
                  "pub::DLL_LoadLibrary refuses an absolute path longer than MAX_PATH");
        }
    }

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
