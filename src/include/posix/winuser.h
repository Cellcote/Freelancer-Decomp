// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): user32 on macOS and Linux (src/include/posix, non-Windows only), included by the
// stand-in <windows.h> as the SDK's includes <winuser.h>. The values and layouts are the SDK's.
//
// The calls are implemented once per process by the user32 module (src/PosixUser32): window
// classes, windows (HWND is a small number, never an address), their styles and rectangles,
// the thread's message queue (posted messages, WM_QUIT, sent messages straight to the window
// procedure), focus, capture and the cursor. A window has no screen yet: it is a record with
// its window procedure; SDL gives the top-level windows a screen with M3. Dialog templates are
// in the Windows resource DLLs, which are not read here yet, so DialogBoxParamA fails (-1), as
// Windows does for a template it cannot find. Keyboard text conversions are the C library's.

#pragma once

#if defined(_WIN32)
#error "posix/winuser.h is the stand-in for macOS and Linux; Windows uses the Windows SDK's"
#endif

// What the user32 module exports (default visibility, also from a hidden-visibility build).
#define FL_USER32_API __attribute__((visibility("default")))

typedef LRESULT(CALLBACK* WNDPROC)(HWND, UINT, WPARAM, LPARAM);
typedef INT_PTR(CALLBACK* DLGPROC)(HWND, UINT, WPARAM, LPARAM);
typedef struct HKL__* HKL;
typedef WORD ATOM;

// MAKEINTRESOURCE: a resource id below 0x10000 in a name pointer.
#define IS_INTRESOURCE(Value) ((((ULONG_PTR)(Value)) >> 16) == 0)
#define MAKEINTRESOURCEA(Id) ((LPSTR)((ULONG_PTR)((WORD)(Id))))
#define MAKEINTRESOURCEW(Id) ((LPWSTR)((ULONG_PTR)((WORD)(Id))))
#define MAKEINTRESOURCE MAKEINTRESOURCEA
#define RT_CURSOR MAKEINTRESOURCEA(1)
#define RT_BITMAP MAKEINTRESOURCEA(2)
#define RT_ICON MAKEINTRESOURCEA(3)
#define RT_MENU MAKEINTRESOURCEA(4)
#define RT_DIALOG MAKEINTRESOURCEA(5)
#define RT_STRING MAKEINTRESOURCEA(6)
#define RT_RCDATA MAKEINTRESOURCEA(10)
#define RT_HTML MAKEINTRESOURCEA(23)

// ---------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------

#define WM_NULL 0x0000
#define WM_CREATE 0x0001
#define WM_DESTROY 0x0002
#define WM_MOVE 0x0003
#define WM_SIZE 0x0005
#define WM_ACTIVATE 0x0006
#define WM_SETFOCUS 0x0007
#define WM_KILLFOCUS 0x0008
#define WM_ENABLE 0x000A
#define WM_SETTEXT 0x000C
#define WM_GETTEXT 0x000D
#define WM_PAINT 0x000F
#define WM_CLOSE 0x0010
#define WM_QUIT 0x0012
#define WM_ERASEBKGND 0x0014
#define WM_SHOWWINDOW 0x0018
#define WM_ACTIVATEAPP 0x001C
#define WM_SETCURSOR 0x0020
#define WM_GETMINMAXINFO 0x0024
#define WM_WINDOWPOSCHANGING 0x0046
#define WM_WINDOWPOSCHANGED 0x0047
#define WM_INPUTLANGCHANGEREQUEST 0x0050
#define WM_INPUTLANGCHANGE 0x0051
#define WM_NCCREATE 0x0081
#define WM_NCDESTROY 0x0082
#define WM_NCHITTEST 0x0084
#define WM_NCACTIVATE 0x0086
#define WM_KEYFIRST 0x0100
#define WM_KEYDOWN 0x0100
#define WM_KEYUP 0x0101
#define WM_CHAR 0x0102
#define WM_DEADCHAR 0x0103
#define WM_SYSKEYDOWN 0x0104
#define WM_SYSKEYUP 0x0105
#define WM_SYSCHAR 0x0106
#define WM_SYSDEADCHAR 0x0107
#define WM_KEYLAST 0x0109
#define WM_IME_STARTCOMPOSITION 0x010D
#define WM_IME_ENDCOMPOSITION 0x010E
#define WM_IME_COMPOSITION 0x010F
#define WM_INITDIALOG 0x0110
#define WM_COMMAND 0x0111
#define WM_SYSCOMMAND 0x0112
#define WM_TIMER 0x0113
#define WM_MOUSEFIRST 0x0200
#define WM_MOUSEMOVE 0x0200
#define WM_LBUTTONDOWN 0x0201
#define WM_LBUTTONUP 0x0202
#define WM_LBUTTONDBLCLK 0x0203
#define WM_RBUTTONDOWN 0x0204
#define WM_RBUTTONUP 0x0205
#define WM_RBUTTONDBLCLK 0x0206
#define WM_MBUTTONDOWN 0x0207
#define WM_MBUTTONUP 0x0208
#define WM_MBUTTONDBLCLK 0x0209
#define WM_MOUSEWHEEL 0x020A
#define WM_MOUSELAST 0x020A
#define WM_IME_SETCONTEXT 0x0281
#define WM_IME_NOTIFY 0x0282
#define WM_IME_CONTROL 0x0283
#define WM_USER 0x0400

#define WA_INACTIVE 0
#define WA_ACTIVE 1
#define WA_CLICKACTIVE 2

#define SIZE_RESTORED 0
#define SIZE_MINIMIZED 1
#define SIZE_MAXIMIZED 2

#define SC_SIZE 0xF000
#define SC_MOVE 0xF010
#define SC_MINIMIZE 0xF020
#define SC_MAXIMIZE 0xF030
#define SC_CLOSE 0xF060
#define SC_KEYMENU 0xF100
#define SC_RESTORE 0xF120
#define SC_SCREENSAVE 0xF140
#define SC_MONITORPOWER 0xF170

#define HTERROR (-2)
#define HTNOWHERE 0
#define HTCLIENT 1
#define HTCAPTION 2

#define MK_LBUTTON 0x0001
#define MK_RBUTTON 0x0002
#define MK_SHIFT 0x0004
#define MK_CONTROL 0x0008
#define MK_MBUTTON 0x0010

#define PM_NOREMOVE 0x0000
#define PM_REMOVE 0x0001
#define PM_NOYIELD 0x0002
#define QS_KEY 0x0001
#define QS_MOUSEMOVE 0x0002
#define QS_MOUSEBUTTON 0x0004
#define QS_POSTMESSAGE 0x0008
#define QS_TIMER 0x0010
#define QS_PAINT 0x0020
#define QS_SENDMESSAGE 0x0040
#define QS_HOTKEY 0x0080
#define QS_ALLPOSTMESSAGE 0x0100
#define QS_RAWINPUT 0x0400
#define QS_MOUSE (QS_MOUSEMOVE | QS_MOUSEBUTTON)
#define QS_INPUT (QS_MOUSE | QS_KEY | QS_RAWINPUT)
#define QS_ALLEVENTS (QS_INPUT | QS_POSTMESSAGE | QS_TIMER | QS_PAINT | QS_HOTKEY)
#define QS_ALLINPUT 0x04FF

#define BM_GETCHECK 0x00F0
#define BM_SETCHECK 0x00F1
#define BST_UNCHECKED 0x0000
#define BST_CHECKED 0x0001
#define BN_CLICKED 0

typedef struct tagMSG
{
    HWND hwnd;
    UINT message;
    WPARAM wParam;
    LPARAM lParam;
    DWORD time;
    POINT pt;
} MSG, *PMSG, *LPMSG;

// ---------------------------------------------------------------------------------------------
// Virtual keys (the SDK's)
// ---------------------------------------------------------------------------------------------

#define VK_LBUTTON 0x01
#define VK_RBUTTON 0x02
#define VK_CANCEL 0x03
#define VK_MBUTTON 0x04
#define VK_BACK 0x08
#define VK_TAB 0x09
#define VK_CLEAR 0x0C
#define VK_RETURN 0x0D
#define VK_SHIFT 0x10
#define VK_CONTROL 0x11
#define VK_MENU 0x12
#define VK_PAUSE 0x13
#define VK_CAPITAL 0x14
#define VK_KANA 0x15
#define VK_ESCAPE 0x1B
#define VK_SPACE 0x20
#define VK_PRIOR 0x21
#define VK_NEXT 0x22
#define VK_END 0x23
#define VK_HOME 0x24
#define VK_LEFT 0x25
#define VK_UP 0x26
#define VK_RIGHT 0x27
#define VK_DOWN 0x28
#define VK_SELECT 0x29
#define VK_PRINT 0x2A
#define VK_SNAPSHOT 0x2C
#define VK_INSERT 0x2D
#define VK_DELETE 0x2E
#define VK_HELP 0x2F
#define VK_LWIN 0x5B
#define VK_RWIN 0x5C
#define VK_APPS 0x5D
#define VK_NUMPAD0 0x60
#define VK_NUMPAD1 0x61
#define VK_NUMPAD2 0x62
#define VK_NUMPAD3 0x63
#define VK_NUMPAD4 0x64
#define VK_NUMPAD5 0x65
#define VK_NUMPAD6 0x66
#define VK_NUMPAD7 0x67
#define VK_NUMPAD8 0x68
#define VK_NUMPAD9 0x69
#define VK_MULTIPLY 0x6A
#define VK_ADD 0x6B
#define VK_SEPARATOR 0x6C
#define VK_SUBTRACT 0x6D
#define VK_DECIMAL 0x6E
#define VK_DIVIDE 0x6F
#define VK_F1 0x70
#define VK_F2 0x71
#define VK_F3 0x72
#define VK_F4 0x73
#define VK_F5 0x74
#define VK_F6 0x75
#define VK_F7 0x76
#define VK_F8 0x77
#define VK_F9 0x78
#define VK_F10 0x79
#define VK_F11 0x7A
#define VK_F12 0x7B
#define VK_NUMLOCK 0x90
#define VK_SCROLL 0x91
#define VK_LSHIFT 0xA0
#define VK_RSHIFT 0xA1
#define VK_LCONTROL 0xA2
#define VK_RCONTROL 0xA3
#define VK_LMENU 0xA4
#define VK_RMENU 0xA5
#define VK_OEM_1 0xBA
#define VK_OEM_PLUS 0xBB
#define VK_OEM_COMMA 0xBC
#define VK_OEM_MINUS 0xBD
#define VK_OEM_PERIOD 0xBE
#define VK_OEM_2 0xBF
#define VK_OEM_3 0xC0
#define VK_OEM_4 0xDB
#define VK_OEM_5 0xDC
#define VK_OEM_6 0xDD
#define VK_OEM_7 0xDE
#define VK_PROCESSKEY 0xE5

#define MAPVK_VK_TO_VSC 0
#define MAPVK_VSC_TO_VK 1
#define MAPVK_VK_TO_CHAR 2
#define MAPVK_VSC_TO_VK_EX 3

// ---------------------------------------------------------------------------------------------
// Windows, classes and styles
// ---------------------------------------------------------------------------------------------

#define CS_VREDRAW 0x0001
#define CS_HREDRAW 0x0002
#define CS_DBLCLKS 0x0008
#define CS_OWNDC 0x0020

#define WS_OVERLAPPED 0x00000000L
#define WS_POPUP 0x80000000L
#define WS_CHILD 0x40000000L
#define WS_MINIMIZE 0x20000000L
#define WS_VISIBLE 0x10000000L
#define WS_DISABLED 0x08000000L
#define WS_CLIPSIBLINGS 0x04000000L
#define WS_CLIPCHILDREN 0x02000000L
#define WS_MAXIMIZE 0x01000000L
#define WS_CAPTION 0x00C00000L
#define WS_BORDER 0x00800000L
#define WS_DLGFRAME 0x00400000L
#define WS_SYSMENU 0x00080000L
#define WS_THICKFRAME 0x00040000L
#define WS_MINIMIZEBOX 0x00020000L
#define WS_MAXIMIZEBOX 0x00010000L
#define WS_OVERLAPPEDWINDOW                                                                        \
    (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX)
#define WS_EX_TOPMOST 0x00000008L
#define WS_EX_APPWINDOW 0x00040000L

#define CW_USEDEFAULT ((int)0x80000000)

#define GWL_WNDPROC (-4)
#define GWL_HINSTANCE (-6)
#define GWL_ID (-12)
#define GWL_STYLE (-16)
#define GWL_EXSTYLE (-20)
#define GWL_USERDATA (-21)
#define GWLP_WNDPROC (-4)
#define GWLP_USERDATA (-21)

#define SW_HIDE 0
#define SW_SHOWNORMAL 1
#define SW_NORMAL 1
#define SW_SHOWMINIMIZED 2
#define SW_SHOWMAXIMIZED 3
#define SW_MAXIMIZE 3
#define SW_SHOWNOACTIVATE 4
#define SW_SHOW 5
#define SW_MINIMIZE 6
#define SW_SHOWNA 8
#define SW_RESTORE 9
#define SW_SHOWDEFAULT 10

#define SWP_NOSIZE 0x0001
#define SWP_NOMOVE 0x0002
#define SWP_NOZORDER 0x0004
#define SWP_NOREDRAW 0x0008
#define SWP_NOACTIVATE 0x0010
#define SWP_FRAMECHANGED 0x0020
#define SWP_SHOWWINDOW 0x0040
#define SWP_HIDEWINDOW 0x0080
#define HWND_TOP ((HWND)0)
#define HWND_BOTTOM ((HWND)1)
#define HWND_TOPMOST ((HWND) - 1)
#define HWND_NOTOPMOST ((HWND) - 2)

#define SM_CXSCREEN 0
#define SM_CYSCREEN 1

#define COLOR_WINDOW 5
#define COLOR_BTNFACE 15

// The cursor and icon names (MAKEINTRESOURCE ids).
#define IDC_ARROW MAKEINTRESOURCEA(32512)
#define IDC_IBEAM MAKEINTRESOURCEA(32513)
#define IDC_WAIT MAKEINTRESOURCEA(32514)
#define IDC_CROSS MAKEINTRESOURCEA(32515)
#define IDC_SIZENWSE MAKEINTRESOURCEA(32642)
#define IDC_SIZENESW MAKEINTRESOURCEA(32643)
#define IDC_SIZEWE MAKEINTRESOURCEA(32644)
#define IDC_SIZENS MAKEINTRESOURCEA(32645)
#define IDC_SIZEALL MAKEINTRESOURCEA(32646)
#define IDC_HAND MAKEINTRESOURCEA(32649)
#define IDI_APPLICATION MAKEINTRESOURCEA(32512)

#define IMAGE_ICON 1
#define IMAGE_CURSOR 2
#define LR_SHARED 0x00008000

#define ENUM_CURRENT_SETTINGS ((DWORD) - 1)
#define ENUM_REGISTRY_SETTINGS ((DWORD) - 2)

typedef struct tagWNDCLASSA
{
    UINT style;
    WNDPROC lpfnWndProc;
    int cbClsExtra;
    int cbWndExtra;
    HINSTANCE hInstance;
    HICON hIcon;
    HCURSOR hCursor;
    HBRUSH hbrBackground;
    LPCSTR lpszMenuName;
    LPCSTR lpszClassName;
} WNDCLASSA, *PWNDCLASSA, *LPWNDCLASSA;

typedef struct tagCREATESTRUCTA
{
    LPVOID lpCreateParams;
    HINSTANCE hInstance;
    HMENU hMenu;
    HWND hwndParent;
    int cy;
    int cx;
    int y;
    int x;
    LONG style;
    LPCSTR lpszName;
    LPCSTR lpszClass;
    DWORD dwExStyle;
} CREATESTRUCTA, *LPCREATESTRUCTA;

// The display mode EnumDisplaySettingsA describes (the fields the code reads; the SDK's
// layout up to dmDisplayFrequency).
#define CCHDEVICENAME 32
#define CCHFORMNAME 32
#define DM_BITSPERPEL 0x00040000L
#define DM_PELSWIDTH 0x00080000L
#define DM_PELSHEIGHT 0x00100000L
#define DM_DISPLAYFREQUENCY 0x00400000L
typedef struct _devicemodeA
{
    BYTE dmDeviceName[CCHDEVICENAME];
    WORD dmSpecVersion;
    WORD dmDriverVersion;
    WORD dmSize;
    WORD dmDriverExtra;
    DWORD dmFields;
    POINT dmPosition;
    DWORD dmDisplayOrientation;
    DWORD dmDisplayFixedOutput;
    short dmColor;
    short dmDuplex;
    short dmYResolution;
    short dmTTOption;
    short dmCollate;
    BYTE dmFormName[CCHFORMNAME];
    WORD dmLogPixels;
    DWORD dmBitsPerPel;
    DWORD dmPelsWidth;
    DWORD dmPelsHeight;
    DWORD dmDisplayFlags;
    DWORD dmDisplayFrequency;
    DWORD dmICMMethod;
    DWORD dmICMIntent;
    DWORD dmMediaType;
    DWORD dmDitherType;
    DWORD dmReserved1;
    DWORD dmReserved2;
    DWORD dmPanningWidth;
    DWORD dmPanningHeight;
} DEVMODEA, *LPDEVMODEA;

// ---------------------------------------------------------------------------------------------
// The calls (src/PosixUser32)
// ---------------------------------------------------------------------------------------------

// Classes and windows.
FL_USER32_API ATOM RegisterClassA(const WNDCLASSA* Class);
FL_USER32_API BOOL UnregisterClassA(LPCSTR Name, HINSTANCE Instance);
FL_USER32_API HWND CreateWindowExA(DWORD ExStyle, LPCSTR ClassName, LPCSTR Title, DWORD Style,
                                   int X, int Y, int Width, int Height, HWND Parent, HMENU Menu,
                                   HINSTANCE Instance, LPVOID Param);
FL_USER32_API BOOL DestroyWindow(HWND Window);
FL_USER32_API BOOL IsWindow(HWND Window);
FL_USER32_API BOOL IsWindowUnicode(HWND Window);
FL_USER32_API BOOL IsWindowVisible(HWND Window);
FL_USER32_API BOOL ShowWindow(HWND Window, int Command);
FL_USER32_API BOOL UpdateWindow(HWND Window);
FL_USER32_API BOOL InvalidateRect(HWND Window, const RECT* Rect, BOOL Erase);
FL_USER32_API BOOL EnableWindow(HWND Window, BOOL Enable);
FL_USER32_API BOOL SetWindowPos(HWND Window, HWND After, int X, int Y, int Width, int Height,
                                UINT Flags);
FL_USER32_API BOOL MoveWindow(HWND Window, int X, int Y, int Width, int Height, BOOL Repaint);
FL_USER32_API BOOL GetWindowRect(HWND Window, LPRECT Rect);
FL_USER32_API BOOL GetClientRect(HWND Window, LPRECT Rect);
FL_USER32_API BOOL ClientToScreen(HWND Window, LPPOINT Point);
FL_USER32_API BOOL ScreenToClient(HWND Window, LPPOINT Point);
FL_USER32_API BOOL AdjustWindowRect(LPRECT Rect, DWORD Style, BOOL Menu);
FL_USER32_API BOOL AdjustWindowRectEx(LPRECT Rect, DWORD Style, BOOL Menu, DWORD ExStyle);
FL_USER32_API LONG GetWindowLongA(HWND Window, int Index);
FL_USER32_API LONG SetWindowLongA(HWND Window, int Index, LONG Value);
FL_USER32_API LONG_PTR GetWindowLongPtrA(HWND Window, int Index);
FL_USER32_API LONG_PTR SetWindowLongPtrA(HWND Window, int Index, LONG_PTR Value);
FL_USER32_API BOOL SetWindowTextA(HWND Window, LPCSTR Text);
FL_USER32_API BOOL SetWindowTextW(HWND Window, LPCWSTR Text);
FL_USER32_API HWND GetDesktopWindow();
FL_USER32_API HWND GetForegroundWindow();
FL_USER32_API BOOL SetForegroundWindow(HWND Window);
FL_USER32_API HWND GetActiveWindow();
FL_USER32_API HWND GetFocus();
FL_USER32_API HWND SetFocus(HWND Window);
FL_USER32_API HWND SetCapture(HWND Window);
FL_USER32_API BOOL ReleaseCapture();
FL_USER32_API HMENU GetMenu(HWND Window);
FL_USER32_API int GetSystemMetrics(int Index);
FL_USER32_API BOOL EnumDisplaySettingsA(LPCSTR Device, DWORD Mode, DEVMODEA* Settings);

// Messages.
FL_USER32_API LRESULT DefWindowProcA(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
FL_USER32_API LRESULT CallWindowProcA(WNDPROC Procedure, HWND Window, UINT Message, WPARAM WParam,
                                      LPARAM LParam);
FL_USER32_API LRESULT SendMessageA(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
FL_USER32_API LRESULT SendMessageW(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
FL_USER32_API BOOL PostMessageA(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
FL_USER32_API void PostQuitMessage(int ExitCode);
FL_USER32_API BOOL PeekMessageA(LPMSG Message, HWND Window, UINT First, UINT Last, UINT Remove);
FL_USER32_API BOOL GetMessageA(LPMSG Message, HWND Window, UINT First, UINT Last);
FL_USER32_API BOOL TranslateMessage(const MSG* Message);
FL_USER32_API LRESULT DispatchMessageA(const MSG* Message);

// Whether the calling thread's queue holds input of the wake mask's classes (QS_) that arrived
// since the thread last looked with PeekMessageA / GetMessageA; WM_QUIT always counts.
FL_USER32_API bool PosixUser32HasInput(DWORD WakeMask);

// MsgWaitForMultipleObjects: the handles are the platform layer's, which user32 does not use (it
// would link dacom, which links it), so the wait is here, looking for input every 10 ms. Wait
// any: WAIT_OBJECT_0 + Count for new input of the wake mask, or the handles' wait. Wait all:
// WAIT_OBJECT_0 once the input is there and every handle is signalled (the handles are only
// waited on, and an auto-reset event only consumed, when the input is there). At the deadline
// the queue is looked at once more.
inline DWORD MsgWaitForMultipleObjects(DWORD Count, const HANDLE* Handles, BOOL WaitAll,
                                       DWORD Milliseconds, DWORD WakeMask)
{
    DWORD const Start = GetTickCount();
    for (;;)
    {
        bool const bInput = PosixUser32HasInput(WakeMask);
        if (bInput && (WaitAll == FALSE || Count == 0))
            return WAIT_OBJECT_0 + (WaitAll == FALSE ? Count : 0);
        if (bInput)
        {
            // A failed wait (WAIT_FAILED) is returned as it is, not as the handles' success.
            DWORD const Result = Platform::WaitMany(Count, Handles, true, 0);
            if (Result == WAIT_FAILED)
                return WAIT_FAILED;
            if (Result != WAIT_TIMEOUT)
                return WAIT_OBJECT_0;
        }
        DWORD Slice = 10;
        bool bLast = false;
        if (Milliseconds != INFINITE)
        {
            DWORD const Elapsed = GetTickCount() - Start;
            if (Elapsed >= Milliseconds)
            {
                Slice = 0;
                bLast = true;
            }
            else if (Milliseconds - Elapsed < Slice)
                Slice = Milliseconds - Elapsed;
        }
        if (WaitAll == FALSE && Count != 0)
        {
            DWORD const Result = Platform::WaitMany(Count, Handles, false, Slice);
            if (Result != WAIT_TIMEOUT)
                return Result;
        }
        else if (Slice != 0)
            Platform::SleepMs(Slice);
        if (bLast)
        {
            if (PosixUser32HasInput(WakeMask) && (WaitAll == FALSE || Count == 0))
                return WAIT_OBJECT_0 + (WaitAll == FALSE ? Count : 0);
            return WAIT_TIMEOUT;
        }
    }
}

// Dialogs (no templates here: DialogBoxParamA answers -1).
FL_USER32_API INT_PTR DialogBoxParamA(HINSTANCE Instance, LPCSTR Template, HWND Parent,
                                      DLGPROC Procedure, LPARAM Param);
FL_USER32_API BOOL EndDialog(HWND Dialog, INT_PTR Result);
FL_USER32_API HWND GetDlgItem(HWND Dialog, int Id);
FL_USER32_API LRESULT SendDlgItemMessageA(HWND Dialog, int Id, UINT Message, WPARAM WParam,
                                          LPARAM LParam);

// Message boxes: the text goes to stderr, and the answer is the one that carries on (IDIGNORE
// for abort/retry/ignore, IDYES for a question, IDOK otherwise).
FL_USER32_API int MessageBoxA(HWND Window, LPCSTR Text, LPCSTR Caption, UINT Type);
FL_USER32_API int MessageBoxW(HWND Window, LPCWSTR Text, LPCWSTR Caption, UINT Type);

// The cursor.
FL_USER32_API HCURSOR LoadCursorA(HINSTANCE Instance, LPCSTR Name);
FL_USER32_API HICON LoadIconA(HINSTANCE Instance, LPCSTR Name);
FL_USER32_API HCURSOR SetCursor(HCURSOR Cursor);
FL_USER32_API HCURSOR GetCursor();
FL_USER32_API int ShowCursor(BOOL Show);
FL_USER32_API BOOL GetCursorPos(LPPOINT Point);
FL_USER32_API BOOL SetCursorPos(int X, int Y);
FL_USER32_API BOOL ClipCursor(const RECT* Rect);
#define CURSOR_SHOWING 0x00000001
typedef struct tagCURSORINFO
{
    DWORD cbSize;
    DWORD flags;
    HCURSOR hCursor;
    POINT ptScreenPos;
} CURSORINFO, *PCURSORINFO, *LPCURSORINFO;
FL_USER32_API BOOL GetCursorInfo(PCURSORINFO Info);

// The keyboard.
FL_USER32_API SHORT GetKeyState(int Key);
FL_USER32_API SHORT GetAsyncKeyState(int Key);
FL_USER32_API UINT MapVirtualKeyA(UINT Code, UINT Type);
FL_USER32_API int GetKeyNameTextW(LONG LParam, LPWSTR Buffer, int Size);
FL_USER32_API HKL GetKeyboardLayout(DWORD Thread);
FL_USER32_API int GetKeyboardLayoutList(int Count, HKL* Layouts);

// Text helpers (the C library's case mapping).
FL_USER32_API LPWSTR CharUpperW(LPWSTR Text);
FL_USER32_API LPWSTR CharLowerW(LPWSTR Text);
FL_USER32_API int wsprintfW(LPWSTR Buffer, LPCWSTR Format, ...);
FL_USER32_API int LoadStringW(HINSTANCE Instance, UINT Id, LPWSTR Buffer, int Size);

// GDI's rectangle fill (no device context draws here).
FL_USER32_API int FillRect(HDC Dc, const RECT* Rect, HBRUSH Brush);

// The caret's blink and the double click's interval, in milliseconds (Windows' defaults).
FL_USER32_API UINT GetCaretBlinkTime();
FL_USER32_API UINT GetDoubleClickTime();
