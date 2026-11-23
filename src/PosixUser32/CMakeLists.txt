# user32 on macOS and Linux -- PORT(macOS), not part of retail; built only off Windows. See
# src/include/posix/winuser.h. Every module that uses the stand-in <windows.h> links it
# (cmake/Freelancer.cmake), as MSVC links user32.lib into every module by default.

fl_add_module(PosixUser32 DLL PORT OUTPUT_NAME user32
  SOURCES
    user32.cpp
)
