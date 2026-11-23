# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(imeui DLL RETAIL
  SOURCES
    CCaret.cpp
    CImeUiFont.cpp
    CInputBox.cpp
    IMEInputBox.cpp
    IMEFocus.cpp
    ImeUiCore.cpp
    ImeUiToolbar.cpp
    imeui_dllmain.cpp
    imeui.rc
)

fl_defines(imeui IMEUI_EXPORTS WIN32_LEAN_AND_MEAN)
fl_link(imeui
  LIBS imm32.lib user32.lib gdi32.lib advapi32.lib ole32.lib)

# Off Windows, LoadLibraryA and the rest of kernel32 are the platform layer's, whose
# process-wide state dacom holds (FL_PLATFORM_DEFINE_SHARED), so imeui links dacom there.
if(NOT FL_WINDOWS)
  fl_link(imeui REFERENCES dacom)
endif()
