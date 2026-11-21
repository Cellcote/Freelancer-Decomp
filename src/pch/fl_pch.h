// Precompiled header for the largest projects (Common, freelancer, Content), forced in front of
// every source file by Directory.Build.targets. Build-time only: it holds just the Windows SDK and
// standard library headers those files already include (dacom.h, at the top of nearly every one,
// includes <windows.h>), so including them first changes no code. Never put project headers here.
// A file that has to set up macros before <windows.h> (WIN32_LEAN_AND_MEAN, NOMINMAX) opts out in
// Directory.Build.targets.
#pragma once

#include <windows.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include <list>
#include <map>
#include <string>
#include <vector>
