// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for <mstcpip.h> on macOS and Linux (src/include/posix, non-Windows
// only). SIO_UDP_CONNRESET, the one control code posix/winsock2.h's WSAIoctl knows, is defined
// there.

#pragma once

#if defined(_WIN32)
#error "posix/mstcpip.h is the stand-in for macOS and Linux; Windows uses the SDK's mstcpip.h"
#endif

#include <winsock2.h>
