# CMake build of ximage.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(ximage DLL
  SOURCES
    ximage.cpp
    ximage.rc
)

fl_includes(ximage ../include)
fl_link(ximage LIBS dacom.lib user32.lib REFERENCES dacom)
