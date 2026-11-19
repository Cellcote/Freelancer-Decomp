# CMake build of x86math.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(x86math DLL
  SOURCES
    x86math.cpp
    dacom_stricmp.cpp
    x86math.rc
)

fl_link(x86math LIBS dacom.lib REFERENCES dacom)
