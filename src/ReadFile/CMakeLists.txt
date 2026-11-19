# CMake build of ReadFile.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(ReadFile DLL
  SOURCES
    DacomStricmp.cpp
    ReadFile.cpp
    ReadFile.rc
)

fl_link(ReadFile LIBS dacom.lib REFERENCES dacom)
