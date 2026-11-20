# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(resources RESOURCE_DLL
  SOURCES
    resources.rc
)
