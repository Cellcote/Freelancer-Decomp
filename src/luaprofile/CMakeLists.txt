# CMake build of luaprofile.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(luaprofile DLL
  SOURCES
    luaprofile.cpp
    lua32/src/lapi.c
    lua32/src/lauxlib.c
    lua32/src/lbuffer.c
    lua32/src/lbuiltin.c
    lua32/src/ldo.c
    lua32/src/lfunc.c
    lua32/src/lgc.c
    lua32/src/llex.c
    lua32/src/lmem.c
    lua32/src/lobject.c
    lua32/src/lparser.c
    lua32/src/lstate.c
    lua32/src/lstring.c
    lua32/src/ltable.c
    lua32/src/ltm.c
    lua32/src/lundump.c
    lua32/src/lvm.c
    lua32/src/lzio.c
    lua32/lua32_layout.c
    luaprofile.rc
)

# ..\include is Directory.Build.props' src\include, which MSBuild keeps in its last place.
fl_includes(luaprofile lua32/include lua32/src)
target_compile_options(luaprofile PRIVATE $<$<COMPILE_LANGUAGE:C,CXX>:/wd4996>)
# The Lua 3.2 sources (every .c item): C, without /sdl, with their own warnings off.
get_target_property(_lua luaprofile SOURCES)
list(FILTER _lua INCLUDE REGEX "\\.c$")
fl_item(${_lua} SDLCheck false)
set_property(SOURCE ${_lua} APPEND PROPERTY COMPILE_OPTIONS
  /wd4013 /wd4101 /wd4189 /wd4244 /wd4267
  /wd4311 /wd4312 /wd4057 /wd4131 /wd4127 /wd4456 /wd4701 /wd4702 /wd4706 /wd4996)

fl_link(luaprofile LIBS dacom.lib REFERENCES dacom)
