# CMake build of qhull.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(qhull LIB NO_SDL WARNING_LEVEL 1
  SOURCES
    vendor/geom.c
    vendor/geom2.c
    vendor/global.c
    vendor/io.c
    vendor/mem.c
    vendor/merge.c
    vendor/poly.c
    vendor/poly2.c
    vendor/qhull.c
    vendor/qset.c
    vendor/stat.c
    vendor/user.c
)

# qhull 2002.1 (vendored under vendor/), see qhull.vcxproj and vendor/COPYING.txt.
target_compile_definitions(qhull PRIVATE _CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_DEPRECATE)
target_include_directories(qhull PRIVATE vendor)
target_compile_options(qhull PRIVATE
  /wd4013 /wd4018 /wd4028 /wd4090 /wd4101 /wd4113 /wd4131 /wd4133 /wd4142 /wd4152 /wd4189
  /wd4244 /wd4267 /wd4305 /wd4306 /wd4700 /wd4701 /wd4996)
