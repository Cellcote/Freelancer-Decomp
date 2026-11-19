# CMake build of system.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(system DLL
  SOURCES
    system.cpp
    system.rc
)

target_include_directories(system PRIVATE ../include)
fl_link(system LIBS dacom.lib REFERENCES dacom)
