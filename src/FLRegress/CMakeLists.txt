# CMake build of FLRegress.vcxproj (see cmake/Freelancer.cmake): the port-only headless
# regression harness. Keep the source list in the .vcxproj's order.

fl_add_module(FLRegress EXE CONSOLE
  SOURCES
    FLRegress.cpp
    regress_client.cpp
    regress_dump.cpp
)

fl_includes(FLRegress ${CMAKE_CURRENT_SOURCE_DIR} ../include)
fl_defines(FLRegress _CONSOLE)
fl_link(FLRegress
  LIBS dacom.lib DALib.lib Common.lib Server.lib ole32.lib
  REFERENCES dacom DALib Common Server)
