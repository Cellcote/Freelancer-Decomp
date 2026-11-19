# CMake build of zlib.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(zlib DLL NO_SDL DEF zlib.def
  SOURCES
    vendor/adler32.c
    vendor/compress.c
    vendor/crc32.c
    vendor/deflate.c
    vendor/gzclose.c
    vendor/gzlib.c
    vendor/gzread.c
    vendor/gzwrite.c
    vendor/inffast.c
    vendor/inflate.c
    vendor/inftrees.c
    vendor/trees.c
    vendor/uncompr.c
    vendor/zutil.c
    vendor/minizip/ioapi.c
    vendor/minizip/unzip.c
    vendor/minizip/zip.c
    zlib_version_compat.c
    zlib.rc
)

target_compile_definitions(zlib PRIVATE ZLIB_WINAPI _CRT_NONSTDC_NO_DEPRECATE)
target_include_directories(zlib PRIVATE vendor vendor/minizip)
target_compile_options(zlib PRIVATE $<$<COMPILE_LANGUAGE:C>:/wd4244 /wd4267 /wd4996>)
fl_link(zlib)
