# Building

The build is CMake with Ninja and 32-bit MSVC. `CMakeLists.txt` at the repo root lists the
modules; each one has a `CMakeLists.txt` in its folder under `src\`, and `cmake/Freelancer.cmake`
holds the compiler, resource-compiler and linker options they share. Everything goes into `bin\`
(`bin_debug\`, `binasan\`), next to the runtime configuration.

## Requirements

- Visual Studio 2022 with the *Desktop development with C++* workload, *C++ MFC for latest v143
  build tools (x86 & x64)* (FLServer) and *C++ CMake tools for Windows* (CMake 3.27 or later and
  Ninja).
- The DirectX 8.1 SDK in `dx8\` (README.md), or `-DDx8SdkDir=<path>`.
- Python 3 on `PATH` for Debug and DebugASan (the client's post-build step copies the dev `*.ini`).
- RP9 only: the DirectX SDK (June 2010), found through `%DXSDK_DIR%` or `-DDx9SdkDir=<path>`.

## Building

From an **x86** Developer Command Prompt (`vcvarsall.bat x86`), at the repo root:

```
cmake --preset release
cmake --build --preset release
```

| Preset | Output | |
|---|---|---|
| `release` | `bin\` | |
| `debug` | `bin_debug\` | copies the dev `*.ini` from `bin\` with the paths rewritten (`src/copy_dev_ini.py`) |
| `debugasan` | `binasan\` | AddressSanitizer; also copies the `clang_rt.asan_*` runtime next to the exe |

Each preset has its own build tree under `build\`. One module: `cmake --build --preset release
--target Common`. In Visual Studio, *File > Open > Folder* on the repo root uses the same presets.

Only the Ninja generator is supported: the Visual Studio generators would turn the options into
MSBuild properties, and MSBuild adds its own defaults on top. The configure step stops with an
error for that, and for anything but a 32-bit MSVC toolchain.

## Switches

Port-only features, `-D<Name>=0|1` at configure time (for example
`cmake --preset release -DFreelancerRp9=1`):

| Switch | Default | |
|---|---|---|
| `FreelancerWidescreenSupport` | 1 | Widescreen (Hor+) display modes and HUD; 0 builds retail's 4:3 client |
| `FreelancerWalkMode` | 1 | Walking through base rooms ([walk_mode.md](walk_mode.md)) |
| `FreelancerRp9` | 0 | `RP9.dll` and the client's Direct3D 9 hooks ([re/rp9.md](re/rp9.md)) |
| `FreelancerBetaLabel` | 0 | The "Beta 2" label in the corner of the screen |
| `FreelancerEnableTickProfile` | 0 | On-screen frame profiler |

## The options, and why they are what they are

`cmake/Freelancer.cmake` spells every option out instead of taking CMake's MSVC defaults; the set
is the one of the Visual Studio solution this build replaced, which was checked equal binary for
binary before the solution was removed. The ones that matter for fidelity:

- **x87 floating point** (`/arch:IA32 /fp:precise`), as the VC6-built retail modules: Direct3D 8
  drops the main thread's x87 precision to 24-bit and never restores it, and x87 code inherits
  that where SSE2 code would not. A few files differ on purpose (`fl_item()` in alchemy, Thorn and
  the client's `alchemy_float_forward.cpp`).
- **`_ITERATOR_DEBUG_LEVEL=1` in Release** keeps the std containers retail-sized: the layout
  `static_assert`s in `src/include` hold only with it (or with Debug's level 2).
- **No `NDEBUG`**: asserts stay on in Release.
- **No `/sdl`** in modules that export classes (Common, Server, DALib, the client, ...): it adds
  exported `__autoclassinit2` members that retail does not export (`NO_SDL`).
- **Link order**: objects are linked in the order of the `SOURCES` list, which is also the order of
  static initialisation (see the note on `vis_effect_arch.cpp` in `src/freelancer/CMakeLists.txt`);
  libraries in the order `fl_link()` names them.
- **DX8 headers after the Windows SDK**: `dx8\include` is appended to cl's `INCLUDE`, not passed
  as `/I`, so the SDK's `basetsd.h` wins.
- Per-file settings (`fl_item()`) replace the module-wide ones instead of adding to them: `/RTC1`
  with `/O2` is an error, and `/GL` cannot be taken back.

## Adding a module or a source file

- A source file goes into its module's `SOURCES` list; where it goes there is its place in the link
  order.
- A module is a folder with a `CMakeLists.txt` (`fl_add_module()`, `fl_link()`, see
  `cmake/Freelancer.cmake`), added to `FL_PROJECTS` in the root `CMakeLists.txt` after every
  module it links.
- A resource-only DLL from a retail dump: `src/Resources/import_decompiled_rc.py` writes its
  `CMakeLists.txt` ([re/resources.md](re/resources.md)).
