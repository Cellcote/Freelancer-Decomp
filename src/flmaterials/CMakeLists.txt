# CMake build of flmaterials.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(flmaterials DLL
  SOURCES
    flmaterials.cpp
    MaterialImpl.cpp
    FLAppearance.cpp
    flmaterials.rc
)

fl_includes(flmaterials ../include)
fl_link(flmaterials LIBS dacom.lib REFERENCES dacom)
