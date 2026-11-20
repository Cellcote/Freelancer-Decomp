# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(system DLL
  SOURCES
    system.cpp
    system.rc
)

fl_includes(system ../include)
fl_link(system LIBS dacom.lib REFERENCES dacom)
