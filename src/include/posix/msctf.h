// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the Windows SDK's <msctf.h> on macOS and Linux
// (src/include/posix, non-Windows only). imeui only asks it for the language bar manager, which
// is in the stand-in <ctfutb.h>; there is no Text Services Framework here.

#pragma once

#if defined(_WIN32)
#error "posix/msctf.h is the stand-in for macOS and Linux; Windows uses the Windows SDK's"
#endif

#include <ctfutb.h>
