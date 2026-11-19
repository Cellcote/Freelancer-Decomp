# Freelancer Decomp: CMake build next to src\Freelancer.sln.
#
# This module re-creates, flag for flag, what MSBuild (platform toolset v143) passes to cl, rc,
# lib and link for the projects in src\. MSBuild's own defaults (Microsoft.Cpp.Default.props,
# Microsoft.Cl.Common.props, Microsoft.Link.Common.props) are written out here explicitly,
# because CMake's MSVC defaults differ: CMake adds /DNDEBUG, /D_WINDOWS, /GR, /Ob2,
# <target>_EXPORTS and /subsystem:console, and drops /Oy-, /Zc:inline, /SAFESEH and /sdl,
# none of which match. src\Directory.Build.props and src\Directory.Build.targets are the
# MSBuild side of everything below.
#
# .github/scripts/build_parity.py compares the two builds command line by command line and
# binary by binary; CI runs it on every push.

include_guard(GLOBAL)

set(FL_SRC_DIR "${PROJECT_SOURCE_DIR}/src")

# ---------------------------------------------------------------------------------------------
# Configurations: the three of Freelancer.sln, with MSBuild's output directories.
# ---------------------------------------------------------------------------------------------
set(FL_CONFIGS Release Debug DebugASan)
get_property(_fl_multi GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_fl_multi)
  set(CMAKE_CONFIGURATION_TYPES ${FL_CONFIGS} CACHE STRING "" FORCE)
else()
  if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Release, Debug or DebugASan" FORCE)
  endif()
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS ${FL_CONFIGS})
  if(NOT CMAKE_BUILD_TYPE IN_LIST FL_CONFIGS)
    message(FATAL_ERROR "CMAKE_BUILD_TYPE must be one of ${FL_CONFIGS} (got '${CMAKE_BUILD_TYPE}')")
  endif()
endif()

# Repo-root siblings of src\, as Directory.Build.props: every ..\DATA / ..\EXE path the game
# resolves from its exe directory works from all three.
set(FL_BIN_DIR "${PROJECT_SOURCE_DIR}/$<IF:$<CONFIG:Debug>,bin_debug,$<IF:$<CONFIG:DebugASan>,binasan,bin>>")

set(_fl_cfg_debug "$<CONFIG:Debug>")
set(_fl_cfg_asan "$<CONFIG:DebugASan>")
set(_fl_cfg_release "$<CONFIG:Release>")
set(_fl_cfg_debuglibs "$<CONFIG:Debug,DebugASan>")

# ---------------------------------------------------------------------------------------------
# Toolchain checks: everything is 32-bit MSVC, and the Visual Studio generators are out because
# they would hand the flags back to MSBuild (which then adds its own defaults on top).
# ---------------------------------------------------------------------------------------------
if(NOT MSVC OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
  message(FATAL_ERROR "Freelancer builds with MSVC (cl.exe) only.")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Freelancer is 32-bit only: configure from an x86 Developer Command Prompt "
                      "(vcvarsall.bat x86), or use the presets in CMakePresets.json.")
endif()
if(CMAKE_GENERATOR MATCHES "^Visual Studio")
  message(FATAL_ERROR "Use the Ninja generator (see CMakePresets.json); the Visual Studio solution "
                      "is src\\Freelancer.sln.")
endif()

# ---------------------------------------------------------------------------------------------
# SDK locations and build switches, under the same names as the MSBuild properties.
# ---------------------------------------------------------------------------------------------
set(Dx8SdkDir "${PROJECT_SOURCE_DIR}/dx8" CACHE PATH "DirectX 8.1 SDK (holds include/ and lib/)")
set(Dx9SdkDir "$ENV{DXSDK_DIR}" CACHE PATH "DirectX SDK (June 2010), RP9 only")
file(TO_CMAKE_PATH "${Dx8SdkDir}" FL_DX8_DIR)
file(TO_CMAKE_PATH "${Dx9SdkDir}" FL_DX9_DIR)
string(REGEX REPLACE "/+$" "" FL_DX9_DIR "${FL_DX9_DIR}")
if(NOT EXISTS "${FL_DX8_DIR}/include/d3d8.h" OR NOT EXISTS "${FL_DX8_DIR}/lib/d3d8.lib")
  message(FATAL_ERROR "DirectX 8.1 SDK not found under '${FL_DX8_DIR}' (needs include/d3d8.h and "
                      "lib/d3d8.lib). Copy it to dx8/ at the repo root or pass -DDx8SdkDir=<path>.")
endif()

# The switches of src\Freelancer.Diagnostics.props (the VS property page). The sheet stays the one
# place that sets them: it is read here, and -D<Name>=0|1 overrides it like /p:<Name>=0|1 does.
set(_fl_switches FreelancerEnableTickProfile FreelancerBetaLabel FreelancerWidescreenSupport
                 FreelancerRp9 FreelancerWalkMode)
# src\Directory.Build.props: the value for projects that do not import the sheet.
set(_fl_default_FreelancerEnableTickProfile 0)
set(_fl_default_FreelancerBetaLabel 0)
set(_fl_default_FreelancerWidescreenSupport 1)
set(_fl_default_FreelancerRp9 0)
set(_fl_default_FreelancerWalkMode 0)
file(READ "${FL_SRC_DIR}/Freelancer.Diagnostics.props" _fl_props)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${FL_SRC_DIR}/Freelancer.Diagnostics.props")
foreach(_name IN LISTS _fl_switches)
  if(_fl_props MATCHES "<${_name}>([01])</${_name}>")
    set(_fl_sheet_${_name} "${CMAKE_MATCH_1}")
  else()
    message(FATAL_ERROR "src/Freelancer.Diagnostics.props does not set ${_name}")
  endif()
  set(${_name} "" CACHE STRING "0 or 1; empty takes src/Freelancer.Diagnostics.props")
  set_property(CACHE ${_name} PROPERTY STRINGS "" 0 1)
  if(NOT ${_name} STREQUAL "" AND NOT ${_name} MATCHES "^[01]$")
    message(FATAL_ERROR "${_name} must be 0, 1 or empty (got '${${_name}}')")
  endif()
endforeach()

# fl_switch(<out> <name> <imports_sheet>): the value MSBuild evaluates for a project that does
# (Common, freelancer, RP9) or does not import Freelancer.Diagnostics.props.
function(fl_switch out name imports_sheet)
  if(NOT ${name} STREQUAL "")
    set(${out} "${${name}}" PARENT_SCOPE)
  elseif(imports_sheet)
    set(${out} "${_fl_sheet_${name}}" PARENT_SCOPE)
  else()
    set(${out} "${_fl_default_${name}}" PARENT_SCOPE)
  endif()
endfunction()

foreach(_name IN LISTS _fl_switches)
  fl_switch(_value ${_name} TRUE)
  message(STATUS "${_name}=${_value}")
endforeach()

# ---------------------------------------------------------------------------------------------
# Global flags: cleared, so that only what fl_add_module() sets reaches the tools. Normal
# variables shadow the cache, so -DCMAKE_CXX_FLAGS=... cannot quietly make the builds differ.
# ---------------------------------------------------------------------------------------------
foreach(_lang C CXX RC)
  set(CMAKE_${_lang}_FLAGS "")
  set(CMAKE_${_lang}_STANDARD_LIBRARIES "")
  foreach(_cfg ${FL_CONFIGS} MinSizeRel RelWithDebInfo)
    string(TOUPPER "${_cfg}" _cfg)
    set(CMAKE_${_lang}_FLAGS_${_cfg} "")
  endforeach()
endforeach()
foreach(_kind EXE SHARED MODULE STATIC)
  set(CMAKE_${_kind}_LINKER_FLAGS "")
  foreach(_cfg ${FL_CONFIGS} MinSizeRel RelWithDebInfo)
    string(TOUPPER "${_cfg}" _cfg)
    set(CMAKE_${_kind}_LINKER_FLAGS_${_cfg} "")
  endforeach()
endforeach()

# The subsystem comes from fl_add_module (/SUBSYSTEM:WINDOWS or CONSOLE, as MSBuild writes it).
foreach(_lang C CXX)
  set(CMAKE_${_lang}_CREATE_WIN32_EXE "")
  set(CMAKE_${_lang}_CREATE_CONSOLE_EXE "")
endforeach()

# RuntimeLibrary (Directory.Build.props): /MD, /MDd for Debug and DebugASan.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<${_fl_cfg_debuglibs}:Debug>DLL")
# /Zi or /ZI is an item setting here (see _fl_item_options), not a target one.
set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "")

# The DirectX 8.1 headers go after the Windows SDK, not before it (the SDK's basetsd.h must win
# over the DX8 one): MSBuild appends them to IncludePath, i.e. to the INCLUDE environment of cl.
# A /I option would search them first, so cl is started through `cmake -E env` with the same
# INCLUDE edit. Directory.Build.targets does this for every project.
set(FL_DX8_INCLUDE_LAUNCHER
    "${CMAKE_COMMAND}" -E env "--modify" "INCLUDE=path_list_append:${FL_DX8_DIR}/include")
set(CMAKE_C_COMPILER_LAUNCHER ${FL_DX8_INCLUDE_LAUNCHER})
set(CMAKE_CXX_COMPILER_LAUNCHER ${FL_DX8_INCLUDE_LAUNCHER})

# ---------------------------------------------------------------------------------------------
# Compile settings that a .vcxproj item may override (ClCompile metadata). MSBuild resolves each
# one per source file: the item's value, else the project's ItemDefinitionGroup, else the
# default; one cl option comes out of it. Appending a per-file option to a project-wide one is
# not the same (/RTC1 with /O2 is an error, /GL cannot be taken back), so these options are set
# on every source file, never on the target. The names are the MSBuild metadata names.
# ---------------------------------------------------------------------------------------------
set(_fl_item_keys Optimization IntrinsicFunctions FloatingPointModel EnableEnhancedInstructionSet
                  BasicRuntimeChecks DebugInformationFormat SupportJustMyCode
                  WholeProgramOptimization SDLCheck)

# MSBuild's value for a configuration before any project or item setting (Microsoft.Cl.Common.props
# for v143 / Win32, with WholeProgramOptimization=true in Release and Directory.Build.props).
function(_fl_item_default out key cfg)
  # UseDebugLibraries: true for Debug and DebugASan.
  set(_debuglibs TRUE)
  if(cfg STREQUAL "Release")
    set(_debuglibs FALSE)
  endif()
  if(key STREQUAL "Optimization")
    if(_debuglibs)
      set(_v Disabled)
    else()
      set(_v MaxSpeed)
    endif()
  elseif(key STREQUAL "IntrinsicFunctions")
    set(_v false)
  elseif(key STREQUAL "FloatingPointModel")
    set(_v Precise)
  elseif(key STREQUAL "EnableEnhancedInstructionSet")
    set(_v NoExtensions)
  elseif(key STREQUAL "BasicRuntimeChecks")
    if(_debuglibs)
      set(_v EnableFastChecks)
    else()
      set(_v Default)
    endif()
  elseif(key STREQUAL "DebugInformationFormat")
    # /ZI is the Win32 default with UseDebugLibraries; DebugASan's /Zi is set in _fl_finalize.
    if(_debuglibs)
      set(_v EditAndContinue)
    else()
      set(_v ProgramDatabase)
    endif()
  elseif(key STREQUAL "SupportJustMyCode")
    if(_debuglibs)
      set(_v true)
    else()
      set(_v false)
    endif()
  elseif(key STREQUAL "WholeProgramOptimization")
    if(_debuglibs)
      set(_v false)
    else()
      set(_v true)
    endif()
  elseif(key STREQUAL "SDLCheck")
    set(_v true)
  endif()
  set(${out} "${_v}" PARENT_SCOPE)
endfunction()

function(_fl_item_option out key value)
  set(_map_Optimization_MaxSpeed /O2)
  set(_map_Optimization_Disabled /Od)
  set(_map_Optimization_MinSpace /O1)
  set(_map_Optimization_Full /Ox)
  set(_map_IntrinsicFunctions_true /Oi)
  set(_map_IntrinsicFunctions_false "")
  set(_map_FloatingPointModel_Precise /fp:precise)
  set(_map_FloatingPointModel_Fast /fp:fast)
  set(_map_FloatingPointModel_Strict /fp:strict)
  set(_map_EnableEnhancedInstructionSet_NoExtensions /arch:IA32)
  set(_map_EnableEnhancedInstructionSet_StreamingSIMDExtensions /arch:SSE)
  set(_map_EnableEnhancedInstructionSet_StreamingSIMDExtensions2 /arch:SSE2)
  set(_map_BasicRuntimeChecks_EnableFastChecks /RTC1)
  set(_map_BasicRuntimeChecks_Default "")
  set(_map_DebugInformationFormat_EditAndContinue /ZI)
  set(_map_DebugInformationFormat_ProgramDatabase /Zi)
  set(_map_DebugInformationFormat_OldStyle /Z7)
  set(_map_DebugInformationFormat_None "")
  set(_map_SupportJustMyCode_true /JMC)
  set(_map_SupportJustMyCode_false "")
  set(_map_WholeProgramOptimization_true /GL)
  set(_map_WholeProgramOptimization_false "")
  set(_map_SDLCheck_true /sdl)
  set(_map_SDLCheck_false /sdl-)
  if(NOT DEFINED _map_${key}_${value})
    message(FATAL_ERROR "No cl option for ${key}=${value}")
  endif()
  set(${out} "${_map_${key}_${value}}" PARENT_SCOPE)
endfunction()

# fl_item(<files...> [<Key> <Value>]... [INCLUDE_DIRECTORIES <dirs...>])
#   ClCompile metadata of single items, e.g.
#   fl_item(alchemy_math.cpp Optimization MaxSpeed BasicRuntimeChecks Default)
#   INCLUDE_DIRECTORIES is the item's AdditionalIncludeDirectories.
function(fl_item)
  set(_files)
  set(_args ${ARGN})
  while(_args)
    list(GET _args 0 _a)
    if(_a IN_LIST _fl_item_keys OR _a STREQUAL "INCLUDE_DIRECTORIES")
      break()
    endif()
    list(APPEND _files "${_a}")
    list(REMOVE_AT _args 0)
  endwhile()
  while(_args)
    list(POP_FRONT _args _key)
    if(_key STREQUAL "INCLUDE_DIRECTORIES")
      list(TRANSFORM _args PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/" REGEX "^[^/$]")
      set_property(SOURCE ${_files} APPEND PROPERTY INCLUDE_DIRECTORIES ${_args})
      break()
    endif()
    list(POP_FRONT _args _value)
    _fl_item_option(_check ${_key} "${_value}")
    set_property(SOURCE ${_files} PROPERTY FL_${_key} "${_value}")
  endwhile()
endfunction()

# Applied once the project's CMakeLists.txt is done, so that fl_item() calls after
# fl_add_module() count.
function(_fl_finalize name)
  # Directory.Build.props: src\include after the project's own AdditionalIncludeDirectories
  # (they are prepended to it). A project that puts it earlier names it itself (DALib).
  target_include_directories(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${FL_SRC_DIR}/include>")

  get_target_property(_sources ${name} SOURCES)
  foreach(_src IN LISTS _sources)
    if(NOT _src MATCHES "\\.(c|cpp)$")
      continue()
    endif()
    set(_per_cfg)
    foreach(_cfg IN LISTS FL_CONFIGS)
      set(_opts)
      foreach(_key IN LISTS _fl_item_keys)
        _fl_item_default(_v ${_key} ${_cfg})
        get_property(_t_set TARGET ${name} PROPERTY FL_${_key} SET)
        if(_t_set)
          get_property(_v TARGET ${name} PROPERTY FL_${_key})
        endif()
        # Directory.Build.targets, after the projects: DebugASan has no /RTC and /Zi.
        if(_cfg STREQUAL "DebugASan" AND _key STREQUAL "BasicRuntimeChecks")
          set(_v Default)
        elseif(_cfg STREQUAL "DebugASan" AND _key STREQUAL "DebugInformationFormat")
          set(_v ProgramDatabase)
        endif()
        get_property(_s_set SOURCE "${_src}" PROPERTY FL_${_key} SET)
        if(_s_set)
          get_property(_v SOURCE "${_src}" PROPERTY FL_${_key})
        endif()
        _fl_item_option(_o ${_key} "${_v}")
        list(APPEND _opts ${_o})
      endforeach()
      list(JOIN _opts "\;" _opts)
      list(APPEND _per_cfg "$<$<CONFIG:${_cfg}>:${_opts}>")
    endforeach()
    set_property(SOURCE "${_src}" APPEND PROPERTY COMPILE_OPTIONS ${_per_cfg})
  endforeach()
endfunction()

# ---------------------------------------------------------------------------------------------
# fl_add_module(<name> <DLL|EXE|LIB|RESOURCE_DLL>
#               [OUTPUT_NAME <name>]       TargetName, when it is not the project name
#               [NO_SDL]                   <SDLCheck>false</SDLCheck>
#               [NO_WPO]                   no <WholeProgramOptimization>true (Release /GL, /LTCG)
#               [UNICODE]                  <CharacterSet>Unicode (default MultiByte)
#               [MFC]                      <UseOfMfc>Dynamic
#               [CONSOLE]                  <SubSystem>Console (default Windows)
#               [NO_SAFESEH]               <ImageHasSafeExceptionHandlers>false
#               [NO_INCREMENTAL]           <LinkIncremental>false in every configuration
#               [EXPORTS]                  an exe that exports names (and so has an import library)
#               [IMPORTS_DIAGNOSTICS_PROPS] the project imports Freelancer.Diagnostics.props
#               [WARNING_LEVEL <n>]        default 3
#               [DEF <file.def>]           ModuleDefinitionFile
#               [ITEM_DEFAULTS <Key> <Value>...]  ClCompile ItemDefinitionGroup (see fl_item)
#               SOURCES <files...>)        compile items in the .vcxproj's order (= link order)
#
# The <name> is the .vcxproj name; MSBuild's $(MSBuildProjectName) checks use it.
# ---------------------------------------------------------------------------------------------
function(fl_add_module name kind)
  cmake_parse_arguments(PARSE_ARGV 2 A
    "NO_SDL;NO_WPO;UNICODE;MFC;CONSOLE;NO_SAFESEH;NO_INCREMENTAL;IMPORTS_DIAGNOSTICS_PROPS;EXPORTS"
    "OUTPUT_NAME;WARNING_LEVEL;DEF" "SOURCES;ITEM_DEFAULTS")
  if(A_UNPARSED_ARGUMENTS)
    message(FATAL_ERROR "fl_add_module(${name}): unknown arguments ${A_UNPARSED_ARGUMENTS}")
  endif()
  if(NOT DEFINED A_WARNING_LEVEL)
    set(A_WARNING_LEVEL 3)
  endif()

  if(kind STREQUAL "EXE")
    add_executable(${name} ${A_SOURCES})
  elseif(kind STREQUAL "DLL" OR kind STREQUAL "RESOURCE_DLL")
    add_library(${name} SHARED ${A_SOURCES})
  elseif(kind STREQUAL "LIB")
    add_library(${name} STATIC ${A_SOURCES})
  else()
    message(FATAL_ERROR "fl_add_module(${name}): unknown kind ${kind}")
  endif()
  # CMake would define <name>_EXPORTS in a DLL or an exe with exports; MSBuild defines no such
  # symbol.
  set_property(TARGET ${name} PROPERTY DEFINE_SYMBOL "")
  if(A_OUTPUT_NAME)
    set_property(TARGET ${name} PROPERTY OUTPUT_NAME "${A_OUTPUT_NAME}")
  endif()

  # OutDir: bin\ (bin_debug\, binasan\) for the module, its import library and its PDB, as
  # MSBuild. Intermediate files stay in the CMake build tree.
  set_target_properties(${name} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
    ARCHIVE_OUTPUT_DIRECTORY "${FL_BIN_DIR}"
    PDB_OUTPUT_DIRECTORY "${FL_BIN_DIR}")

  if(kind STREQUAL "RESOURCE_DLL")
    _fl_resource_dll(${name})
    return()
  endif()

  # Project-wide ClCompile settings that items may override.
  if(A_NO_SDL)
    set_property(TARGET ${name} PROPERTY FL_SDLCheck false)
  endif()
  if(A_NO_WPO)
    set_property(TARGET ${name} PROPERTY FL_WholeProgramOptimization false)
  endif()
  while(A_ITEM_DEFAULTS)
    list(POP_FRONT A_ITEM_DEFAULTS _key _value)
    _fl_item_option(_check ${_key} "${_value}")
    set_property(TARGET ${name} PROPERTY FL_${_key} "${_value}")
  endwhile()
  # DEFER evaluates its arguments when it runs: bake the name in now.
  cmake_language(EVAL CODE "cmake_language(DEFER CALL _fl_finalize [[${name}]])")

  # Preprocessor: Directory.Build.props, Directory.Build.targets and the implicit MSBuild ones
  # (_WINDLL for a DynamicLibrary, _MBCS / _UNICODE;UNICODE from CharacterSet, _AFXDLL from
  # UseOfMfc=Dynamic). No NDEBUG and no _WINDOWS: MSBuild adds neither.
  fl_switch(_tick FreelancerEnableTickProfile ${A_IMPORTS_DIAGNOSTICS_PROPS})
  set(_defs WIN32 _CRT_SECURE_NO_WARNINGS "FREELANCER_ENABLE_TICK_PROFILE=${_tick}"
            "$<${_fl_cfg_release}:_ITERATOR_DEBUG_LEVEL=1>" "$<${_fl_cfg_asan}:FL_ASAN>")
  if(kind STREQUAL "DLL")
    list(APPEND _defs _WINDLL)
  endif()
  if(A_UNICODE)
    set(_charset _UNICODE UNICODE)
  else()
    set(_charset _MBCS)
  endif()
  list(APPEND _defs ${_charset})
  if(A_MFC)
    list(APPEND _defs _AFXDLL)
  endif()
  target_compile_definitions(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_defs}>")

  # The rest of the cl options MSBuild passes (the per-item ones come from _fl_finalize).
  set(_cl
    /W${A_WARNING_LEVEL} /WX- /diagnostics:column /Oy- /Gm- /EHsc
    "$<${_fl_cfg_asan}:/fsanitize=address>"
    /GS /Zc:wchar_t /Zc:forScope /Zc:inline
    "$<$<COMPILE_LANGUAGE:CXX>:/std:c++17>"
    /external:W3 /Gd /analyze- /FC /errorReport:prompt)
  target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_cl}>")

  # rc: /l 0x0409 (Culture), the project directory, and the CharacterSet/MFC defines.
  target_compile_options(${name} PRIVATE "$<$<COMPILE_LANGUAGE:RC>:/l0x0409;/nologo>")
  target_compile_definitions(${name} PRIVATE "$<$<COMPILE_LANGUAGE:RC>:${_charset}>")
  target_include_directories(${name} PRIVATE "$<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}>")

  set(_wpo "$<AND:${_fl_cfg_release},$<NOT:$<BOOL:${A_NO_WPO}>>>")
  if(kind STREQUAL "LIB")
    set_property(TARGET ${name} APPEND PROPERTY STATIC_LIBRARY_OPTIONS
      /MACHINE:X86 "$<${_wpo}:/LTCG>" /NOLOGO)
    return()
  endif()

  # link: Release is /INCREMENTAL:NO with /SAFESEH; Debug links incrementally; DebugASan does not
  # (Directory.Build.targets).
  if(A_CONSOLE)
    set(_subsystem CONSOLE)
  else()
    set(_subsystem WINDOWS)
  endif()
  if(A_NO_INCREMENTAL)
    set(_incremental /INCREMENTAL:NO)
  else()
    set(_incremental "$<IF:${_fl_cfg_debug},/INCREMENTAL,/INCREMENTAL:NO>")
  endif()
  if(kind STREQUAL "EXE")
    set(_uac "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
  else()
    set(_uac "/MANIFESTUAC:NO")
  endif()
  if(A_NO_SAFESEH)
    set(_safeseh /SAFESEH:NO)
  else()
    set(_safeseh "$<${_fl_cfg_release}:/SAFESEH>")
  endif()
  target_link_options(${name} PRIVATE
    /MANIFEST "$<${_wpo}:/LTCG:incremental>" /NXCOMPAT /DYNAMICBASE /DEBUG /MACHINE:X86
    ${_safeseh} ${_incremental} /SUBSYSTEM:${_subsystem} "${_uac}" /ERRORREPORT:PROMPT /NOLOGO
    /TLBID:1)
  # AdditionalLibraryDirectories: $(OutDir) for every project (Directory.Build.props).
  target_link_directories(${name} PRIVATE "${FL_BIN_DIR}")

  if(A_DEF)
    get_filename_component(_def "${A_DEF}" ABSOLUTE)
    target_link_options(${name} PRIVATE "/DEF:${_def}")
    set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "${_def}")
  endif()
  if(A_EXPORTS)
    # An exe with exports gets an import library next to it, as MSBuild.
    set_property(TARGET ${name} PROPERTY ENABLE_EXPORTS ON)
  endif()
endfunction()

# Resource-only DLLs: src\Resources\ResourceDll.props (no code, no CRT, no entry point).
function(_fl_resource_dll name)
  set_property(TARGET ${name} PROPERTY LINKER_LANGUAGE CXX)
  # The .rc scripts are UTF-8 (/c65001).
  target_compile_options(${name} PRIVATE /l0x0409 /nologo /c65001)
  target_compile_definitions(${name} PRIVATE _MBCS)
  target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_options(${name} PRIVATE
    /MANIFEST /NXCOMPAT /DYNAMICBASE /MACHINE:X86 /NOENTRY /NODEFAULTLIB
    "$<${_fl_cfg_release}:/SAFESEH>" "$<IF:${_fl_cfg_debug},/INCREMENTAL,/INCREMENTAL:NO>"
    /SUBSYSTEM:WINDOWS /MANIFESTUAC:NO /ERRORREPORT:PROMPT /NOLOGO /TLBID:1)
  target_link_libraries(${name} PRIVATE ${FL_DEFAULT_LIBS})
endfunction()

# The CoreLibraryDependencies MSBuild puts in place of %(AdditionalDependencies).
set(FL_DEFAULT_LIBS kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib
                    shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib)

# fl_link(<name> [LIBS <AdditionalDependencies...>] [REFERENCES <projects...>])
#
# LIBS is the project's AdditionalDependencies before %(AdditionalDependencies), in order: an
# entry that names a project (dacom.lib, Common.lib, ...) links that project's import library,
# anything else is a system or SDK library. MSBuild then adds the default libraries and the
# import library of every ProjectReference not already named; REFERENCES lists the project
# references.
#
# The link order of libraries decides which one resolves a symbol two of them define, so it is
# kept exactly: project libraries go in as files ($<TARGET_LINKER_FILE>), which CMake leaves in
# place, where linking the targets would let CMake reorder them by dependency. The build order
# comes from add_dependencies, the relink from LINK_DEPENDS.
function(fl_link name)
  cmake_parse_arguments(PARSE_ARGV 1 A "" "" "LIBS;REFERENCES")
  set(_items)
  foreach(_lib IN LISTS A_LIBS)
    get_filename_component(_base "${_lib}" NAME_WE)
    if(NOT IS_ABSOLUTE "${_lib}" AND TARGET "${_base}")
      list(APPEND _items "@${_base}")
    else()
      list(APPEND _items "${_lib}")
    endif()
  endforeach()
  list(APPEND _items ${FL_DEFAULT_LIBS})
  foreach(_ref IN LISTS A_REFERENCES)
    if(NOT TARGET "${_ref}")
      message(FATAL_ERROR "fl_link(${name}): ${_ref} is not a project")
    endif()
    list(APPEND _items "@${_ref}")
  endforeach()
  list(REMOVE_DUPLICATES _items)
  set(_libs)
  foreach(_item IN LISTS _items)
    if(_item MATCHES "^@(.*)$")
      set(_target "${CMAKE_MATCH_1}")
      list(APPEND _libs "$<TARGET_LINKER_FILE:${_target}>")
      add_dependencies(${name} ${_target})
      set_property(TARGET ${name} APPEND PROPERTY LINK_DEPENDS "$<TARGET_LINKER_FILE:${_target}>")
    else()
      list(APPEND _libs "${_item}")
    endif()
  endforeach()
  target_link_libraries(${name} PRIVATE ${_libs})
endfunction()

# A ProjectReference with LinkLibraryDependencies=false: build order only.
function(fl_order_after name)
  add_dependencies(${name} ${ARGN})
endfunction()

# Debug / DebugASan of the client: the dev *.ini of bin\ copied into the output directory with
# the bin\ paths rewritten, and for DebugASan the AddressSanitizer runtime DLLs
# (Directory.Build.targets).
function(fl_dev_ini_post_build name)
  find_package(Python3 COMPONENTS Interpreter)
  if(NOT Python3_Interpreter_FOUND)
    message(WARNING "Python 3 not found: Debug/DebugASan builds will not copy the dev *.ini")
    return()
  endif()
  add_custom_command(TARGET ${name} POST_BUILD
    COMMAND "$<IF:${_fl_cfg_debuglibs},${Python3_EXECUTABLE};${FL_SRC_DIR}/copy_dev_ini.py;$<TARGET_FILE_DIR:${name}>,${CMAKE_COMMAND};-E;true>"
    COMMAND_EXPAND_LISTS VERBATIM)
  get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
  file(GLOB _asan_rt "${_cl_dir}/clang_rt.asan*_dynamic-i386.dll")
  if(_asan_rt)
    add_custom_command(TARGET ${name} POST_BUILD
      COMMAND "$<IF:${_fl_cfg_asan},${CMAKE_COMMAND};-E;copy_if_different;${_asan_rt};$<TARGET_FILE_DIR:${name}>,${CMAKE_COMMAND};-E;true>"
      COMMAND_EXPAND_LISTS VERBATIM)
  else()
    message(STATUS "AddressSanitizer runtime DLLs not found next to ${CMAKE_CXX_COMPILER}")
  endif()
endfunction()
