# user32 on macOS and Linux -- PORT(macOS), not part of retail; built only off Windows. See
# src/include/posix/winuser.h. Every module that uses the stand-in <windows.h> links it
# (cmake/Freelancer.cmake), as MSVC links user32.lib into every module by default.

fl_add_module(PosixUser32 DLL PORT OUTPUT_NAME user32
  SOURCES
    user32.cpp
)

# user32_sdl: the screen behind user32's top-level windows (user32_backend.h), over SDL3 as a
# shared library (one SDL per process: RPGPU draws into these windows through the same one).
# user32 loads it when the first top-level window is made, so it is not a link dependency of
# anything: built when SDL3 is found (Homebrew's sdl3 on macOS, a system or source build on
# Linux); without it windows stay records.
# SDL 3.2 or later, as RPGPU takes it (the root CMakeLists.txt).
find_package(SDL3 3.2 CONFIG QUIET COMPONENTS SDL3-shared)
if(SDL3_FOUND AND TARGET SDL3::SDL3-shared)
  fl_add_module(PosixUser32Sdl DLL PORT PORTABLE OUTPUT_NAME user32_sdl
    SOURCES
      user32_sdl.cpp
  )
  target_link_libraries(PosixUser32Sdl PRIVATE SDL3::SDL3-shared)
  # Built with user32, so a build of user32 (or anything that links it) has its screen.
  add_dependencies(PosixUser32 PosixUser32Sdl)
else()
  message(STATUS "user32_sdl: SDL3 not found; windows have no screen in this build")
endif()
