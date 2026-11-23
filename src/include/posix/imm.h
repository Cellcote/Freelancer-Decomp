// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the Windows SDK's <imm.h> on macOS and Linux (src/include/posix,
// non-Windows only). There is no input method editor here: every keyboard layout answers
// ImmIsIME false and a window has no input context, which is what imm32 tells a Windows system
// without an IME, so imeui stays out of the way. Text input from an IME comes with SDL's text
// input (docs/x64-client-plan.md, "IME").

#pragma once

#if defined(_WIN32)
#error "posix/imm.h is the stand-in for macOS and Linux; Windows uses the Windows SDK's"
#endif

#include <windows.h>

typedef struct HIMC__* HIMC;
typedef struct HIMCC__* HIMCC;

#define GCS_COMPREADSTR 0x0001
#define GCS_COMPREADATTR 0x0002
#define GCS_COMPREADCLAUSE 0x0004
#define GCS_COMPSTR 0x0008
#define GCS_COMPATTR 0x0010
#define GCS_COMPCLAUSE 0x0020
#define GCS_CURSORPOS 0x0080
#define GCS_DELTASTART 0x0100
#define GCS_RESULTREADSTR 0x0200
#define GCS_RESULTREADCLAUSE 0x0400
#define GCS_RESULTSTR 0x0800
#define GCS_RESULTCLAUSE 0x1000

#define IMN_CLOSESTATUSWINDOW 0x0001
#define IMN_OPENSTATUSWINDOW 0x0002
#define IMN_CHANGECANDIDATE 0x0003
#define IMN_CLOSECANDIDATE 0x0004
#define IMN_OPENCANDIDATE 0x0005
#define IMN_SETCONVERSIONMODE 0x0006
#define IMN_SETSENTENCEMODE 0x0007
#define IMN_SETOPENSTATUS 0x0008
#define IMN_PRIVATE 0x000E

#define NI_OPENCANDIDATE 0x0010
#define NI_CLOSECANDIDATE 0x0011
#define NI_SELECTCANDIDATESTR 0x0012
#define NI_CHANGECANDIDATELIST 0x0013
#define NI_COMPOSITIONSTR 0x0015
#define CPS_COMPLETE 0x0001
#define CPS_CONVERT 0x0002
#define CPS_REVERT 0x0003
#define CPS_CANCEL 0x0004

#define ATTR_INPUT 0x00
#define ATTR_TARGET_CONVERTED 0x01
#define ATTR_CONVERTED 0x02
#define ATTR_TARGET_NOTCONVERTED 0x03
#define ATTR_INPUT_ERROR 0x04

#define IMM_ERROR_NODATA (-1)
#define IMM_ERROR_GENERAL (-2)

typedef struct tagCANDIDATELIST
{
    DWORD dwSize;
    DWORD dwStyle;
    DWORD dwCount;
    DWORD dwSelection;
    DWORD dwPageStart;
    DWORD dwPageSize;
    DWORD dwOffset[1];
} CANDIDATELIST, *PCANDIDATELIST, *LPCANDIDATELIST;

inline BOOL ImmIsIME(HKL)
{
    return FALSE;
}
inline UINT ImmGetIMEFileNameA(HKL, LPSTR Buffer, UINT Size)
{
    if (Buffer != nullptr && Size > 0)
        Buffer[0] = 0;
    return 0;
}
inline HIMC ImmGetContext(HWND)
{
    return nullptr;
}
inline BOOL ImmReleaseContext(HWND, HIMC)
{
    return TRUE;
}
inline HIMC ImmAssociateContext(HWND, HIMC)
{
    return nullptr;
}
inline HWND ImmGetDefaultIMEWnd(HWND)
{
    return nullptr;
}
inline LONG ImmGetCompositionStringA(HIMC, DWORD, LPVOID, DWORD)
{
    return IMM_ERROR_GENERAL;
}
inline LONG ImmGetCompositionStringW(HIMC, DWORD, LPVOID, DWORD)
{
    return IMM_ERROR_GENERAL;
}
inline DWORD ImmGetCandidateListA(HIMC, DWORD, LPCANDIDATELIST, DWORD)
{
    return 0;
}
inline DWORD ImmGetCandidateListW(HIMC, DWORD, LPCANDIDATELIST, DWORD)
{
    return 0;
}
inline BOOL ImmGetConversionStatus(HIMC, LPDWORD Conversion, LPDWORD Sentence)
{
    if (Conversion != nullptr)
        *Conversion = 0;
    if (Sentence != nullptr)
        *Sentence = 0;
    return FALSE;
}
inline BOOL ImmGetOpenStatus(HIMC)
{
    return FALSE;
}
inline BOOL ImmNotifyIME(HIMC, DWORD, DWORD, DWORD)
{
    return FALSE;
}
// VK_PROCESSKEY's original key: none without an IME.
inline UINT ImmGetVirtualKey(HWND)
{
    return 0;
}
