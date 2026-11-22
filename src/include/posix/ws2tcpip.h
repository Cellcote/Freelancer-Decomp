// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for <ws2tcpip.h> on macOS and Linux (src/include/posix, non-Windows
// only). getaddrinfo, freeaddrinfo, inet_pton and socklen_t are the C library's, which
// posix/winsock2.h includes.

#pragma once

#if defined(_WIN32)
#error "posix/ws2tcpip.h is the stand-in for macOS and Linux; Windows uses the SDK's ws2tcpip.h"
#endif

#include <winsock2.h>

typedef struct addrinfo ADDRINFOA;
typedef struct addrinfo* PADDRINFOA;
