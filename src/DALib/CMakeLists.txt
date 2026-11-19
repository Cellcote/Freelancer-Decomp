# CMake build of DALib.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

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

target_compile_definitions(DALib PRIVATE DALIB_EXPORTS ZLIB_WINAPI)
# %(AdditionalIncludeDirectories);..\zlib\vendor: after src\include, not before it.
target_include_directories(DALib PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${FL_SRC_DIR}/include> ../zlib/vendor)
fl_link(DALib LIBS dacom.lib zlib.lib winmm.lib REFERENCES dacom zlib)
