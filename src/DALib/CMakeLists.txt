# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(DALib DLL NO_SDL
  SOURCES
    DALib.cpp
    md5hash.cpp
    CDPMsgList.cpp
    CDPNetCodec.cpp
    CDPClient.cpp
    CDPClientProxy.cpp
    CDPServer.cpp
    CGunWrapper.cpp
    DALib.rc
)

# NO_SDL: retail DALib.dll exports no __autoclassinit2 (dalib_net.h's dllexport classes).
fl_defines(DALib DALIB_EXPORTS ZLIB_WINAPI)
# zlib's headers after src\include, not before it.
fl_includes(DALib ${FL_SRC_DIR}/include ../zlib/vendor)
fl_link(DALib LIBS dacom.lib zlib.lib winmm.lib REFERENCES dacom zlib)
