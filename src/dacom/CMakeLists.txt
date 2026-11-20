# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(dacom DLL DEF dacom.def
  SOURCES
    dacom.cpp
    dacom.rc
)

fl_link(dacom)
