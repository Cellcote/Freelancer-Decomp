# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(NameResources RESOURCE_DLL
  SOURCES
    NameResources.rc
)
