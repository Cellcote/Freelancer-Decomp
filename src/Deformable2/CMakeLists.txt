# CMake build of Deformable2.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(Deformable2 DLL
  SOURCES
    Deformable2.cpp
    EyeMaterial.cpp
    Deformable2.rc
)

fl_link(Deformable2 LIBS dacom.lib REFERENCES dacom)
