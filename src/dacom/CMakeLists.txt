# CMake build of dacom.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(dacom DLL DEF dacom.def
  SOURCES
    dacom.cpp
    dacom.rc
)

fl_link(dacom)
