// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): user32 on macOS and Linux, the calls src/include/posix/winuser.h declares. Not
// part of retail; built only off Windows, where every module that uses the stand-in <windows.h>
// links it (cmake/Freelancer.cmake), as user32.lib is among MSVC's default libraries.
//
// One instance per process holds the window classes, the windows and the message queue. It
// uses nothing of the platform layer's shared state (dacom holds that, and dacom links this
// module), only its own lock.
//
// A window here is a record: its class's window procedure, styles, rectangle, text and window
// data. There is no screen behind it yet; SDL gives the top-level windows one with M3, and its
// events become the messages below. Until then the "screen" is a 1024x768 desktop at 32 bits.

#include <windows.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <map>
#include <mutex>
#include <string>
#include <strings.h>

namespace
{
struct FClass
{
    WNDCLASSA Class;
    std::string Name;
};

struct FWindow
{
    std::string ClassName;
    WNDPROC Procedure = nullptr;
    HINSTANCE Instance = nullptr;
    HWND Parent = nullptr;
    HMENU Menu = nullptr;
    DWORD Style = 0;
    DWORD ExStyle = 0;
    RECT Rect = {};
    std::wstring Text;
    LONG_PTR UserData = 0;
    bool bEnabled = true;
};

// The desktop until SDL brings the display's (M3).
constexpr int kDesktopWidth = 1024;
constexpr int kDesktopHeight = 768;

// The first window number: clear of HWND_TOP / HWND_BOTTOM (0, 1) and of small sentinels.
constexpr std::uintptr_t kFirstWindow = 0x10;

struct FState
{
    std::recursive_mutex Lock;
    std::condition_variable_any Posted;
    std::map<std::string, FClass> Classes; // by lower-case name
    std::map<std::uintptr_t, FWindow> Windows;
    std::uintptr_t NextWindow = kFirstWindow;
    std::deque<MSG> Queue;
    HWND Focus = nullptr;
    HWND Active = nullptr;
    HWND Capture = nullptr;
    HCURSOR Cursor = nullptr;
    int CursorCount = 0; // ShowCursor's display count: shown at 0 and above
    POINT CursorPos = {kDesktopWidth / 2, kDesktopHeight / 2};
    SHORT Keys[256] = {};
};

FState& State()
{
    // Never destroyed: a module's static destructors may still post or destroy windows.
    static FState* const Instance = new FState();
    return *Instance;
}

std::string Lower(LPCSTR Name)
{
    std::string Text = Name != nullptr ? Name : "";
    for (char& Character : Text)
        Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
    return Text;
}

std::uintptr_t Key(HWND Window)
{
    return reinterpret_cast<std::uintptr_t>(Window);
}

// The window's record, or nullptr; the caller holds the lock.
FWindow* Find(HWND Window)
{
    auto const Found = State().Windows.find(Key(Window));
    return Found != State().Windows.end() ? &Found->second : nullptr;
}

std::string Narrow(LPCWSTR Text)
{
    std::string Out;
    if (Text == nullptr)
        return Out;
    for (; *Text != 0; ++Text)
    {
        char32_t Code = static_cast<char32_t>(*Text);
        if (Code < 0x80)
            Out += static_cast<char>(Code);
        else if (Code < 0x800)
        {
            Out += static_cast<char>(0xC0 | (Code >> 6));
            Out += static_cast<char>(0x80 | (Code & 0x3F));
        }
        else if (Code < 0x10000)
        {
            Out += static_cast<char>(0xE0 | (Code >> 12));
            Out += static_cast<char>(0x80 | ((Code >> 6) & 0x3F));
            Out += static_cast<char>(0x80 | (Code & 0x3F));
        }
        else
        {
            Out += static_cast<char>(0xF0 | (Code >> 18));
            Out += static_cast<char>(0x80 | ((Code >> 12) & 0x3F));
            Out += static_cast<char>(0x80 | ((Code >> 6) & 0x3F));
            Out += static_cast<char>(0x80 | (Code & 0x3F));
        }
    }
    return Out;
}

int MessageBoxAnswer(UINT Type)
{
    switch (Type & 0x0F)
    {
    case MB_ABORTRETRYIGNORE:
        return IDIGNORE;
    case MB_YESNOCANCEL:
    case MB_YESNO:
        return IDYES;
    default:
        return IDOK;
    }
}

bool Matches(const MSG& Message, HWND Window, UINT First, UINT Last)
{
    // A null window takes every message of the thread; (HWND)-1 only those posted with no window.
    if (Window == reinterpret_cast<HWND>(static_cast<std::intptr_t>(-1)))
    {
        if (Message.hwnd != nullptr)
            return false;
    }
    else if (Window != nullptr && Message.hwnd != Window && Message.message != WM_QUIT)
        return false;
    if (First == 0 && Last == 0)
        return true;
    return Message.message >= First && Message.message <= Last;
}

// The first queued message that matches, taken out with Remove; the caller holds the lock.
bool TakeMessage(LPMSG Out, HWND Window, UINT First, UINT Last, bool bRemove)
{
    FState& S = State();
    for (auto It = S.Queue.begin(); It != S.Queue.end(); ++It)
    {
        if (!Matches(*It, Window, First, Last))
            continue;
        if (Out != nullptr)
            *Out = *It;
        if (bRemove)
            S.Queue.erase(It);
        return true;
    }
    return false;
}

DWORD Ticks()
{
    using namespace std::chrono;
    return static_cast<DWORD>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// US-layout set-1 scan codes and key names, for MapVirtualKeyA and GetKeyNameTextW.
struct FKeyName
{
    UINT Vk;
    UINT Scan;
    bool bExtended;
    const wchar_t* Name;
};
const FKeyName kKeys[] = {
    {VK_ESCAPE, 0x01, false, L"Esc"},
    {'1', 0x02, false, L"1"},
    {'2', 0x03, false, L"2"},
    {'3', 0x04, false, L"3"},
    {'4', 0x05, false, L"4"},
    {'5', 0x06, false, L"5"},
    {'6', 0x07, false, L"6"},
    {'7', 0x08, false, L"7"},
    {'8', 0x09, false, L"8"},
    {'9', 0x0A, false, L"9"},
    {'0', 0x0B, false, L"0"},
    {VK_OEM_MINUS, 0x0C, false, L"-"},
    {VK_OEM_PLUS, 0x0D, false, L"="},
    {VK_BACK, 0x0E, false, L"Backspace"},
    {VK_TAB, 0x0F, false, L"Tab"},
    {'Q', 0x10, false, L"Q"},
    {'W', 0x11, false, L"W"},
    {'E', 0x12, false, L"E"},
    {'R', 0x13, false, L"R"},
    {'T', 0x14, false, L"T"},
    {'Y', 0x15, false, L"Y"},
    {'U', 0x16, false, L"U"},
    {'I', 0x17, false, L"I"},
    {'O', 0x18, false, L"O"},
    {'P', 0x19, false, L"P"},
    {VK_OEM_4, 0x1A, false, L"["},
    {VK_OEM_6, 0x1B, false, L"]"},
    {VK_RETURN, 0x1C, false, L"Enter"},
    {VK_CONTROL, 0x1D, false, L"Ctrl"},
    {'A', 0x1E, false, L"A"},
    {'S', 0x1F, false, L"S"},
    {'D', 0x20, false, L"D"},
    {'F', 0x21, false, L"F"},
    {'G', 0x22, false, L"G"},
    {'H', 0x23, false, L"H"},
    {'J', 0x24, false, L"J"},
    {'K', 0x25, false, L"K"},
    {'L', 0x26, false, L"L"},
    {VK_OEM_1, 0x27, false, L";"},
    {VK_OEM_7, 0x28, false, L"'"},
    {VK_OEM_3, 0x29, false, L"`"},
    {VK_SHIFT, 0x2A, false, L"Shift"},
    {VK_OEM_5, 0x2B, false, L"\\"},
    {'Z', 0x2C, false, L"Z"},
    {'X', 0x2D, false, L"X"},
    {'C', 0x2E, false, L"C"},
    {'V', 0x2F, false, L"V"},
    {'B', 0x30, false, L"B"},
    {'N', 0x31, false, L"N"},
    {'M', 0x32, false, L"M"},
    {VK_OEM_COMMA, 0x33, false, L","},
    {VK_OEM_PERIOD, 0x34, false, L"."},
    {VK_OEM_2, 0x35, false, L"/"},
    {VK_RSHIFT, 0x36, false, L"Right Shift"},
    {VK_MULTIPLY, 0x37, false, L"Num *"},
    {VK_MENU, 0x38, false, L"Alt"},
    {VK_SPACE, 0x39, false, L"Space"},
    {VK_CAPITAL, 0x3A, false, L"Caps Lock"},
    {VK_F1, 0x3B, false, L"F1"},
    {VK_F2, 0x3C, false, L"F2"},
    {VK_F3, 0x3D, false, L"F3"},
    {VK_F4, 0x3E, false, L"F4"},
    {VK_F5, 0x3F, false, L"F5"},
    {VK_F6, 0x40, false, L"F6"},
    {VK_F7, 0x41, false, L"F7"},
    {VK_F8, 0x42, false, L"F8"},
    {VK_F9, 0x43, false, L"F9"},
    {VK_F10, 0x44, false, L"F10"},
    {VK_PAUSE, 0x45, false, L"Pause"},
    {VK_SCROLL, 0x46, false, L"Scroll Lock"},
    {VK_NUMPAD7, 0x47, false, L"Num 7"},
    {VK_NUMPAD8, 0x48, false, L"Num 8"},
    {VK_NUMPAD9, 0x49, false, L"Num 9"},
    {VK_SUBTRACT, 0x4A, false, L"Num -"},
    {VK_NUMPAD4, 0x4B, false, L"Num 4"},
    {VK_NUMPAD5, 0x4C, false, L"Num 5"},
    {VK_NUMPAD6, 0x4D, false, L"Num 6"},
    {VK_ADD, 0x4E, false, L"Num +"},
    {VK_NUMPAD1, 0x4F, false, L"Num 1"},
    {VK_NUMPAD2, 0x50, false, L"Num 2"},
    {VK_NUMPAD3, 0x51, false, L"Num 3"},
    {VK_NUMPAD0, 0x52, false, L"Num 0"},
    {VK_DECIMAL, 0x53, false, L"Num Del"},
    {VK_F11, 0x57, false, L"F11"},
    {VK_F12, 0x58, false, L"F12"},
    {VK_RCONTROL, 0x1D, true, L"Right Ctrl"},
    {VK_DIVIDE, 0x35, true, L"Num /"},
    {VK_RMENU, 0x38, true, L"Right Alt"},
    {VK_NUMLOCK, 0x45, true, L"Num Lock"},
    {VK_HOME, 0x47, true, L"Home"},
    {VK_UP, 0x48, true, L"Up"},
    {VK_PRIOR, 0x49, true, L"Page Up"},
    {VK_LEFT, 0x4B, true, L"Left"},
    {VK_RIGHT, 0x4D, true, L"Right"},
    {VK_END, 0x4F, true, L"End"},
    {VK_DOWN, 0x50, true, L"Down"},
    {VK_NEXT, 0x51, true, L"Page Down"},
    {VK_INSERT, 0x52, true, L"Insert"},
    {VK_DELETE, 0x53, true, L"Delete"},
    {VK_LWIN, 0x5B, true, L"Left Windows"},
    {VK_RWIN, 0x5C, true, L"Right Windows"},
    {VK_APPS, 0x5D, true, L"Application"},
};
} // namespace

// ---------------------------------------------------------------------------------------------
// Classes and windows
// ---------------------------------------------------------------------------------------------

ATOM RegisterClassA(const WNDCLASSA* Class)
{
    if (Class == nullptr || Class->lpszClassName == nullptr || Class->lpfnWndProc == nullptr)
        return 0;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    std::string const Name = Lower(Class->lpszClassName);
    if (State().Classes.count(Name) != 0)
        return 0; // ERROR_CLASS_ALREADY_EXISTS
    FClass Entry{*Class, Class->lpszClassName};
    Entry.Class.lpszClassName = nullptr;
    State().Classes.emplace(Name, Entry);
    return static_cast<ATOM>(0xC000 + State().Classes.size());
}

BOOL UnregisterClassA(LPCSTR Name, HINSTANCE)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().Classes.erase(Lower(Name)) != 0 ? TRUE : FALSE;
}

HWND CreateWindowExA(DWORD ExStyle, LPCSTR ClassName, LPCSTR Title, DWORD Style, int X, int Y,
                     int Width, int Height, HWND Parent, HMENU Menu, HINSTANCE Instance,
                     LPVOID Param)
{
    HWND Window = nullptr;
    WNDPROC Procedure = nullptr;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        auto const Class = State().Classes.find(Lower(ClassName));
        if (Class == State().Classes.end())
            return nullptr; // ERROR_CANNOT_FIND_WND_CLASS
        FWindow Record;
        Record.ClassName = Class->second.Name;
        Record.Procedure = Procedure = Class->second.Class.lpfnWndProc;
        Record.Instance = Instance;
        Record.Parent = Parent;
        Record.Menu = Menu;
        Record.Style = Style;
        Record.ExStyle = ExStyle;
        if (X == CW_USEDEFAULT)
            X = Y = 0;
        if (Width == CW_USEDEFAULT)
        {
            Width = kDesktopWidth;
            Height = kDesktopHeight;
        }
        Record.Rect = {X, Y, X + Width, Y + Height};
        if (Title != nullptr)
            for (const char* Cursor = Title; *Cursor != 0; ++Cursor)
                Record.Text += static_cast<wchar_t>(static_cast<unsigned char>(*Cursor));
        Window = reinterpret_cast<HWND>(State().NextWindow++);
        State().Windows.emplace(Key(Window), Record);
    }

    CREATESTRUCTA Create = {
        Param, Instance,  Menu,   Parent, Height, Width, Y, X, static_cast<LONG>(Style),
        Title, ClassName, ExStyle};
    LPARAM const CreateParam = reinterpret_cast<LPARAM>(&Create);
    if (Procedure(Window, WM_NCCREATE, 0, CreateParam) == 0 ||
        Procedure(Window, WM_CREATE, 0, CreateParam) == -1)
    {
        DestroyWindow(Window);
        return nullptr;
    }
    if ((Style & WS_VISIBLE) != 0)
        Procedure(Window, WM_SHOWWINDOW, TRUE, 0);
    return Window;
}

BOOL DestroyWindow(HWND Window)
{
    WNDPROC Procedure = nullptr;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow* const Record = Find(Window);
        if (Record == nullptr)
            return FALSE;
        Procedure = Record->Procedure;
    }
    Procedure(Window, WM_DESTROY, 0, 0);
    Procedure(Window, WM_NCDESTROY, 0, 0);
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FState& S = State();
    S.Windows.erase(Key(Window));
    if (S.Focus == Window)
        S.Focus = nullptr;
    if (S.Active == Window)
        S.Active = nullptr;
    if (S.Capture == Window)
        S.Capture = nullptr;
    S.Queue.erase(std::remove_if(S.Queue.begin(), S.Queue.end(),
                                 [Window](const MSG& Message) { return Message.hwnd == Window; }),
                  S.Queue.end());
    return TRUE;
}

BOOL IsWindow(HWND Window)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return Find(Window) != nullptr ? TRUE : FALSE;
}

// Every class here is registered with RegisterClassA, so no window is Unicode.
BOOL IsWindowUnicode(HWND)
{
    return FALSE;
}

BOOL IsWindowVisible(HWND Window)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow const* const Record = Find(Window);
    return Record != nullptr && (Record->Style & WS_VISIBLE) != 0 ? TRUE : FALSE;
}

// The previous visibility, as Windows answers.
BOOL ShowWindow(HWND Window, int Command)
{
    WNDPROC Procedure = nullptr;
    bool bWasVisible = false;
    bool bVisible = false;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow* const Record = Find(Window);
        if (Record == nullptr)
            return FALSE;
        bWasVisible = (Record->Style & WS_VISIBLE) != 0;
        bVisible = Command != SW_HIDE;
        Record->Style = bVisible ? (Record->Style | WS_VISIBLE) : (Record->Style & ~WS_VISIBLE);
        Record->Style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
        if (Command == SW_MINIMIZE || Command == SW_SHOWMINIMIZED)
            Record->Style |= WS_MINIMIZE;
        else if (Command == SW_MAXIMIZE)
            Record->Style |= WS_MAXIMIZE;
        Procedure = Record->Procedure;
    }
    if (bWasVisible != bVisible)
        Procedure(Window, WM_SHOWWINDOW, bVisible ? TRUE : FALSE, 0);
    return bWasVisible ? TRUE : FALSE;
}

BOOL UpdateWindow(HWND Window)
{
    return IsWindow(Window);
}

BOOL InvalidateRect(HWND Window, const RECT*, BOOL)
{
    return Window == nullptr || IsWindow(Window) ? TRUE : FALSE;
}

BOOL EnableWindow(HWND Window, BOOL Enable)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow* const Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    bool const bWasDisabled = !Record->bEnabled;
    Record->bEnabled = Enable != FALSE;
    return bWasDisabled ? TRUE : FALSE;
}

BOOL SetWindowPos(HWND Window, HWND, int X, int Y, int Width, int Height, UINT Flags)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow* const Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    RECT& Rect = Record->Rect;
    if ((Flags & SWP_NOMOVE) == 0)
    {
        int const W = Rect.right - Rect.left;
        int const H = Rect.bottom - Rect.top;
        Rect = {X, Y, X + W, Y + H};
    }
    if ((Flags & SWP_NOSIZE) == 0)
    {
        Rect.right = Rect.left + Width;
        Rect.bottom = Rect.top + Height;
    }
    if ((Flags & SWP_SHOWWINDOW) != 0)
        Record->Style |= WS_VISIBLE;
    if ((Flags & SWP_HIDEWINDOW) != 0)
        Record->Style &= ~WS_VISIBLE;
    return TRUE;
}

BOOL MoveWindow(HWND Window, int X, int Y, int Width, int Height, BOOL)
{
    return SetWindowPos(Window, nullptr, X, Y, Width, Height, SWP_NOZORDER);
}

BOOL GetWindowRect(HWND Window, LPRECT Rect)
{
    if (Rect == nullptr)
        return FALSE;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    if (Window == GetDesktopWindow())
    {
        *Rect = {0, 0, kDesktopWidth, kDesktopHeight};
        return TRUE;
    }
    FWindow const* const Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    *Rect = Record->Rect;
    return TRUE;
}

// No frames here: the client area is the whole window.
BOOL GetClientRect(HWND Window, LPRECT Rect)
{
    RECT Outer;
    if (Rect == nullptr || !GetWindowRect(Window, &Outer))
        return FALSE;
    *Rect = {0, 0, Outer.right - Outer.left, Outer.bottom - Outer.top};
    return TRUE;
}

BOOL ClientToScreen(HWND Window, LPPOINT Point)
{
    RECT Outer;
    if (Point == nullptr || !GetWindowRect(Window, &Outer))
        return FALSE;
    Point->x += Outer.left;
    Point->y += Outer.top;
    return TRUE;
}

BOOL ScreenToClient(HWND Window, LPPOINT Point)
{
    RECT Outer;
    if (Point == nullptr || !GetWindowRect(Window, &Outer))
        return FALSE;
    Point->x -= Outer.left;
    Point->y -= Outer.top;
    return TRUE;
}

BOOL AdjustWindowRect(LPRECT Rect, DWORD, BOOL)
{
    return Rect != nullptr ? TRUE : FALSE;
}

BOOL AdjustWindowRectEx(LPRECT Rect, DWORD, BOOL, DWORD)
{
    return Rect != nullptr ? TRUE : FALSE;
}

LONG_PTR GetWindowLongPtrA(HWND Window, int Index)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow const* const Record = Find(Window);
    if (Record == nullptr)
        return 0;
    switch (Index)
    {
    case GWL_STYLE:
        return static_cast<LONG_PTR>(Record->Style);
    case GWL_EXSTYLE:
        return static_cast<LONG_PTR>(Record->ExStyle);
    case GWLP_WNDPROC:
        return reinterpret_cast<LONG_PTR>(Record->Procedure);
    case GWL_HINSTANCE:
        return reinterpret_cast<LONG_PTR>(Record->Instance);
    case GWLP_USERDATA:
        return Record->UserData;
    default:
        return 0;
    }
}

LONG_PTR SetWindowLongPtrA(HWND Window, int Index, LONG_PTR Value)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow* const Record = Find(Window);
    if (Record == nullptr)
        return 0;
    LONG_PTR const Old = GetWindowLongPtrA(Window, Index);
    switch (Index)
    {
    case GWL_STYLE:
        Record->Style = static_cast<DWORD>(Value);
        break;
    case GWL_EXSTYLE:
        Record->ExStyle = static_cast<DWORD>(Value);
        break;
    case GWLP_WNDPROC:
        Record->Procedure = reinterpret_cast<WNDPROC>(Value);
        break;
    case GWLP_USERDATA:
        Record->UserData = Value;
        break;
    default:
        return 0;
    }
    return Old;
}

// The 32-bit forms: the styles and the user data's low 32 bits (a window procedure does not fit
// in them on a 64-bit build, so GWL_WNDPROC answers 0 there, as 64-bit Windows does).
LONG GetWindowLongA(HWND Window, int Index)
{
    if (Index == GWL_WNDPROC && sizeof(void*) > sizeof(LONG))
        return 0;
    return static_cast<LONG>(GetWindowLongPtrA(Window, Index));
}

LONG SetWindowLongA(HWND Window, int Index, LONG Value)
{
    if (Index == GWL_WNDPROC && sizeof(void*) > sizeof(LONG))
        return 0;
    LONG_PTR Wide = Value;
    if (Index == GWL_STYLE || Index == GWL_EXSTYLE)
        Wide = static_cast<LONG_PTR>(static_cast<DWORD>(Value));
    return static_cast<LONG>(SetWindowLongPtrA(Window, Index, Wide));
}

BOOL SetWindowTextA(HWND Window, LPCSTR Text)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow* const Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    Record->Text.clear();
    if (Text != nullptr)
        for (; *Text != 0; ++Text)
            Record->Text += static_cast<wchar_t>(static_cast<unsigned char>(*Text));
    return TRUE;
}

BOOL SetWindowTextW(HWND Window, LPCWSTR Text)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow* const Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    Record->Text = Text != nullptr ? Text : L"";
    return TRUE;
}

// The desktop: a window number of its own, not a record.
HWND GetDesktopWindow()
{
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(kFirstWindow - 1));
}

HWND GetForegroundWindow()
{
    return GetActiveWindow();
}

BOOL SetForegroundWindow(HWND Window)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    if (Find(Window) == nullptr)
        return FALSE;
    State().Active = Window;
    return TRUE;
}

HWND GetActiveWindow()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().Active;
}

HWND GetFocus()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().Focus;
}

// WM_KILLFOCUS to the window losing it, WM_SETFOCUS to the one taking it. The previous focus.
HWND SetFocus(HWND Window)
{
    HWND Previous = nullptr;
    WNDPROC OldProcedure = nullptr;
    WNDPROC NewProcedure = nullptr;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow const* const Record = Find(Window);
        if (Window != nullptr && Record == nullptr)
            return nullptr;
        Previous = State().Focus;
        if (Previous == Window)
            return Previous;
        if (FWindow const* const Old = Find(Previous))
            OldProcedure = Old->Procedure;
        NewProcedure = Record != nullptr ? Record->Procedure : nullptr;
        State().Focus = Window;
    }
    if (OldProcedure != nullptr)
        OldProcedure(Previous, WM_KILLFOCUS, reinterpret_cast<WPARAM>(Window), 0);
    if (NewProcedure != nullptr)
        NewProcedure(Window, WM_SETFOCUS, reinterpret_cast<WPARAM>(Previous), 0);
    return Previous;
}

HWND SetCapture(HWND Window)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    HWND const Previous = State().Capture;
    State().Capture = Window;
    return Previous;
}

BOOL ReleaseCapture()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    State().Capture = nullptr;
    return TRUE;
}

HMENU GetMenu(HWND Window)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow const* const Record = Find(Window);
    return Record != nullptr ? Record->Menu : nullptr;
}

int GetSystemMetrics(int Index)
{
    switch (Index)
    {
    case SM_CXSCREEN:
        return kDesktopWidth;
    case SM_CYSCREEN:
        return kDesktopHeight;
    default:
        return 0;
    }
}

// The display's modes: the current one (ENUM_CURRENT_SETTINGS / ENUM_REGISTRY_SETTINGS), or by
// index the common 32-bit modes up to the desktop's size.
BOOL EnumDisplaySettingsA(LPCSTR, DWORD Mode, DEVMODEA* Settings)
{
    static const POINT kModes[] = {{640, 480}, {800, 600}, {1024, 768}};
    if (Settings == nullptr)
        return FALSE;
    POINT Size = {kDesktopWidth, kDesktopHeight};
    if (Mode != ENUM_CURRENT_SETTINGS && Mode != ENUM_REGISTRY_SETTINGS)
    {
        if (Mode >= sizeof(kModes) / sizeof(kModes[0]))
            return FALSE;
        Size = kModes[Mode];
    }
    WORD const StructSize = Settings->dmSize;
    std::memset(Settings, 0,
                StructSize >= sizeof(DEVMODEA) || StructSize == 0 ? sizeof(DEVMODEA) : StructSize);
    Settings->dmSize = StructSize;
    Settings->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    Settings->dmBitsPerPel = 32;
    Settings->dmPelsWidth = static_cast<DWORD>(Size.x);
    Settings->dmPelsHeight = static_cast<DWORD>(Size.y);
    Settings->dmDisplayFrequency = 60;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------

LRESULT DefWindowProcA(HWND Window, UINT Message, WPARAM WParam, LPARAM)
{
    switch (Message)
    {
    case WM_NCCREATE:
        return TRUE;
    case WM_CLOSE:
        DestroyWindow(Window);
        return 0;
    case WM_NCHITTEST:
        return HTCLIENT;
    case WM_ERASEBKGND:
        return 1;
    case WM_SYSCOMMAND:
        if ((WParam & 0xFFF0) == SC_CLOSE)
            SendMessageA(Window, WM_CLOSE, 0, 0);
        return 0;
    default:
        return 0;
    }
}

LRESULT CallWindowProcA(WNDPROC Procedure, HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    return Procedure != nullptr ? Procedure(Window, Message, WParam, LParam) : 0;
}

LRESULT SendMessageA(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    WNDPROC Procedure = nullptr;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow const* const Record = Find(Window);
        if (Record == nullptr)
            return 0;
        Procedure = Record->Procedure;
    }
    return Procedure(Window, Message, WParam, LParam);
}

// The window procedures here take both forms alike (no window is Unicode, and the messages the
// code sends this way carry no text).
LRESULT SendMessageW(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    return SendMessageA(Window, Message, WParam, LParam);
}

BOOL PostMessageA(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    if (Window != nullptr && Find(Window) == nullptr)
        return FALSE;
    MSG Entry = {Window, Message, WParam, LParam, Ticks(), State().CursorPos};
    State().Queue.push_back(Entry);
    State().Posted.notify_all();
    return TRUE;
}

void PostQuitMessage(int ExitCode)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    MSG Entry = {nullptr, WM_QUIT, static_cast<WPARAM>(ExitCode), 0, Ticks(), State().CursorPos};
    State().Queue.push_back(Entry);
    State().Posted.notify_all();
}

BOOL PeekMessageA(LPMSG Message, HWND Window, UINT First, UINT Last, UINT Remove)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return TakeMessage(Message, Window, First, Last, (Remove & PM_REMOVE) != 0) ? TRUE : FALSE;
}

// Waits for a message; FALSE for WM_QUIT.
BOOL GetMessageA(LPMSG Message, HWND Window, UINT First, UINT Last)
{
    MSG Taken;
    std::unique_lock<std::recursive_mutex> Guard(State().Lock);
    State().Posted.wait(Guard, [&] { return TakeMessage(&Taken, Window, First, Last, true); });
    if (Message != nullptr)
        *Message = Taken;
    return Taken.message == WM_QUIT ? FALSE : TRUE;
}

// No character messages from key messages yet: the window layer posts WM_CHAR itself (M3).
BOOL TranslateMessage(const MSG*)
{
    return FALSE;
}

LRESULT DispatchMessageA(const MSG* Message)
{
    if (Message == nullptr || Message->hwnd == nullptr)
        return 0;
    return SendMessageA(Message->hwnd, Message->message, Message->wParam, Message->lParam);
}

// Whether a message is queued (MsgWaitForMultipleObjects, posix/winuser.h).
bool PosixUser32HasMessage()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return !State().Queue.empty();
}

// ---------------------------------------------------------------------------------------------
// Dialogs and message boxes
// ---------------------------------------------------------------------------------------------

INT_PTR DialogBoxParamA(HINSTANCE, LPCSTR Template, HWND, DLGPROC, LPARAM)
{
    std::fprintf(stderr, "[user32] DialogBoxParamA(%p): no dialog templates here yet\n",
                 static_cast<const void*>(Template));
    return -1;
}

BOOL EndDialog(HWND, INT_PTR)
{
    return TRUE;
}

HWND GetDlgItem(HWND, int)
{
    return nullptr;
}

LRESULT SendDlgItemMessageA(HWND Dialog, int Id, UINT Message, WPARAM WParam, LPARAM LParam)
{
    HWND const Item = GetDlgItem(Dialog, Id);
    return Item != nullptr ? SendMessageA(Item, Message, WParam, LParam) : 0;
}

int MessageBoxA(HWND, LPCSTR Text, LPCSTR Caption, UINT Type)
{
    std::fprintf(stderr, "[%s] %s\n", Caption != nullptr ? Caption : "",
                 Text != nullptr ? Text : "");
    return MessageBoxAnswer(Type);
}

int MessageBoxW(HWND, LPCWSTR Text, LPCWSTR Caption, UINT Type)
{
    std::fprintf(stderr, "[%s] %s\n", Narrow(Caption).c_str(), Narrow(Text).c_str());
    return MessageBoxAnswer(Type);
}

// ---------------------------------------------------------------------------------------------
// The cursor
// ---------------------------------------------------------------------------------------------

// A cursor or icon is its resource id in a handle (the system's) or 1 (the module's own): there
// are no cursor images here yet.
HCURSOR LoadCursorA(HINSTANCE Instance, LPCSTR Name)
{
    if (Instance == nullptr && !IS_INTRESOURCE(Name))
        return nullptr;
    return reinterpret_cast<HCURSOR>(Instance == nullptr ? reinterpret_cast<std::uintptr_t>(Name)
                                                         : 1);
}

HICON LoadIconA(HINSTANCE, LPCSTR)
{
    return reinterpret_cast<HICON>(static_cast<std::uintptr_t>(1));
}

HCURSOR SetCursor(HCURSOR Cursor)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    HCURSOR const Previous = State().Cursor;
    State().Cursor = Cursor;
    return Previous;
}

HCURSOR GetCursor()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().Cursor;
}

int ShowCursor(BOOL Show)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    State().CursorCount += Show ? 1 : -1;
    return State().CursorCount;
}

BOOL GetCursorPos(LPPOINT Point)
{
    if (Point == nullptr)
        return FALSE;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    *Point = State().CursorPos;
    return TRUE;
}

BOOL SetCursorPos(int X, int Y)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    State().CursorPos = {std::clamp(X, 0, kDesktopWidth - 1), std::clamp(Y, 0, kDesktopHeight - 1)};
    return TRUE;
}

BOOL ClipCursor(const RECT*)
{
    return TRUE;
}

// CURSOR_SHOWING while ShowCursor's count is 0 or more.
BOOL GetCursorInfo(PCURSORINFO Info)
{
    if (Info == nullptr)
        return FALSE;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    Info->flags = State().CursorCount >= 0 ? CURSOR_SHOWING : 0;
    Info->hCursor = State().Cursor;
    Info->ptScreenPos = State().CursorPos;
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// The keyboard
// ---------------------------------------------------------------------------------------------

SHORT GetKeyState(int Key)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().Keys[Key & 0xFF];
}

SHORT GetAsyncKeyState(int Key)
{
    return static_cast<SHORT>(GetKeyState(Key) & 0x8000);
}

UINT MapVirtualKeyA(UINT Code, UINT Type)
{
    switch (Type)
    {
    case MAPVK_VK_TO_VSC:
        for (const FKeyName& Entry : kKeys)
            if (Entry.Vk == Code && !Entry.bExtended)
                return Entry.Scan;
        for (const FKeyName& Entry : kKeys)
            if (Entry.Vk == Code)
                return Entry.Scan;
        return 0;
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
        for (const FKeyName& Entry : kKeys)
            if (Entry.Scan == Code && !Entry.bExtended)
                return Entry.Vk;
        return 0;
    case MAPVK_VK_TO_CHAR:
        if ((Code >= '0' && Code <= '9') || (Code >= 'A' && Code <= 'Z'))
            return Code;
        return 0;
    default:
        return 0;
    }
}

// The key's name from a key message's lParam (scan code in bits 16-23, extended in bit 24), as
// an English US keyboard names it. The length written.
int GetKeyNameTextW(LONG LParam, LPWSTR Buffer, int Size)
{
    if (Buffer == nullptr || Size <= 0)
        return 0;
    UINT const Scan = (static_cast<DWORD>(LParam) >> 16) & 0xFF;
    bool const bExtended = (static_cast<DWORD>(LParam) & 0x01000000) != 0;
    for (const FKeyName& Entry : kKeys)
    {
        if (Entry.Scan != Scan || Entry.bExtended != bExtended)
            continue;
        int const Length = static_cast<int>(std::wcslen(Entry.Name));
        int const Copied = std::min(Length, Size - 1);
        std::wmemcpy(Buffer, Entry.Name, static_cast<size_t>(Copied));
        Buffer[Copied] = 0;
        return Copied;
    }
    Buffer[0] = 0;
    return 0;
}

// US English (0x0409) only.
HKL GetKeyboardLayout(DWORD)
{
    return reinterpret_cast<HKL>(static_cast<std::uintptr_t>(0x04090409));
}

int GetKeyboardLayoutList(int Count, HKL* Layouts)
{
    if (Count > 0 && Layouts != nullptr)
        Layouts[0] = GetKeyboardLayout(0);
    return 1;
}

// ---------------------------------------------------------------------------------------------
// Text helpers
// ---------------------------------------------------------------------------------------------

// A string, or one character in the low word of the pointer (as CharUpperW takes it).
LPWSTR CharUpperW(LPWSTR Text)
{
    if (IS_INTRESOURCE(Text))
        return reinterpret_cast<LPWSTR>(static_cast<std::uintptr_t>(
            std::towupper(static_cast<wint_t>(reinterpret_cast<std::uintptr_t>(Text)))));
    for (LPWSTR Cursor = Text; *Cursor != 0; ++Cursor)
        *Cursor = static_cast<wchar_t>(std::towupper(static_cast<wint_t>(*Cursor)));
    return Text;
}

LPWSTR CharLowerW(LPWSTR Text)
{
    if (IS_INTRESOURCE(Text))
        return reinterpret_cast<LPWSTR>(static_cast<std::uintptr_t>(
            std::towlower(static_cast<wint_t>(reinterpret_cast<std::uintptr_t>(Text)))));
    for (LPWSTR Cursor = Text; *Cursor != 0; ++Cursor)
        *Cursor = static_cast<wchar_t>(std::towlower(static_cast<wint_t>(*Cursor)));
    return Text;
}

// wsprintfW: 1024 characters at most with the terminator, the format in MSVC's wide meaning
// (%s and %c wide, %S / %C narrow: posix/msvc_crt.h). No floating-point conversions, as
// user32's.
int wsprintfW(LPWSTR Buffer, LPCWSTR Format, ...)
{
    if (Buffer == nullptr || Format == nullptr)
        return 0;
    std::wstring const Native = PosixMsvcCrt::WideFormat(Format);
    va_list Args;
    va_start(Args, Format);
    int const Length = std::vswprintf(Buffer, 1024, Native.c_str(), Args);
    va_end(Args);
    if (Length < 0)
    {
        Buffer[1023] = 0;
        return static_cast<int>(std::wcslen(Buffer));
    }
    return Length;
}

// String tables are in the Windows resource DLLs, which are not read here yet.
int LoadStringW(HINSTANCE, UINT, LPWSTR Buffer, int Size)
{
    if (Buffer != nullptr && Size > 0)
        Buffer[0] = 0;
    return 0;
}

int FillRect(HDC, const RECT*, HBRUSH)
{
    return 0;
}

// Windows' defaults.
UINT GetCaretBlinkTime()
{
    return 530;
}

UINT GetDoubleClickTime()
{
    return 500;
}
