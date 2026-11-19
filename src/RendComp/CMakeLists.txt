# CMake build of RendComp.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(RendComp DLL
  SOURCES
    RendComp.cpp
    RenderManager.cpp
    Plugins.cpp
    TriMesh.cpp
    VMeshLibrary.cpp
    VMeshWire.cpp
    VMeshPart.cpp
    RendComp.rc
)

fl_includes(RendComp ../include)
fl_link(RendComp LIBS dacom.lib REFERENCES dacom)
