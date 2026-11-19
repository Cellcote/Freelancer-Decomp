# CMake build of InfoCards.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(InfoCards RESOURCE_DLL
  SOURCES
    InfoCards.rc
)
