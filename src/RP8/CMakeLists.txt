# CMake build of RP8.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(RP8 DLL NO_SAFESEH
  SOURCES
    RP8.cpp
    Mgsdb.cpp
    RP8.rc
)

fl_includes(RP8 ../include)
# DX8.1 SDK d3dx8.lib objects carry -defaultlib:libci (the VC6 single-threaded iostream library),
# which no current toolset ships; RP8 does not use iostreams. The SDK's lib dir comes before the
# Windows SDK's (its ddraw.lib), as AdditionalLibraryDirectories. d3dx8.lib has no SafeSEH tables.
target_link_options(RP8 PRIVATE /NODEFAULTLIB:libci.lib)
target_link_directories(RP8 BEFORE PRIVATE "${FL_DX8_DIR}/lib")
fl_link(RP8 LIBS dacom.lib ddraw.lib d3d8.lib d3dx8.lib legacy_stdio_definitions.lib REFERENCES dacom)
