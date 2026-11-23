# PortTest: checks of the port's own platform headers on Linux and macOS -- PORT(macOS), not part
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
# dacom at load time too: it holds the process's platform state (FlPlatformShared), which the
# test uses before it loads the other modules, so test and modules share one.
target_link_libraries(module_tests PRIVATE ${CMAKE_DL_LIBS} dacom)
target_include_directories(module_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include/posix
                           ${CMAKE_SOURCE_DIR}/src/include)
target_compile_options(module_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       "-D__declspec(x)=" -D__cdecl= -D__stdcall= -Wall -Wextra -Wno-unknown-pragmas)
# Loaded at run time, by name, as the game loads them: built first, not linked.
add_dependencies(module_tests dacom system x86math ReadFile zlib luaprofile Thorn EngBase
                 ximage Shading RendComp flmaterials alchemy Deformable2 DALib Common Server Content imeui
                 RemoteServer arena_probe_a arena_probe_b)
# RPGPU when the build has it (FreelancerRpGpu, SDL3): module_tests loads it and, with SDL3 linked
# as the client links it (one shared SDL), hands it an SDL window.
if(TARGET RPGPU)
  add_dependencies(module_tests RPGPU)
  target_compile_definitions(module_tests PRIVATE FL_TEST_RPGPU)
  target_link_libraries(module_tests PRIVATE SDL3::SDL3-shared)
endif()
add_test(NAME module_tests COMMAND module_tests $<TARGET_FILE_DIR:dacom>)

# dalib_net_tests: DALib's CDPClient and CDPServer over loopback through DPNet (DPNetAdapter.cpp):
# enumeration, connect, both ways, both ends disconnecting. It subclasses them, so it links DALib.
add_executable(dalib_net_tests dalib_net_tests.cpp)
target_compile_features(dalib_net_tests PRIVATE cxx_std_20)
target_link_libraries(dalib_net_tests PRIVATE DALib dacom)
target_include_directories(dalib_net_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include/posix
                           ${CMAKE_SOURCE_DIR}/src/include)
target_compile_options(dalib_net_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       "-D__declspec(x)=" -D__cdecl= -D__stdcall= -Wall -Wextra -Wno-unknown-pragmas)
add_test(NAME dalib_net_tests COMMAND dalib_net_tests)

# user32_tests: the client's stand-ins (src/include/posix) and the user32 module
# (src/PosixUser32), built and linked as a game module is: against the stand-in <windows.h>,
# with user32 and dacom (the platform layer's state).
add_executable(user32_tests user32_tests.cpp)
target_compile_features(user32_tests PRIVATE cxx_std_20)
target_link_libraries(user32_tests PRIVATE ${CMAKE_DL_LIBS} PosixUser32 dacom)
target_include_directories(user32_tests PRIVATE ${CMAKE_SOURCE_DIR}/src/include/posix
                           ${CMAKE_SOURCE_DIR}/src/include)
target_compile_options(user32_tests PRIVATE -include ${CMAKE_SOURCE_DIR}/src/include/fl_layout.h
                       "-D__declspec(x)=" -D__cdecl= -D__stdcall= -Wall -Wextra -Wno-unknown-pragmas)
# Where user32_sdl is built, the screen checks look at its SDL windows and queue SDL events
# through user32_screen_probe (the same shared SDL), and fail if a window gets no screen.
if(TARGET PosixUser32Sdl)
  add_library(user32_screen_probe STATIC user32_screen_probe.cpp)
  target_compile_features(user32_screen_probe PRIVATE cxx_std_20)
  target_link_libraries(user32_screen_probe PUBLIC SDL3::SDL3-shared)
  target_link_libraries(user32_tests PRIVATE user32_screen_probe)
  target_compile_definitions(user32_tests PRIVATE FL_USER32_SCREEN_EXPECTED=1)
  add_dependencies(user32_tests PosixUser32Sdl)
endif()
add_test(NAME user32_tests COMMAND user32_tests)
# The stand-ins without a screen: windows stay records even where user32_sdl is built.
set_tests_properties(user32_tests PROPERTIES ENVIRONMENT FL_USER32_HEADLESS=1)
# The same stand-ins with a screen: SDL's dummy video driver, which needs no display, whatever
# FL_USER32_HEADLESS the shell has (skipped when user32_sdl is not built).
add_test(NAME user32_screen_tests COMMAND user32_tests --screen)
set_tests_properties(user32_screen_tests PROPERTIES ENVIRONMENT
                     "SDL_VIDEO_DRIVER=dummy;FL_USER32_HEADLESS=0")
