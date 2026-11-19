# CMake build

`CMakeLists.txt` at the repo root builds the same modules as `src\Freelancer.sln`, from the
same sources and with the same compiler, resource-compiler and linker options, into the same
`bin\` (`bin_debug\`, `binasan\`). Both builds are maintained side by side. CI builds Release
both ways and checks that the binaries match (see [Parity check](#parity-check)).

## Building

Requirements are the same as for the solution (README.md): Visual Studio 2022 with the C++
workload and MFC, the DirectX 8.1 SDK in `dx8\`, and CMake 3.27 or later with Ninja. The CMake
and Ninja that come with Visual Studio are enough.

From an **x86** Developer Command Prompt (`vcvarsall.bat x86`), at the repo root:

```
cmake --preset release
cmake --build --preset release
```

The presets are `release`, `debug` and `debugasan`. Each one has its own build tree under `build\`.
In Visual Studio, *File > Open > Folder* on the repo root picks up the same presets.

Only the Ninja generator is supported. The Visual Studio generators would turn the options back
into MSBuild properties, and MSBuild would add its own defaults on top, so the build would no
longer be the same. The toolchain must be 32-bit MSVC. The configure step stops with an error
for anything else.

## Options

| Cache variable | Default | MSBuild equivalent |
|---|---|---|
| `Dx8SdkDir` | `<repo>/dx8` | `/p:Dx8SdkDir=` |
| `Dx9SdkDir` | `%DXSDK_DIR%` | `/p:Dx9SdkDir=` (RP9 only) |
| `FreelancerWidescreenSupport`, `FreelancerWalkMode`, `FreelancerRp9`, `FreelancerBetaLabel`, `FreelancerEnableTickProfile` | empty: the value in `src/Freelancer.Diagnostics.props` | `/p:<Name>=0\|1` |

For example: `cmake --preset release -DFreelancerRp9=1`. The property sheet
`src/Freelancer.Diagnostics.props` stays the single place where the defaults are set: CMake reads
it at configure time, and re-configures when it changes.

## How it matches MSBuild

All of this lives in `cmake/Freelancer.cmake`:

- **CMake's own MSVC defaults are cleared** (`/DNDEBUG`, `/D_WINDOWS`, `/GR`, `/Ob2`,
  `/subsystem:console`, the `<target>_EXPORTS` define). MSBuild's v143 defaults are written out
  explicitly instead: `/Oy-`, `/Zc:inline`, `/sdl`, `/GL` + `/LTCG:incremental`, `/SAFESEH`,
  `_WINDLL`, `_MBCS`, and the rest. Release defines no `NDEBUG`, just as in the solution.
- **Item metadata replaces project settings**, as in MSBuild. Per-file settings such as
  `alchemy_math.cpp`'s `/fp:fast /Oi`, or no `/GL` on `ThornControlCalls.cpp`, are resolved per
  source file with `fl_item()`. They are not added after the project's options: `/RTC1` with
  `/O2` is a hard error, and `/GL` cannot be taken back.
- **DX8 headers come after the Windows SDK.** MSBuild appends `dx8\include` to the `INCLUDE`
  environment of cl, not to `/I`. CMake does the same, by starting cl through
  `cmake -E env --modify INCLUDE=path_list_append:...`.
- **Link order is kept.** Objects are linked in the `.vcxproj`'s item order, which is also the
  static-initialisation order (see the comment in `freelancer.vcxproj`). Libraries are linked
  in `AdditionalDependencies` order, then the default libraries, then the project references.
  Import libraries are passed as files, so CMake cannot reorder them by dependency.

## Adding or changing a source file

Change both `src\<project>\<project>.vcxproj` and `src\<project>\CMakeLists.txt`, and keep the
same position in both lists. Per-file settings go in the `.vcxproj` item and in an `fl_item()`
call. If the two builds drift apart, the CI parity check fails and names the file.

## Parity check

`.github/scripts/build_parity.py` (the `cmake` job in `.github/workflows/build.yml`):

- `commands`: compares every cl / rc / link / lib command line MSBuild ran, from the `.tlog`
  files in `src\obj`, with the ones CMake generated (`compile_commands.json`, `build.ninja`). It
  checks that the same sources are compiled with the same options, defines and include order,
  and that each module links the same objects and libraries in the same order. Options that only
  affect diagnostics are listed but do not fail the check.
- `binaries`: compares every `.exe` / `.dll` header field and section, plus the imports and
  exports. Only the debug directory (PDB GUID and path), the timestamps and the checksum may
  differ.

The job builds both into `bin\` in turn, because the PDB path is embedded in the binary. A
different output directory would shift the data after it.
