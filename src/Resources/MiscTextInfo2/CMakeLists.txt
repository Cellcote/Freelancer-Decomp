# CMake build of MiscTextInfo2.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(MiscTextInfo2 RESOURCE_DLL
  SOURCES
    MiscTextInfo2.rc
)
