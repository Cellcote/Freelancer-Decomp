# PortTests: checks of the port's own platform headers on Linux and macOS -- PORT(macOS), not part
# of retail. Built only by the non-Windows build (CMakeLists.txt), with ctest.

add_executable(port_tests port_tests.cpp)
target_compile_features(port_tests PRIVATE cxx_std_20)
# dlopen / dlsym are in libdl before glibc 2.34.
target_link_libraries(port_tests PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(port_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
# fl_layout.h is forced into every source, as in the Windows build.
target_compile_options(port_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       -Wall -Wextra)
add_test(NAME port_tests COMMAND port_tests)

# arena_probe_a / arena_probe_b: two modules that use LowArena, for module_tests (one arena per
# process: dacom's).
foreach(_probe a b)
  add_library(arena_probe_${_probe} SHARED arena_probe.cpp)
  set_target_properties(arena_probe_${_probe} PROPERTIES PREFIX "" LIBRARY_OUTPUT_DIRECTORY "${FL_BIN_DIR}")
  target_compile_features(arena_probe_${_probe} PRIVATE cxx_std_20)
  target_include_directories(arena_probe_${_probe} PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
  target_compile_definitions(arena_probe_${_probe} PRIVATE FL_PROBE_PREFIX=Probe_${_probe}_)
  target_compile_options(arena_probe_${_probe} PRIVATE
                         -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h -Wall -Wextra)
  target_link_libraries(arena_probe_${_probe} PRIVATE dacom)
  if(NOT APPLE)
    target_link_options(arena_probe_${_probe} PRIVATE -Wl,-Bsymbolic-functions)
  endif()
endforeach()

# module_tests: loads the client modules this build makes and checks that their DllMains ran,
# and ReadFile's DOS file system on this disk. Built against the stand-in <windows.h> (src/include/posix), as they are.
add_executable(module_tests module_tests.cpp)
target_compile_features(module_tests PRIVATE cxx_std_20)
target_link_libraries(module_tests PRIVATE ${CMAKE_DL_LIBS})
target_include_directories(module_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include/posix
                           ${CMAKE_SOURCE_DIR}/src/include)
target_compile_options(module_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       "-D__declspec(x)=" -D__cdecl= -D__stdcall= -Wall -Wextra -Wno-unknown-pragmas)
# Loaded at run time, by name, as the game loads them: built first, not linked.
add_dependencies(module_tests dacom system x86math ReadFile zlib luaprofile Thorn EngBase
                 ximage Shading RendComp flmaterials alchemy Deformable2 arena_probe_a arena_probe_b)
add_test(NAME module_tests COMMAND module_tests $<TARGET_FILE_DIR:dacom>)
