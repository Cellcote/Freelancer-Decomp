// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): MSVC's <malloc.h> on macOS and Linux (src/include/posix, non-Windows only):
// malloc and free, and _alloca as alloca.

#pragma once

#include <alloca.h>
#include <stdlib.h>

#define _alloca alloca
