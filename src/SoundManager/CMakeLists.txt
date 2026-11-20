# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(SoundManager DLL
  SOURCES
    SoundManager.cpp
    SoundManager.rc
)

# The DX8.1 SDK's dsound.lib, as AdditionalLibraryDirectories.
target_link_directories(SoundManager BEFORE PRIVATE "${FL_DX8_DIR}/lib")
fl_link(SoundManager LIBS dacom.lib ole32.lib dsound.lib msacm32.lib winmm.lib REFERENCES dacom)
