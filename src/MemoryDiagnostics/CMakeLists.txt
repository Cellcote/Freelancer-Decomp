# See cmake/Freelancer.cmake. The source list is the link order, and with it the order of
# static initialisation.

fl_add_module(MemoryDiagnostics EXE UNICODE NO_WPO NO_INCREMENTAL WARNING_LEVEL 4
  ITEM_DEFAULTS DebugInformationFormat ProgramDatabase
  SOURCES
    main.cpp
    diagnostics.cpp
)

fl_defines(MemoryDiagnostics UNICODE _UNICODE)
target_compile_options(MemoryDiagnostics PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/utf-8>)
fl_link(MemoryDiagnostics
  LIBS user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib comdlg32.lib psapi.lib)
