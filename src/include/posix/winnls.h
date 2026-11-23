// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the Windows SDK's <winnls.h> on macOS and Linux
// (src/include/posix, non-Windows only). The national-language calls are in the stand-in
// <windows.h> (posix/kernel32.h), as the SDK's <windows.h> brings this header in.

#pragma once

#if defined(_WIN32)
#error "posix/winnls.h is the stand-in for macOS and Linux; Windows uses the Windows SDK's"
#endif

#include <windows.h>
