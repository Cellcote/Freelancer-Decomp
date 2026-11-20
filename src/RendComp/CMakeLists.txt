# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

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
