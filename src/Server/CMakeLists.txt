# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

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

# NO_SDL: retail Server.dll exports no __autoclassinit2, and the module exports exactly the names
# retail exports.
fl_defines(Server _WINDOWS _USRDLL SERVER_EXPORTS)
# dacom: retail Server.dll imports DACOM.DLL::FDUMP at 97 call sites (the thunk at 0x06d64a44).
# DALib: retail imports DALIB.DLL::??1MD5Hash@@UAE@XZ; its own exported MD5Hash deleting
# destructor (md5hash.h, MD5HASH_API) calls DALib's.
fl_link(Server
  LIBS kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib
       ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib Common.lib
  REFERENCES dacom DALib Common)
