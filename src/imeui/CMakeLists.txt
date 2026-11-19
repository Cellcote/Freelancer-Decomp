# CMake build of imeui.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(imeui DLL
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

target_compile_definitions(imeui PRIVATE IMEUI_EXPORTS)
fl_link(imeui LIBS imm32.lib user32.lib gdi32.lib advapi32.lib ole32.lib)
