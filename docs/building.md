# Building

The build is CMake. `CMakeLists.txt` at the repo root lists the modules; each one has a
`CMakeLists.txt` in its folder under `src\`, and `cmake/Freelancer.cmake` holds the compiler,
resource-compiler and linker options they share.

- **Windows** (MSVC, Ninja): every module, for Win32 (the retail reference) or x64
  ([x64.md](x64.md)).
- **Linux and macOS** (GCC or Clang): the *portable* modules only, the ones that use no Windows
  API (today qhull, TexConvert and DPNet with its tests). Every other module is Windows-only
  until the non-Windows port gives it a platform backend; see [Linux and macOS](#linux-and-macos).

## Requirements (Windows)

- Visual Studio 2022 with the *Desktop development with C++* workload, *C++ MFC for latest v143
  build tools (x86 & x64)* (FLServer) and *C++ CMake tools for Windows* (CMake 3.27 or later and
  Ninja).
- The DirectX 8.1 SDK in `dx8\` (README.md), or `-DDx8SdkDir=<path>`.
- Python 3 on `PATH` for every output directory except `bin\` (a post-build step copies the dev
  `*.ini` files, see below).
- RPGPU (on by default on x64): vcpkg. Visual Studio ships it; elsewhere set `VCPKG_ROOT`.
- RP9 only: the DirectX SDK (June 2010), found through `%DXSDK_DIR%` or `-DDx9SdkDir=<path>`.

## Building on Windows

From a Developer Command Prompt of the platform you build, at the repo root. Win32, from an
**x86** prompt (`vcvarsall.bat x86`):

```
cmake --preset release
cmake --build --preset release
```

x64, from an **x64** prompt (`vcvarsall.bat x64`):

```
cmake --preset release-x64
cmake --build --preset release-x64
```

A preset whose platform does not match the prompt's compiler stops the configure step.

| Preset | Platform | Output | |
|---|---|---|---|
| `release` | Win32 | `bin\` | the retail reference build |
| `debug` | Win32 | `bin_debug\` | |
| `debugasan` | Win32 | `binasan\` | AddressSanitizer; also copies the `clang_rt.asan_*` runtime next to the exe |
| `release-x64` | x64 | `bin_x64\` | |
| `debug-x64` | x64 | `bin_x64_debug\` | |

Each preset has its own build tree under `build\`. One module: `cmake --build --preset release
--target Common`. In Visual Studio, *File > Open > Folder* on the repo root offers the same
presets; VS Code's CMake Tools does too.

All output directories are siblings at the repo root, so every `..\DATA` / `..\EXE` path the game
resolves from its exe directory works from each of them. Every build into a directory other than
`bin\` copies the dev `*.ini` and character files from `bin\` with the absolute paths rewritten
(`tools/copy_dev_ini.py`): edit the files in `bin\`, not the copies.

Only the Ninja generator is supported: the Visual Studio generators would turn the options into
MSBuild properties, and MSBuild adds its own defaults on top. The configure step stops with an
error for that, and for a toolchain that is neither x86 nor x64 MSVC.

## Switches

Port-only features, `-D<Name>=0|1` at configure time (for example
`cmake --preset release -DFreelancerRp9=1`):

| Switch | Default | |
|---|---|---|
| `FreelancerWidescreenSupport` | 1 | Widescreen (Hor+) display modes and HUD; 0 builds retail's 4:3 client |
| `FreelancerWalkMode` | 1 | Walking through base rooms ([walk_mode.md](walk_mode.md)) |
| `FreelancerRp9` | 0 | `RP9.dll` (Win32 only) and the client's Direct3D 9 hooks ([re/rp9.md](re/rp9.md)) |
| `FreelancerRpGpu` | 1 on x64, 0 on Win32 | `RPGPU.dll`, the SDL3 + SDL_GPU renderer ([re/rpgpu.md](re/rpgpu.md)) |
| `FreelancerBetaLabel` | 0 | The "Beta 2" label in the corner of the screen |
| `FreelancerEnableTickProfile` | 0 | On-screen frame profiler |

## vcpkg

Port-only modules that need third-party packages (today RPGPU, with SDL3) get them from vcpkg:
one manifest, `src/vcpkg.json`, with a feature per module. The root `CMakeLists.txt` turns on the
features of the modules that are on and installs them while it configures (vcpkg's toolchain
file, found through `VCPKG_ROOT`, `VCPKG_INSTALLATION_ROOT` or Visual Studio's copy, unless
`CMAKE_TOOLCHAIN_FILE` is given). In a new build tree with no such module on, vcpkg is not used at
all; a tree first configured with one on keeps vcpkg's toolchain but installs nothing once they
are all off. CMake reads a toolchain file only when a build tree is first configured, so turning
such a module on in a tree configured without vcpkg asks for
`cmake --fresh --preset <preset> -D<Switch>=1`. `--fresh` starts from an empty cache: repeat every
other `-D` option the tree had. Without
vcpkg a module that is on is skipped with a warning, or fails the configure step with
`-DFreelancerRequireVcpkg=ON`, as in CI.

Every module says what it is in its `fl_add_module()`: `RETAIL` (a rebuild of a retail module or
resource DLL), `PORT` (port-only: RP9, RPGPU, FLDedicated, TexConvert) or `TOOL` (FLRegress,
RpcCodecTest, TexConvertTest, DPNetInteropTest, MemoryDiagnostics). A retail module that asks for a
vcpkg package stops the configure step.

## The options, and why they are what they are

`cmake/Freelancer.cmake` spells every option out instead of taking CMake's MSVC defaults. The set
is the one of the Visual Studio solution this build replaced, which was checked equal command
line for command line and binary for binary (Release, Win32 and x64) before the solution was
removed. The ones that matter for fidelity:

- **x87 floating point on Win32** (`/arch:IA32 /fp:precise`), as the VC6-built retail modules:
  Direct3D 8 drops the main thread's x87 precision to 24-bit and never restores it, and x87 code
  inherits that where SSE2 code would not. A few files differ on purpose (`fl_item()` in alchemy,
  Thorn and the client's `alchemy_float_forward.cpp`). x64 always uses SSE2.
- **`_ITERATOR_DEBUG_LEVEL=1` in Release** keeps the std containers retail-sized: the layout
  `static_assert`s in `src/include` hold only with it (or with Debug's level 2).
- **No `NDEBUG`**: asserts stay on in Release.
- **No `/sdl`**: it zero-fills new objects before their constructors run (hiding the
  uninitialised-member behaviour of retail) and gives every exported class an exported
  `__autoclassinit2` that retail does not export.
- **C++23 with `/permissive`**: `/std:c++23preview` (MSVC has no plain `/std:c++23` yet) alone
  implies `/permissive-`; the decompiled sources compile unchanged without it. Use only the C++23
  that every compiler CI builds with supports (see [C++23 in this code](#c23-in-this-code)).
- **Link order**: objects are linked in the order of the `SOURCES` list, which is also the order of
  static initialisation (see the note on `Client/cmn/vis_effect_arch.cpp` in `src/freelancer/CMakeLists.txt`);
  libraries in the order `fl_link()` names them. This is why the source lists are written out
  rather than globbed.
- **DX8 headers after the Windows SDK**: `dx8\include` is appended to cl's `INCLUDE`, not passed
  as `/I`, so the SDK's `basetsd.h` wins.
- Per-file settings (`fl_item()`) replace the module-wide ones instead of adding to them: `/RTC1`
  with `/O2` is an error, and `/GL` cannot be taken back.
- **Precompiled header** (`PCH` in `fl_add_module()`: Common, freelancer and Content):
  `src/pch/fl_pch.h` holds only Windows SDK and standard library headers and is forced in front
  of `fl_layout.h`. It is build-time only, but a header there can still change code, so read the
  note in the file before changing its list. Files that must set up macros before `<windows.h>`
  opt out with `fl_item(<files> PrecompiledHeader NotUsing)`.

## Adding a module or a source file

- A source file goes into its module's `SOURCES` list; where it goes there is its place in the link
  order.
- A module is a folder with a `CMakeLists.txt` (`fl_add_module()`, `fl_link()`, see
  `cmake/Freelancer.cmake`), added to `FL_PROJECTS` in the root `CMakeLists.txt` after every
  module it links. A module for one platform only returns early (`if(NOT FL_WIN32) return()`).
- A resource-only DLL from a retail dump: `src/Resources/import_decompiled_rc.py` writes its
  `CMakeLists.txt` ([re/resources.md](re/resources.md)).

## Linux and macOS

```
cmake --preset portable && cmake --build --preset portable && ctest --preset portable
```

`portable-asan` builds the same in Debug with AddressSanitizer and UndefinedBehaviorSanitizer.
Needs CMake 3.27 or later, Ninja and a C++23 compiler (GCC 13+, Clang 18+, or Apple Clang 17+ from
Xcode 16.3). CI builds both presets on Linux (GCC, Clang) and macOS.

A module is portable when it uses no Windows API: `PORTABLE` in its `fl_add_module()`, and its
folder in the non-Windows `FL_PROJECTS` of the root `CMakeLists.txt`. On these compilers
`fl_add_module()` makes a plain CMake target: the MSVC-only settings (`.rc` and `.def` files,
`fl_item()`, `fl_cl_options()`, linker flags) are ignored, and `fl_layout.h` is forced in as on
Windows. The other modules are being ported to macOS ([x64-client-plan.md](x64-client-plan.md),
phase 6, M2): one that is in `FL_PROJECTS` without `PORTABLE` compiles against the stand-in
`<windows.h>` of `src/include/posix` over the POSIX backend of `src/include/platform.h`
([platform.md](platform.md)). A DLL module is named as on Windows, without a `lib` prefix
(`dacom.dylib`, `dacom.so`), so that `Platform::ModuleLoad("dacom.dll")` finds it.

## C++23 in this code

The code is C++23, but use only what every compiler CI builds with supports: MSVC 17.14
(`/std:c++23preview`), GCC 13 with libstdc++ 13 and Clang 18 (Ubuntu 24.04), and the Apple Clang on
GitHub's `macos-latest` image (Xcode 16, libc++ 19). Older Xcode versions (Apple Clang 15 and 16)
do not build it.

- **Available**: `std::expected`, `std::to_underlying`, `std::byteswap`, `std::unreachable`,
  `string::contains`, `if consteval`, and C++20's `std::bit_cast`, `<bit>`, `std::span`,
  `std::numbers`, `contains()` on associative containers, `std::erase`/`std::erase_if`, the
  `std::ranges` algorithms, C++23's `std::ranges::contains` (in place of a hand-written search
  loop or `std::find(...) != end()`) and the C++20 views (`filter`, `transform`, `reverse`, `iota`).
- **Not yet**: `std::print`, `std::ranges::to` and `ranges::find_last` (libstdc++ 14),
  `std::flat_map`, `[[assume]]` (MSVC). Check the rest of C++23 ranges (`fold_left`,
  `views::zip`, `views::enumerate`) against libstdc++ 13 and libc++ 19 before using it; CI builds
  with both.

In retail code a standard replacement must give the same result for every input on x87, so
some hand-written forms stay:

- `if (a < b) a = b;` on floats is not `std::max`: `std::max` takes `const float&` and so rounds
  an x87 extended-precision operand to float *before* the comparison, while retail compared first.
- `_BitScanReverse` is not `std::countl_zero`: for 0 BSR leaves the index unchanged, while
  `countl_zero(0)` is 32.
- `push_back(T(...))` is not `emplace_back(...)`: the temporary's construction and destruction go
  away.
- `std::sort` is not `std::ranges::sort`: they are separate implementations, and an unstable
  sort may leave equal elements in a different order.
- A literal that is pi rounded to float but written as a `double` (`3.1415927410125732`) is not
  `std::numbers::pi`.

## Warnings, formatting and static analysis

- **Warnings.** On Windows every first-party module builds at `/W4` (`fl_add_module`'s default);
  the vendored zlib and qhull keep their own levels. Headers included with `<...>` (CRT, STL,
  Windows SDK, MFC, DirectX) are external and their warnings are off. Warnings never fail the
  build: CI shows a table per warning and per module on the run page
  (`.github/scripts/warning_summary.py`) and keeps the full build output as an artifact. The
  portable build uses `-Wall -Wextra`.
- **Formatting.** `.clang-format` (clang-format 19) describes the code style. The tree is not
  reformatted in bulk, which would rewrite about a quarter of all lines and break upstream merges:
  CI checks only the lines a pull request changes. Format your own changes with
  `git clang-format origin/master`. Reformat a whole file only in a commit that changes nothing
  else. The modules that put opening braces on the same line (rpclocal, RemoteClient,
  RemoteServer, RemoteCommon) have a `.clang-format` of their own that inherits the root file
  and changes only their local habits (attached braces, `template<`, `{ a, b }`).
- **clang-tidy.** `.clang-tidy` lists bug-finding checks; checks that would push changes to
  retail layout or ownership (`modernize-*` and the like) are left out. Configure with
  `-DFL_CLANG_TIDY=ON` to run it while building (needs clang-tidy 19 or later on `PATH`; on
  macOS with a non-Apple clang-tidy also pass `-DCMAKE_OSX_SYSROOT=$(xcrun --show-sdk-path)`).
  Vendored modules are skipped. CI runs it on the portable modules (Linux, Clang) and reports the
  findings without failing.
