// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the Windows SDK's <ctfutb.h> on macOS and Linux
// (src/include/posix, non-Windows only): the language bar manager imeui asks COM for, with the
// SDK's vtable order and IID. CoCreateInstance finds no class here (posix/unknwn.h), so the
// interface is only declared.

#pragma once

#if defined(_WIN32)
#error "posix/ctfutb.h is the stand-in for macOS and Linux; Windows uses the Windows SDK's"
#endif

#include <windows.h>

struct ITfLangBarMgr : public IUnknown
{
    STDMETHOD(AdviseEventSink)(void*, HWND, DWORD, DWORD*) PURE;
    STDMETHOD(UnadviseEventSink)(DWORD) PURE;
    STDMETHOD(GetThreadMarshalInterface)(DWORD, DWORD, REFIID, IUnknown**) PURE;
    STDMETHOD(GetThreadLangBarItemMgr)(DWORD, void**, DWORD*) PURE;
    STDMETHOD(GetInputProcessorProfiles)(DWORD, void**, DWORD*) PURE;
    STDMETHOD(RestoreLastFocus)(DWORD*, BOOL) PURE;
    STDMETHOD(SetModalInput)(void*, DWORD, DWORD) PURE;
    STDMETHOD(ShowFloating)(DWORD) PURE;
    STDMETHOD(GetShowFloatingStatus)(DWORD*) PURE;
};

FL_POSIX_UUIDOF(ITfLangBarMgr, 0x87955690, 0xE627, 0x11D2,
                {0x8D, 0xDB, 0x00, 0x10, 0x5A, 0x27, 0x99, 0xB5})
