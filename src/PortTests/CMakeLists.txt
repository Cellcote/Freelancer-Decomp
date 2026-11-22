# PortTests: checks of the port's own platform headers on Linux and macOS -- PORT(macOS), not part
# of retail. Built only by the non-Windows build (CMakeLists.txt), with ctest.

add_executable(port_tests port_tests.cpp)
target_compile_features(port_tests PRIVATE cxx_std_20)
target_include_directories(port_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include)
# fl_layout.h is forced into every source, as in the Windows build.
target_compile_options(port_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       -Wall -Wextra)
add_test(NAME port_tests COMMAND port_tests)

# module_tests: loads the client modules this build makes and checks that their DllMains ran,
# and ReadFile's DOS file system on this disk. Built against the stand-in <windows.h> (src/include/posix), as they are.
add_executable(module_tests module_tests.cpp)
target_compile_features(module_tests PRIVATE cxx_std_20)
target_include_directories(module_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include/posix
                           ${CMAKE_SOURCE_DIR}/src/include)
target_compile_options(module_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       "-D__declspec(x)=" -D__cdecl= -D__stdcall= -Wall -Wextra -Wno-unknown-pragmas)
# Loaded at run time, by name, as the game loads them: built first, not linked.
add_dependencies(module_tests dacom system x86math ReadFile zlib luaprofile Thorn)
add_test(NAME module_tests COMMAND module_tests $<TARGET_FILE_DIR:dacom>)
