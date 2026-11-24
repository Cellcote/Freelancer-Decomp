# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

# Win32: rc.exe and the linker. macOS and Linux: tools/build_resource_dll.py makes the same DLL.
# x64 Windows builds neither.
if(NOT FL_WINDOWS)
  fl_add_generated_resource_dll(EquipResources EquipResources.rc)
  return()
endif()
if(NOT FL_WIN32)
  return()
endif()

fl_add_module(EquipResources RESOURCE_DLL RETAIL
  SOURCES
    EquipResources.rc
)
