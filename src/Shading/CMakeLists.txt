# CMake build of Shading.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(Shading DLL
  SOURCES
    Shading.cpp
    Material.cpp
    MaterialLibrary.cpp
    MaterialRegex.cpp
    MaterialBatcher.cpp
    TextureLibrary.cpp
    MaterialAnimation.cpp
    Shading.rc
)

fl_link(Shading LIBS dacom.lib REFERENCES dacom)
