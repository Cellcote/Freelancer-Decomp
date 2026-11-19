# CMake build of RemoteServer.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(RemoteServer DLL NO_SDL OUTPUT_NAME remoteserver
  SOURCES
    RemoteServer_main.cpp
    rpc_server_channel_receive.cpp
    IServerImpl.cpp
    rpc_server_packets.cpp
    ../RemoteCommon/remote_packet_codecs.cpp
    ../RemoteCommon/remote_server_channel_codecs.cpp
)

target_include_directories(RemoteServer PRIVATE ../include)
target_compile_definitions(RemoteServer PRIVATE REMOTESERVER_EXPORTS FL_CLIENT _WINDOWS _USRDLL)
fl_link(RemoteServer
  LIBS kernel32.lib user32.lib dacom.lib Common.lib Server.lib freelancer.lib rpclocal.lib DALib.lib
  REFERENCES dacom freelancer Common DALib rpclocal Server)
