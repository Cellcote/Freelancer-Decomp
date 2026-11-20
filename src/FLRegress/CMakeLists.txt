# FLRegress.exe: port-only headless regression harness (no retail counterpart). It starts
# Server.dll as the single-player client does and dumps the game state to a text file.
# See cmake/Freelancer.cmake.

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
