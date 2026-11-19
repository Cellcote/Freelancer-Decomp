# CMake build of MiscText.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(MiscText RESOURCE_DLL
  SOURCES
    MiscText.rc
)
