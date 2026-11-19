# CMake build of EngBase.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(EngBase DLL
  SOURCES
    EngBase.cpp
    EngBaseMixer.cpp
    EngBase.rc
)

fl_link(EngBase LIBS dacom.lib REFERENCES dacom)
