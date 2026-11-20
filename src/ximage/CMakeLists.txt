# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(ximage DLL
  SOURCES
    ximage.cpp
    ximage.rc
)

fl_includes(ximage ../include)
fl_link(ximage LIBS dacom.lib user32.lib REFERENCES dacom)
