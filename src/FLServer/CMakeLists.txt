# CMake build of FLServer.vcxproj (see cmake/Freelancer.cmake). Keep the source list in the .vcxproj's
# order: it is the link order, and with it the static-initialisation order.

fl_add_module(FLServer EXE EXPORTS MFC NO_SDL DEF FLServer.def
  SOURCES
    FLServer.cpp
    MainFrm.cpp
    FLServerDoc.cpp
    FLServerView.cpp
    CntrItem.cpp
    ShutdownDlg.cpp
    ServerControl.cpp
    RichEditText.cpp
    Motd.cpp
    AreYouSureDlg.cpp
    ErrorDlg.cpp
    FLListBox.cpp
    AccountsDlg.cpp
    OperationCompleteDlg.cpp
    ServerNewsDlg.cpp
    SetPasswordDlg.cpp
    EditAccountDlg.cpp
    ChoosePlayerDlg.cpp
    SelectStarSystemDlg.cpp
    ServerConfigDlg.cpp
    CreateAccountDlg.cpp
    ServerInfoDlg.cpp
    ChatRdl.cpp
    ChatAccountDlg.cpp
    ChatSystemDlg.cpp
    ResourceStrings.cpp
    StarSystemList.cpp
    UniverseChatDlg.cpp
    FLSummaryForm.cpp
    FLServerCmdLine.cpp
    CrashReport.cpp
    FLServer.rc
)

# Retail FLServer.exe is an MFC42 doc/view application: dynamic MFC, multibyte character set.
# Every translation unit includes StdAfx.h first; no precompiled header.
target_include_directories(FLServer PRIVATE $<$<COMPILE_LANGUAGE:CXX>:${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/../include>)
target_compile_definitions(FLServer PRIVATE $<$<COMPILE_LANGUAGE:CXX>:_WINDOWS>)
target_include_directories(FLServer PRIVATE $<$<COMPILE_LANGUAGE:RC>:${CMAKE_CURRENT_SOURCE_DIR}/res>)
target_compile_definitions(FLServer PRIVATE $<$<COMPILE_LANGUAGE:RC>:_AFXDLL>)
fl_link(FLServer
  LIBS dacom.lib DALib.lib Common.lib Server.lib RemoteClient.lib imm32.lib winmm.lib
  REFERENCES dacom DALib Common Server RemoteClient)
fl_order_after(FLServer serverresources)
