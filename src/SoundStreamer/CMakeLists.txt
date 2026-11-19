# CMake build of SoundStreamer.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(SoundStreamer DLL
  SOURCES
    SoundStreamer.cpp
    SoundStreamer.rc
)

# The DX8.1 SDK's dsound.lib, as AdditionalLibraryDirectories.
target_link_directories(SoundStreamer BEFORE PRIVATE "${FL_DX8_DIR}/lib")
fl_link(SoundStreamer LIBS dacom.lib ole32.lib dsound.lib winmm.lib msacm32.lib REFERENCES dacom)
