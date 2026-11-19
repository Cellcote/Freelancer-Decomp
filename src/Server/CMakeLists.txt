# CMake build of Server.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(Server DLL NO_SDL
  SOURCES
    IServerImpl.cpp
    server_pub.cpp
    server_classes.cpp
    server_chat.cpp
    server_gameobject.cpp
    server_observer.cpp
    server_createinfo.cpp
    server_savegame.cpp
    server_savefile.cpp
    server_fuse.cpp
    server_loot.cpp
    server_mine.cpp
    server_guided.cpp
    server_countermeasure.cpp
    server_basedb.cpp
)

target_compile_definitions(Server PRIVATE _WINDOWS _USRDLL SERVER_EXPORTS)
fl_link(Server
  LIBS kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib
       ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib Common.lib
  REFERENCES dacom DALib Common)
