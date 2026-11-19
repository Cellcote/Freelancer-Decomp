# CMake build of rpclocal.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(rpclocal DLL
  SOURCES
    rpclocal.cpp
    rpc_transport.cpp
    rpc_packet_registry.cpp
    rpc_client_proxy.cpp
    rpc_dispatch.cpp
    rpc_payload_ch1.cpp
    rpc_payload_ch2.cpp
    rpc_payload_ch3.cpp
    rpc_containers.cpp
    rpc_wire_ch1.cpp
    rpc_wire_ch2.cpp
    rpc_wire_generic.cpp
    rpc_wire_fields.cpp
    rpc_wire_varlen.cpp
    rpc_wire_closeout.cpp
    rpc_wire_ch3.cpp
    rpc_packet_dump.cpp
)

target_include_directories(rpclocal PRIVATE ../include)
target_compile_definitions(rpclocal PRIVATE _WINDOWS _USRDLL RPCLOCAL_EXPORTS)
fl_link(rpclocal LIBS kernel32.lib user32.lib Common.lib REFERENCES dacom Common)
