# Source layout

One folder per module: a folder with a `CMakeLists.txt` is a module, and builds the binary named
as the folder (`Common` → `Common.dll`). Two folders build several targets instead: `DPNet` (the
`DPNet` library and its `dpnet_tests`; on Windows its sources are compiled into `DALib`) and
`PortTests` (the ctest programs `port_tests` and `module_tests`, and the `arena_probe_a` / `_b`
modules `module_tests` loads).
Its role, its retail counterpart and its document are in
[docs/re/dll_inventory.md](../docs/re/dll_inventory.md); the naming rules for files and folders are
in [docs/re/refactoring.md](../docs/re/refactoring.md#files-and-folders). The root
`CMakeLists.txt` adds the modules in dependency order (`FL_PROJECTS`).

## Modules by layer

A module links only modules of its own layer or of the layers above it in this table. The one
exception is retail's: `RemoteServer.dll` links `freelancer.exe`, whose exports it calls.

| Layer | Modules |
|-------|---------|
| Engine core | `dacom` (object manager; every DACOM module links it), `zlib`, `DALib`, `ReadFile`, `x86math`, `system`, `EngBase`; `DPNet` (DirectPlay 8 protocol, compiled into `DALib` on x64, Linux and macOS) |
| Engine services | rendering: `RP8`, `RP9`, `RPGPU`, `TexConvert`, `RendComp`, `Shading`, `flmaterials`, `alchemy`, `Deformable2`, `ximage`; audio: `SoundManager`, `SoundStreamer`; scripts: `Thorn`, `luaprofile`; `movie`, `imeui` |
| Game | `qhull` (static, into `Common`), `Common` (shared game runtime), `Server` (the world), `Content` (missions, AI, populators) |
| Network | `rpclocal` (single-player connection), `RemoteServer` (runs in the client), `RemoteClient` (runs in the server) |
| Programs | `freelancer` (the client), `FLServer`, `FLDedicated`; `Resources/*` (resource-only DLLs) |
| Tests and tools | `FLRegress`, `RpcCodecTest`, `TexConvertTest`, `DPNetCheck`, `MemoryDiagnostics`, `PortTests` |

## Shared source folders

These folders have no `CMakeLists.txt`; their files are compiled into other modules.

| Folder | Compiled into |
|--------|---------------|
| `include` | every module: the cross-module ABI (`<module>_*.h`) and the platform layer (`platform.h`, `posix/`) |
| `IVP` | `Common` (the physics engine, as in retail) |
| `RemoteCommon` | `RemoteClient`, `RemoteServer`, `RpcCodecTest` (the packet codecs) |
| `ServerHost` | `FLServer`, `FLDedicated` |
| `pch` | the precompiled header of `Common`, `freelancer` and `Content` |

`vcpkg.json` is the one vcpkg manifest of the port-only modules (the root `CMakeLists.txt` points
vcpkg at this folder).
