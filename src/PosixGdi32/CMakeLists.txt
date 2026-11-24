# gdi32 on macOS and Linux -- PORT(macOS), not part of retail; built only off Windows. See
# src/include/posix/wingdi.h. Every module that uses the stand-in <windows.h> links it
# (cmake/Freelancer.cmake), as MSVC links gdi32.lib into every module by default. TrueType
# rasterising is stb_truetype's (vendor/, compiled into gdi32.cpp).

fl_add_module(PosixGdi32 DLL PORT OUTPUT_NAME gdi32
  SOURCES
    gdi32.cpp
)
