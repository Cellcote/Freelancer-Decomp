// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): user32 on macOS and Linux, the calls src/include/posix/winuser.h declares. Not
// part of retail; built only off Windows, where every module that uses the stand-in <windows.h>
// links it (cmake/Freelancer.cmake), as user32.lib is among MSVC's default libraries.
//
// One instance per process holds the window classes and the windows, and each thread's message
// queue, as Windows keeps them: a window's posted messages go to the queue of the thread that
// made it, PostQuitMessage and a post with no window to the calling thread's, and a thread reads
// only its own. It uses nothing of the platform layer's shared state (dacom holds that, and dacom
// links this module), only its own lock.
//
// A window is a record: its class's window procedure, styles, rectangle, text and window data.
// A top-level window also gets a screen from the backend module (user32_backend.h, SDL3), loaded
// the first time one is made: its HWND is then the backend's window id, the calls below that
// change it change the screen's window too, and the screen's events become messages (the
// sink). Without the backend (FL_USER32_HEADLESS, or no user32_sdl module) windows stay records
// and the desktop is 1024x768 at 32 bits.
//
// The DirectInput system mouse's queue (posix/dinput.h) is here too, one per process, so that
// the backend's events reach the client's device.

#include <windows.h>

#include <dinput.h>

#include "user32_backend.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <deque>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <strings.h>
#include <thread>
#include <vector>

#include <dlfcn.h>

namespace
{
struct FClass
{
    WNDCLASSA Class;
    std::string Name;
    ATOM Atom = 0;
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
    bool bDestroying = false; // DestroyWindow under way: a nested call does nothing
    std::uint64_t Owner = 0; // the thread (ThreadSerial) whose queue takes its posted messages
    // The screen: the show state last reported to the window procedure (0 restored,
    // 1 minimized, 2 maximized), and an owned popup hidden while its owner is minimized.
    int ReportedShowState = 0;
    bool bHiddenWithOwner = false;
};

// A posted message, numbered so that a wait can tell input that arrived since the thread last
// looked (PeekMessageA / GetMessageA) from input it has seen.
struct FQueued
{
    MSG Message;
    std::uint64_t Sequence;
    // A key message from the screen: the whole key state (GetKeyState) as of that key, which
    // it brings when it is taken out of the queue.
    std::shared_ptr<const std::array<SHORT, 256>> Keys;
};

struct FThreadQueue
{
    std::deque<FQueued> Messages;
    std::uint64_t Seen = 0; // the newest sequence the thread has looked at
};

// The desktop without a backend.
constexpr int kHeadlessWidth = 1024;
constexpr int kHeadlessHeight = 768;

// The numbers of windows without a screen (child windows, and every window without a backend),
// far above the backend's ids (SDL counts from 1); the desktop is the number below them.
constexpr std::uintptr_t kFirstRecordWindow = 0x40000000;

struct FState
{
    std::recursive_mutex Lock;
    std::condition_variable_any Posted;
    std::map<std::string, FClass> Classes; // by lower-case name
    ATOM NextAtom = 0xC000;                 // class atoms, never reused
    std::map<std::uintptr_t, FWindow> Windows;
    std::uintptr_t NextWindow = kFirstRecordWindow;
    std::map<std::uint64_t, FThreadQueue> Queues; // by ThreadSerial, while the thread lives
    std::uint64_t NextThread = 1;
    std::uint64_t NextSequence = 1;
    HWND Focus = nullptr;
    HWND Active = nullptr;
    HWND Capture = nullptr;
    std::uint64_t FocusChanges = 0; // SetFocus's transitions, to tell a stale one
    HCURSOR Cursor = nullptr;
    int CursorCount = 0; // ShowCursor's display count: shown at 0 and above
    POINT CursorPos = {kHeadlessWidth / 2, kHeadlessHeight / 2};
    // GetKeyState's table: the keys as of the last key message taken from the queue. The keys
    // as the screen has them now (GetAsyncKeyState) are LiveKeys.
    SHORT Keys[256] = {};
    SHORT LiveKeys[256] = {};
    std::uint64_t Activations = 0; // the screen's focus changes, to tell a superseded one
    int BlockedWaiters = 0;        // threads blocked in GetMessageA's wait (off the screen)
    // The backend, once looked for; the thread that made the first screen window, the only one
    // that takes the screen's events (macOS has its windows on the main thread only).
    const FUser32Backend* Backend = nullptr;
    bool bBackendLookedFor = false;
    std::thread::id ScreenThread;
    HWND Grab = nullptr; // the screen window ClipCursor keeps the cursor in
    PosixDirectInput::FMouseQueue Mouse;
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

// A class by name (without regard to case) or by the atom RegisterClassA returned, in a name
// pointer's low word; the caller holds the lock.
FClass* FindClass(LPCSTR Name)
{
    FState& S = State();
    if (IS_INTRESOURCE(Name))
    {
        ATOM const Atom = static_cast<ATOM>(reinterpret_cast<std::uintptr_t>(Name));
        for (auto& Entry : S.Classes)
            if (Entry.second.Atom == Atom)
                return &Entry.second;
        return nullptr;
    }
    auto const Found = S.Classes.find(Lower(Name));
    return Found != S.Classes.end() ? &Found->second : nullptr;
}

// A number for the calling thread, never reused by another (a std::thread::id can be, once its
// thread has ended). Its queue exists from the thread's first use of user32 until the thread
// ends, so a later thread never inherits a message left for an earlier one.
struct FThreadQueueOwner
{
    std::uint64_t Serial = 0;
    ~FThreadQueueOwner()
    {
        if (Serial == 0)
            return;
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        State().Queues.erase(Serial);
    }
};

// The caller holds the lock.
std::uint64_t ThreadSerial()
{
    static thread_local FThreadQueueOwner Owner;
    if (Owner.Serial == 0)
    {
        Owner.Serial = State().NextThread++;
        State().Queues[Owner.Serial];
    }
    return Owner.Serial;
}

// The calling thread's queue; the caller holds the lock.
FThreadQueue& ThisQueue()
{
    return State().Queues[ThreadSerial()];
}

// Whether Window is Ancestor or one of its child windows, at any depth; the caller holds the lock.
// The parent chain has no cycle: a parent exists before its child is made, destroying it
// destroys the child, and nothing here reparents a window.
bool IsSelfOrDescendant(HWND Window, HWND Ancestor)
{
    while (Window != nullptr)
    {
        if (Window == Ancestor)
            return true;
        FWindow const* const Record = Find(Window);
        if (Record == nullptr || (Record->Style & WS_CHILD) == 0)
            return false;
        Window = Record->Parent;
    }
    return false;
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
    // WM_QUIT passes every filter. A null window takes every message of the thread; (HWND)-1
    // only those posted with no window; a window, its own and its child windows'.
    if (Message.message == WM_QUIT)
        return true;
    if (Window == reinterpret_cast<HWND>(static_cast<std::intptr_t>(-1)))
    {
        if (Message.hwnd != nullptr)
            return false;
    }
    else if (Window != nullptr && !IsSelfOrDescendant(Message.hwnd, Window))
        return false;
    if (First == 0 && Last == 0)
        return true;
    return Message.message >= First && Message.message <= Last;
}

// The first message of the calling thread's queue that matches, taken out with Remove. Looking
// marks everything queued so far as seen; the caller holds the lock.
bool TakeMessage(LPMSG Out, HWND Window, UINT First, UINT Last, bool bRemove)
{
    FThreadQueue& Queue = ThisQueue();
    Queue.Seen = State().NextSequence - 1;
    for (auto It = Queue.Messages.begin(); It != Queue.Messages.end(); ++It)
    {
        if (!Matches(It->Message, Window, First, Last))
            continue;
        if (Out != nullptr)
            *Out = It->Message;
        if (bRemove)
        {
            // A key message brings its key state as it is taken out, as Windows' GetKeyState.
            if (It->Keys != nullptr)
                std::copy(It->Keys->begin(), It->Keys->end(), std::begin(State().Keys));
            Queue.Messages.erase(It);
        }
        return true;
    }
    return false;
}

// The QS_ class of a queued message, for MsgWaitForMultipleObjects' wake mask.
DWORD InputClass(UINT Message)
{
    if (Message >= WM_KEYFIRST && Message <= WM_KEYLAST)
        return QS_KEY;
    if (Message == WM_MOUSEMOVE)
        return QS_MOUSEMOVE;
    if (Message >= WM_MOUSEFIRST && Message <= WM_MOUSELAST)
        return QS_MOUSEBUTTON;
    return QS_POSTMESSAGE | QS_ALLPOSTMESSAGE;
}

// Queues a message on a thread's queue; false when that thread has ended. The caller holds the
// lock.
bool Enqueue(std::uint64_t Thread, const MSG& Message,
             std::shared_ptr<const std::array<SHORT, 256>> Keys = nullptr)
{
    FState& S = State();
    auto const Queue = S.Queues.find(Thread);
    if (Queue == S.Queues.end())
        return false;
    FQueued Entry{Message, S.NextSequence++, std::move(Keys)};
    Queue->second.Messages.push_back(std::move(Entry));
    S.Posted.notify_all();
    return true;
}

DWORD Ticks()
{
    using namespace std::chrono;
    return static_cast<DWORD>(
        duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

// The backend module next to this one (user32_sdl.dylib / .so), loaded the first time the
// screen is needed. nullptr without one, or with FL_USER32_HEADLESS set (to anything but "0").
const FUser32Backend* Backend()
{
    FState& S = State();
    std::lock_guard<std::recursive_mutex> Guard(S.Lock);
    if (S.bBackendLookedFor)
        return S.Backend;
    S.bBackendLookedFor = true;
    const char* const Headless = std::getenv("FL_USER32_HEADLESS");
    if (Headless != nullptr && *Headless != 0 && std::strcmp(Headless, "0") != 0)
        return nullptr;
    Dl_info Info;
    if (dladdr(reinterpret_cast<const void*>(&Backend), &Info) == 0 || Info.dli_fname == nullptr)
        return nullptr;
    std::string Path = Info.dli_fname;
    Path.erase(Path.find_last_of('/') + 1);
#if defined(__APPLE__)
    Path += "user32_sdl.dylib";
#else
    Path += "user32_sdl.so";
#endif
    void* const Module = dlopen(Path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (Module == nullptr)
    {
        std::fprintf(stderr, "[user32] no screen (windows stay records): %s\n", dlerror());
        return nullptr;
    }
    auto const Entry =
        reinterpret_cast<FUser32BackendEntry>(dlsym(Module, FL_USER32_BACKEND_ENTRY));
    const FUser32Backend* const Found = Entry != nullptr ? Entry() : nullptr;
    if (Found == nullptr || Found->Version != kUser32BackendVersion)
    {
        std::fprintf(stderr, "[user32] %s is not a backend of version %d\n", Path.c_str(),
                     kUser32BackendVersion);
        return nullptr;
    }
    S.Backend = Found;
    return Found;
}

// The backend if it is loaded already (the calls that only follow a window's screen).
const FUser32Backend* LoadedBackend()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().Backend;
}

// Whether a window number is the backend's (a top-level window with a screen).
bool IsScreenWindow(HWND Window)
{
    std::uintptr_t const Number = Key(Window);
    return Number != 0 && Number < kFirstRecordWindow - 1 && LoadedBackend() != nullptr;
}

std::uint32_t ScreenId(HWND Window)
{
    return static_cast<std::uint32_t>(Key(Window));
}

HWND WindowOfId(std::uint32_t Id)
{
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(Id));
}

// The desktop's size: the display's once the backend is loaded (by the first top-level window),
// so that asking does not load SDL in a program that makes no window.
POINT DesktopSize()
{
    int Width = 0;
    int Height = 0;
    int Bits = 0;
    int Hz = 0;
    if (const FUser32Backend* const Screen = LoadedBackend())
        if (Screen->DisplayMode(-1, &Width, &Height, &Bits, &Hz))
            return {Width, Height};
    return {kHeadlessWidth, kHeadlessHeight};
}

bool IsFramed(DWORD Style)
{
    return (Style & WS_CAPTION) == WS_CAPTION;
}

// A sizing border or a maximize box: the screen's window can be resized and maximized.
bool IsResizable(DWORD Style)
{
    return (Style & (WS_THICKFRAME | WS_MAXIMIZEBOX)) != 0;
}

// Fullscreen: a popup without a frame that covers the desktop, as the client lays out its
// fullscreen window (ApplyGameWindowLayout).
bool CoversDesktop(DWORD Style, const RECT& Rect)
{
    POINT const Desktop = DesktopSize();
    return !IsFramed(Style) && (Style & WS_POPUP) != 0 && Rect.left <= 0 && Rect.top <= 0 &&
           Rect.right >= Desktop.x && Rect.bottom >= Desktop.y;
}

// The frame (a caption), whether it can be resized, and fullscreen.
void ApplyFrame(HWND Window, DWORD Style, const RECT& Rect)
{
    const FUser32Backend* const Screen = LoadedBackend();
    if (Screen == nullptr || !IsScreenWindow(Window))
        return;
    Screen->SetFrame(ScreenId(Window), IsFramed(Style), IsResizable(Style),
                     CoversDesktop(Style, Rect));
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

// The class's atom: a number of its own, never one a live class has (atoms are not reused).
ATOM RegisterClassA(const WNDCLASSA* Class)
{
    if (Class == nullptr || Class->lpszClassName == nullptr || Class->lpfnWndProc == nullptr)
        return 0;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FState& S = State();
    std::string const Name = Lower(Class->lpszClassName);
    if (S.Classes.count(Name) != 0 || S.NextAtom == 0xFFFF)
        return 0; // ERROR_CLASS_ALREADY_EXISTS
    FClass Entry{*Class, Class->lpszClassName, S.NextAtom++};
    Entry.Class.lpszClassName = nullptr;
    S.Classes.emplace(Name, Entry);
    return Entry.Atom;
}

BOOL UnregisterClassA(LPCSTR Name, HINSTANCE)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FClass* const Class = FindClass(Name);
    if (Class == nullptr)
        return FALSE;
    State().Classes.erase(Lower(Class->Name.c_str()));
    return TRUE;
}

HWND CreateWindowExA(DWORD ExStyle, LPCSTR ClassName, LPCSTR Title, DWORD Style, int X, int Y,
                     int Width, int Height, HWND Parent, HMENU Menu, HINSTANCE Instance,
                     LPVOID Param)
{
    HWND Window = nullptr;
    WNDPROC Procedure = nullptr;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FClass* const Class = FindClass(ClassName);
        if (Class == nullptr || (Parent != nullptr && Find(Parent) == nullptr))
            return nullptr; // ERROR_CANNOT_FIND_WND_CLASS / ERROR_INVALID_WINDOW_HANDLE
        FWindow Record;
        Record.ClassName = Class->Name;
        Record.Procedure = Procedure = Class->Class.lpfnWndProc;
        Record.Instance = Instance;
        Record.Parent = Parent;
        Record.Menu = Menu;
        Record.Style = Style;
        Record.ExStyle = ExStyle;
        Record.bEnabled = (Style & WS_DISABLED) == 0;
        Record.Owner = ThreadSerial();
        // A top-level window (not WS_CHILD: an owned popup is one too) gets a screen when there
        // is a backend (looked for now, so that its default size is the display's); its id is
        // the HWND.
        bool const bTopLevel = (Style & WS_CHILD) == 0;
        const FUser32Backend* const Screen = bTopLevel ? Backend() : nullptr;
        // The screen started (on the main thread) before the default size is taken from it.
        if (Screen != nullptr)
            Screen->Start();
        if (X == CW_USEDEFAULT)
            X = Y = 0;
        if (Width == CW_USEDEFAULT)
        {
            POINT const Desktop = DesktopSize();
            Width = Desktop.x;
            Height = Desktop.y;
        }
        // A child's rectangle is relative to its parent's client area, as Windows keeps it.
        Record.Rect = {X, Y, X + Width, Y + Height};
        if (Title != nullptr)
            for (const char* Cursor = Title; *Cursor != 0; ++Cursor)
                Record.Text += static_cast<wchar_t>(static_cast<unsigned char>(*Cursor));
        // The title as SetWindowTextA gives it to the screen: its bytes as characters, in UTF-8.
        std::uint32_t ScreenWindow = 0;
        if (Screen != nullptr &&
            Screen->CreateWindow(Narrow(Record.Text.c_str()).c_str(), X, Y, Width, Height,
                                 IsFramed(Style), IsResizable(Style), (Style & WS_VISIBLE) != 0,
                                 &ScreenWindow))
        {
            Window = WindowOfId(ScreenWindow);
            if (State().ScreenThread == std::thread::id())
            {
                State().ScreenThread = std::this_thread::get_id();
                // The first screen: the cursor as ShowCursor left it before there was one.
                if (State().CursorCount < 0)
                    Screen->ShowCursor(false);
            }
        }
        else
            Window = reinterpret_cast<HWND>(State().NextWindow++);
        State().Windows.emplace(Key(Window), Record);
        // A popup made over the whole desktop is fullscreen from the start.
        if (Screen != nullptr && CoversDesktop(Style, Record.Rect))
            ApplyFrame(Window, Style, Record.Rect);
    }

    // The class name as the caller passed it (a string or an atom), as CREATESTRUCT carries it.
    CREATESTRUCTA Create = {
        Param, Instance,  Menu,   Parent, Height, Width, Y, X, static_cast<LONG>(Style),
        Title, ClassName, ExStyle};
    LPARAM const CreateParam = reinterpret_cast<LPARAM>(&Create);
    // Each message through the window's current procedure: one WM_NCCREATE installs (a
    // subclass) gets WM_CREATE.
    (void)Procedure;
    if (SendMessageA(Window, WM_NCCREATE, 0, CreateParam) == 0 || !IsWindow(Window) ||
        SendMessageA(Window, WM_CREATE, 0, CreateParam) == -1 || !IsWindow(Window))
    {
        DestroyWindow(Window);
        return nullptr;
    }
    if ((Style & WS_VISIBLE) != 0)
    {
        SendMessageA(Window, WM_SHOWWINDOW, TRUE, 0);
        if (!IsWindow(Window))
            return nullptr;
    }
    return Window;
}

// WM_DESTROY, the children's destruction, WM_NCDESTROY, then the record and the messages queued
// for it go. A call for a window already being destroyed (from its own WM_DESTROY) does nothing.
BOOL DestroyWindow(HWND Window)
{
    std::uint64_t Owner = 0;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow* const Record = Find(Window);
        if (Record == nullptr || Record->bDestroying)
            return FALSE;
        Record->bDestroying = true;
        Owner = Record->Owner;
    }
    // Both messages through the window's current procedure: a subclass that restores the
    // original procedure in WM_DESTROY gets no WM_NCDESTROY.
    SendMessageA(Window, WM_DESTROY, 0, 0);
    std::vector<HWND> Children;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        for (const auto& Entry : State().Windows)
            if (Entry.second.Parent == Window)
                Children.push_back(reinterpret_cast<HWND>(Entry.first));
    }
    for (HWND Child : Children)
        DestroyWindow(Child);
    SendMessageA(Window, WM_NCDESTROY, 0, 0);
    if (IsScreenWindow(Window))
        LoadedBackend()->DestroyWindow(ScreenId(Window));
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FState& S = State();
    if (S.Grab == Window)
        S.Grab = nullptr;
    S.Windows.erase(Key(Window));
    if (S.Focus == Window)
        S.Focus = nullptr;
    if (S.Active == Window)
        S.Active = nullptr;
    if (S.Capture == Window)
        S.Capture = nullptr;
    auto const Queue = S.Queues.find(Owner);
    if (Queue != S.Queues.end())
    {
        std::deque<FQueued>& Messages = Queue->second.Messages;
        Messages.erase(std::remove_if(Messages.begin(), Messages.end(),
                                      [Window](const FQueued& Entry)
                                      { return Entry.Message.hwnd == Window; }),
                       Messages.end());
    }
    // A GetMessageA waiting for this window's messages returns -1.
    S.Posted.notify_all();
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

// Visible when it and every parent up to its top-level window have WS_VISIBLE.
BOOL IsWindowVisible(HWND Window)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow const* Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    for (; Record != nullptr; Record = Find(Record->Parent))
        if ((Record->Style & WS_VISIBLE) == 0)
            return FALSE;
    return TRUE;
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
        // SW_SHOW, SW_HIDE and SW_SHOWNA keep the window minimized or maximized as it is; the
        // others set its state. An explicit command also ends an owner's hold on a popup.
        if (Command != SW_SHOW && Command != SW_HIDE && Command != SW_SHOWNA)
        {
            Record->Style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
            if (Command == SW_MINIMIZE || Command == SW_SHOWMINIMIZED || Command == 7 ||
                Command == 11) // SW_SHOWMINNOACTIVE, SW_FORCEMINIMIZE
                Record->Style |= WS_MINIMIZE;
            else if (Command == SW_MAXIMIZE)
                Record->Style |= WS_MAXIMIZE;
        }
        Record->bHiddenWithOwner = false;
        Procedure = Record->Procedure;
    }
    // A maximize the screen refuses (a window that cannot be resized) leaves it as it was.
    if (IsScreenWindow(Window) && !LoadedBackend()->Show(ScreenId(Window), Command) &&
        Command == SW_MAXIMIZE)
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        if (FWindow* const Record = Find(Window))
            Record->Style &= ~WS_MAXIMIZE;
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
    bool bWasDisabled = false;
    bool const bEnabled = Enable != FALSE;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow* const Record = Find(Window);
        if (Record == nullptr)
            return FALSE;
        bWasDisabled = !Record->bEnabled;
        if (bEnabled == Record->bEnabled)
            return bWasDisabled ? TRUE : FALSE;
        // The state and WS_DISABLED in the style, then WM_ENABLE, before returning. The message
        // is sent with the lock released, as every message here is.
        Record->bEnabled = bEnabled;
        Record->Style = bEnabled ? (Record->Style & ~WS_DISABLED) : (Record->Style | WS_DISABLED);
    }
    SendMessageA(Window, WM_ENABLE, bEnabled ? TRUE : FALSE, 0);
    return bWasDisabled ? TRUE : FALSE;
}

BOOL SetWindowPos(HWND Window, HWND, int X, int Y, int Width, int Height, UINT Flags)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow* const Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    RECT& Rect = Record->Rect;
    RECT const Before = Rect;
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
    if ((Flags & (SWP_SHOWWINDOW | SWP_HIDEWINDOW)) != 0)
        Record->bHiddenWithOwner = false;
    if (IsScreenWindow(Window))
    {
        const FUser32Backend* const Screen = LoadedBackend();
        if (std::memcmp(&Before, &Rect, sizeof(RECT)) != 0)
            Screen->SetBounds(ScreenId(Window), Rect.left, Rect.top, Rect.right - Rect.left,
                              Rect.bottom - Rect.top);
        // SWP_FRAMECHANGED (a new style), or a size or position that may make a popup cover the
        // desktop or stop covering it.
        if ((Flags & SWP_FRAMECHANGED) != 0 || (Flags & SWP_NOSIZE) == 0 ||
            (Flags & SWP_NOMOVE) == 0)
            ApplyFrame(Window, Record->Style, Rect);
        if ((Flags & SWP_SHOWWINDOW) != 0)
            Screen->Show(ScreenId(Window), SW_SHOW);
        if ((Flags & SWP_HIDEWINDOW) != 0)
            Screen->Show(ScreenId(Window), SW_HIDE);
    }
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
        POINT const Desktop = DesktopSize();
        *Rect = {0, 0, Desktop.x, Desktop.y};
        return TRUE;
    }
    FWindow const* Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    // A child's rectangle is kept relative to its parent's client area: the desktop's
    // coordinates add up the parents' origins (there are no frames, so the client area's
    // origin is the window's).
    *Rect = Record->Rect;
    // Only a WS_CHILD window is placed in its parent; an owned popup's position is the desktop's.
    while ((Record->Style & WS_CHILD) != 0)
    {
        Record = Find(Record->Parent);
        if (Record == nullptr)
            break;
        OffsetRect(Rect, Record->Rect.left, Record->Rect.top);
    }
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
        for (const char* Cursor = Text; *Cursor != 0; ++Cursor)
            Record->Text += static_cast<wchar_t>(static_cast<unsigned char>(*Cursor));
    if (IsScreenWindow(Window))
        LoadedBackend()->SetTitle(ScreenId(Window), Narrow(Record->Text.c_str()).c_str());
    return TRUE;
}

BOOL SetWindowTextW(HWND Window, LPCWSTR Text)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow* const Record = Find(Window);
    if (Record == nullptr)
        return FALSE;
    Record->Text = Text != nullptr ? Text : L"";
    if (IsScreenWindow(Window))
        LoadedBackend()->SetTitle(ScreenId(Window), Narrow(Record->Text.c_str()).c_str());
    return TRUE;
}

// The desktop: a window number of its own, not a record.
HWND GetDesktopWindow()
{
    return reinterpret_cast<HWND>(static_cast<std::uintptr_t>(kFirstRecordWindow - 1));
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
    std::uint64_t Change = 0;
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
        Change = ++State().FocusChanges;
    }
    if (OldProcedure != nullptr)
        OldProcedure(Previous, WM_KILLFOCUS, reinterpret_cast<WPARAM>(Window), 0);
    // A WM_KILLFOCUS handler that moved the focus elsewhere made this transition stale: the
    // window it was meant for no longer has the focus, and gets no WM_SETFOCUS.
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        if (State().FocusChanges != Change || Find(Window) == nullptr)
            NewProcedure = nullptr;
    }
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
        return DesktopSize().x;
    case SM_CYSCREEN:
        return DesktopSize().y;
    default:
        return 0;
    }
}

// The display's modes: the current one (ENUM_CURRENT_SETTINGS / ENUM_REGISTRY_SETTINGS), or by
// index the display's fullscreen modes (the backend's), or without a backend the common 32-bit
// modes up to the headless desktop's size.
BOOL EnumDisplaySettingsA(LPCSTR, DWORD Mode, DEVMODEA* Settings)
{
    static const POINT kModes[] = {{640, 480}, {800, 600}, {1024, 768}};
    if (Settings == nullptr)
        return FALSE;
    int Width = kHeadlessWidth;
    int Height = kHeadlessHeight;
    int Bits = 32;
    int Hz = 60;
    bool const bCurrent = Mode == ENUM_CURRENT_SETTINGS || Mode == ENUM_REGISTRY_SETTINGS;
    if (!bCurrent && Mode > static_cast<DWORD>(std::numeric_limits<int>::max()))
        return FALSE;
    // The display's modes once the screen has started (the first top-level window made on the
    // main thread); before that, and in a program whose windows have no screen, the headless
    // desktop's.
    const FUser32Backend* const Screen = LoadedBackend();
    int DesktopWidth = 0;
    int DesktopHeight = 0;
    int DesktopBits = 0;
    int DesktopHz = 0;
    if (Screen != nullptr &&
        Screen->DisplayMode(-1, &DesktopWidth, &DesktopHeight, &DesktopBits, &DesktopHz))
    {
        if (!Screen->DisplayMode(bCurrent ? -1 : static_cast<int>(Mode), &Width, &Height, &Bits,
                                 &Hz))
            return FALSE;
    }
    else if (!bCurrent)
    {
        if (Mode >= sizeof(kModes) / sizeof(kModes[0]))
            return FALSE;
        Width = kModes[Mode].x;
        Height = kModes[Mode].y;
    }
    // The caller's dmSize, and the fields up to dmDisplayFrequency, which every DEVMODEA has.
    WORD const StructSize = Settings->dmSize;
    std::memset(Settings, 0, sizeof(DEVMODEA));
    Settings->dmSize = StructSize;
    Settings->dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY;
    Settings->dmBitsPerPel = static_cast<DWORD>(Bits);
    Settings->dmPelsWidth = static_cast<DWORD>(Width);
    Settings->dmPelsHeight = static_cast<DWORD>(Height);
    Settings->dmDisplayFrequency = static_cast<DWORD>(Hz);
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

// To the queue of the thread that made the window; with no window, to the calling thread's.
BOOL PostMessageA(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    std::uint64_t Thread = ThreadSerial();
    if (Window != nullptr)
    {
        FWindow const* const Record = Find(Window);
        if (Record == nullptr)
            return FALSE;
        Thread = Record->Owner;
    }
    return Enqueue(Thread, MSG{Window, Message, WParam, LParam, Ticks(), State().CursorPos})
               ? TRUE
               : FALSE;
}

// To the calling thread's queue.
void PostQuitMessage(int ExitCode)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    Enqueue(ThreadSerial(),
            MSG{nullptr, WM_QUIT, static_cast<WPARAM>(ExitCode), 0, Ticks(), State().CursorPos});
}

namespace
{
// ---------------------------------------------------------------------------------------------
// The screen's events (FUser32Sink)
// ---------------------------------------------------------------------------------------------

// A mouse message brings the keyboard as of its event when it is taken (GetKeyState), as a key
// message does: a click taken ahead of an earlier key message still sees that key.
void SinkPost(std::uint32_t Window, unsigned Message, std::uintptr_t WParam, std::intptr_t LParam)
{
    HWND const Target = WindowOfId(Window);
    if (Message < WM_MOUSEFIRST || Message > WM_MOUSELAST)
    {
        PostMessageA(Target, Message, WParam, LParam);
        return;
    }
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FWindow const* const Record = Find(Target);
    if (Record == nullptr)
        return;
    auto Snapshot = std::make_shared<std::array<SHORT, 256>>();
    std::copy(std::begin(State().LiveKeys), std::end(State().LiveKeys), Snapshot->begin());
    Enqueue(Record->Owner,
            MSG{Target, Message, static_cast<WPARAM>(WParam), static_cast<LPARAM>(LParam), Ticks(),
                State().CursorPos},
            std::move(Snapshot));
}

void SinkSend(std::uint32_t Window, unsigned Message, std::uintptr_t WParam, std::intptr_t LParam)
{
    SendMessageA(WindowOfId(Window), Message, WParam, LParam);
}

// The key's state (GetKeyState: 0x8000 down, 0x0001 toggled on each press) and its message, with
// the lParam Windows builds: repeat count 1, scan code, extended bit, Alt held (context), the
// previous state and the transition. Alt, F10 and keys with Alt held are system keys. Enter,
// Backspace, Tab and Esc also give WM_CHAR, as TranslateMessage makes them (typed text comes as
// its own events).
void SetKeyState(SHORT& Key, bool bDown, bool bRepeat)
{
    SHORT const Toggle = static_cast<SHORT>(Key & 1);
    if (bDown)
        Key = static_cast<SHORT>(0x8000 | (bRepeat ? Toggle : Toggle ^ 1));
    else
        Key = Toggle;
}

// The generic key of a modifier by side (VK_LSHIFT -> VK_SHIFT), and its other side; 0 for
// other keys.
UINT GenericModifier(UINT VirtualKey, UINT* OtherSide)
{
    switch (VirtualKey)
    {
    case VK_LSHIFT:
    case VK_RSHIFT:
        *OtherSide = VirtualKey == VK_LSHIFT ? VK_RSHIFT : VK_LSHIFT;
        return VK_SHIFT;
    case VK_LCONTROL:
    case VK_RCONTROL:
        *OtherSide = VirtualKey == VK_LCONTROL ? VK_RCONTROL : VK_LCONTROL;
        return VK_CONTROL;
    case VK_LMENU:
    case VK_RMENU:
        *OtherSide = VirtualKey == VK_LMENU ? VK_RMENU : VK_LMENU;
        return VK_MENU;
    default:
        return 0;
    }
}

void SinkReleaseKey(unsigned PhysicalKey);

void SinkKey(std::uint32_t Window, unsigned PhysicalKey, bool bExtended, bool bDown, bool bRepeat)
{
    if (PhysicalKey == 0 || PhysicalKey > 0xFF)
        return;
    // A modifier keeps each side's state, and the generic key is down while either side is;
    // its messages carry the generic key, with the side's scan code.
    UINT OtherSide = 0;
    UINT const Generic = GenericModifier(PhysicalKey, &OtherSide);
    UINT const VirtualKey = Generic != 0 ? Generic : PhysicalKey;
    HWND const Target = WindowOfId(Window);
    // The window checked, the key state changed and the message queued under one lock: a
    // window that is gone takes no key press and gets no message, but a release still lets go
    // of the key (and of the generic modifier when the other side is up).
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    SHORT* const Keys = State().LiveKeys;
    FWindow const* const Record = Find(Target);
    if (Record == nullptr)
    {
        if (!bDown)
            SinkReleaseKey(PhysicalKey);
        return;
    }
    bool const bWasDown = (Keys[VirtualKey] & 0x8000) != 0;
    if (Generic != 0)
    {
        SetKeyState(Keys[PhysicalKey], bDown, bRepeat);
        bool const bEitherDown = bDown || (Keys[OtherSide] & 0x8000) != 0;
        SHORT const Toggle = static_cast<SHORT>(Keys[Generic] & 1);
        Keys[Generic] = static_cast<SHORT>((bEitherDown ? 0x8000 : 0) |
                                           (bDown && !bRepeat ? Toggle ^ 1 : Toggle));
    }
    else
        SetKeyState(Keys[VirtualKey], bDown, bRepeat);
    bool const bAlt = (Keys[VK_MENU] & 0x8000) != 0;
    UINT const Scan = MapVirtualKeyA(PhysicalKey, MAPVK_VK_TO_VSC);
    std::uint32_t LParam = 1u | ((Scan & 0xFFu) << 16);
    if (bExtended)
        LParam |= 1u << 24;
    if (bAlt)
        LParam |= 1u << 29;
    if (bWasDown)
        LParam |= 1u << 30;
    if (!bDown)
        LParam |= 1u << 31;
    bool const bSystem = VirtualKey == VK_MENU || VirtualKey == VK_F10 || bAlt;
    UINT const Message =
        bDown ? (bSystem ? WM_SYSKEYDOWN : WM_KEYDOWN) : (bSystem ? WM_SYSKEYUP : WM_KEYUP);
    // The whole keyboard as of this key: the message brings it when it is taken, so any key
    // message that survives (another window's may be dropped with it) sets every key right.
    auto Snapshot = std::make_shared<std::array<SHORT, 256>>();
    std::copy(std::begin(State().LiveKeys), std::end(State().LiveKeys), Snapshot->begin());
    Enqueue(
        Record->Owner,
        MSG{Target, Message, VirtualKey, static_cast<LPARAM>(LParam), Ticks(), State().CursorPos},
        std::move(Snapshot));
    if (bDown && !bSystem &&
        (VirtualKey == VK_RETURN || VirtualKey == VK_BACK || VirtualKey == VK_TAB ||
         VirtualKey == VK_ESCAPE))
        PostMessageA(Target, WM_CHAR, VirtualKey, static_cast<LPARAM>(LParam));
}

// Typed text: WM_CHAR per UTF-16 unit, a surrogate pair for a character past U+FFFF.
void SinkChar(std::uint32_t Window, std::uint32_t Character)
{
    HWND const Target = WindowOfId(Window);
    if (Character < 0x20 || Character == 0x7F)
        return; // control characters come from the key events
    if (Character > 0xFFFF)
    {
        Character -= 0x10000;
        PostMessageA(Target, WM_CHAR, 0xD800 + (Character >> 10), 1);
        PostMessageA(Target, WM_CHAR, 0xDC00 + (Character & 0x3FF), 1);
        return;
    }
    PostMessageA(Target, WM_CHAR, Character, 1);
}

void SinkMouseMove(std::uint32_t, int, int, int ScreenX, int ScreenY)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    State().CursorPos = {ScreenX, ScreenY};
}

void SinkMouseRaw(std::uint32_t Offset, std::uint32_t Data)
{
    PosixDirectInput::MouseEvent(Offset, Data);
}

// WM_SIZE's reason for a window's show state.
WPARAM SizeReason(DWORD Style)
{
    if ((Style & WS_MINIMIZE) != 0)
        return SIZE_MINIMIZED;
    if ((Style & WS_MAXIMIZE) != 0)
        return SIZE_MAXIMIZED;
    return SIZE_RESTORED;
}

// A move or resize from the screen: the window's rectangle, then WM_MOVE / WM_SIZE.
void SinkBounds(std::uint32_t Window, int X, int Y, int Width, int Height)
{
    HWND const Target = WindowOfId(Window);
    bool bMoved = false;
    bool bResized = false;
    WPARAM Reason = SIZE_RESTORED;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow* const Record = Find(Target);
        if (Record == nullptr)
            return;
        RECT& Rect = Record->Rect;
        bMoved = Rect.left != X || Rect.top != Y;
        bResized = Rect.right - Rect.left != Width || Rect.bottom - Rect.top != Height;
        Rect = {X, Y, X + Width, Y + Height};
        Reason = SizeReason(Record->Style);
    }
    if (bMoved)
        SendMessageA(Target, WM_MOVE, 0, MAKELONG(X, Y));
    // A minimized window reports an empty client area; its rectangle keeps the new bounds for
    // when it is restored.
    if (bResized)
        SendMessageA(Target, WM_SIZE, Reason,
                     Reason == SIZE_MINIMIZED ? 0 : MAKELONG(Width, Height));
}

// WM_SHOWWINDOW's status for an owned window that follows its owner (SW_PARENTCLOSING,
// SW_PARENTOPENING).
constexpr LPARAM kParentClosing = 1;
constexpr LPARAM kParentOpening = 3;

// An owner minimized hides its visible owned popups, and restored shows the ones it hid, as
// Windows does; a popup hidden before stays hidden.
void FollowOwner(HWND Owner, bool bMinimized)
{
    std::vector<HWND> Changed;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        for (auto& [Number, Record] : State().Windows)
        {
            if (Record.Parent != Owner || (Record.Style & WS_CHILD) != 0)
                continue;
            HWND const Popup = reinterpret_cast<HWND>(Number);
            if (bMinimized && (Record.Style & WS_VISIBLE) != 0)
            {
                Record.Style &= ~WS_VISIBLE;
                Record.bHiddenWithOwner = true;
                Changed.push_back(Popup);
            }
            else if (!bMinimized && Record.bHiddenWithOwner)
            {
                Record.Style |= WS_VISIBLE;
                Record.bHiddenWithOwner = false;
                Changed.push_back(Popup);
            }
        }
    }
    for (HWND const Popup : Changed)
    {
        // A handler of an earlier popup may have pumped a newer owner change that moved this
        // one already: it is left as that change set it.
        {
            std::lock_guard<std::recursive_mutex> Guard(State().Lock);
            FWindow const* const Record = Find(Popup);
            bool const bVisible = Record != nullptr && (Record->Style & WS_VISIBLE) != 0;
            if (Record == nullptr || (bMinimized ? bVisible || !Record->bHiddenWithOwner
                                                 : !bVisible || Record->bHiddenWithOwner))
                continue;
        }
        if (IsScreenWindow(Popup))
            LoadedBackend()->Show(ScreenId(Popup), bMinimized ? SW_HIDE : SW_SHOWNA);
        SendMessageA(Popup, WM_SHOWWINDOW, bMinimized ? FALSE : TRUE,
                     bMinimized ? kParentClosing : kParentOpening);
    }
}

// Minimized, maximized or restored on screen (the window's own buttons, the system): the
// window's WS_MINIMIZE / WS_MAXIMIZE, and WM_SIZE with the reason.
void SinkShowState(std::uint32_t Window, int ShowState, int ClientWidth, int ClientHeight)
{
    HWND const Target = WindowOfId(Window);
    RECT Rect = {};
    WPARAM Reason = SIZE_RESTORED;
    bool bWasMinimized = false;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FWindow* const Record = Find(Target);
        if (Record == nullptr)
            return;
        Record->Style &= ~(WS_MINIMIZE | WS_MAXIMIZE);
        if (ShowState == 1)
            Record->Style |= WS_MINIMIZE;
        else if (ShowState == 2)
            Record->Style |= WS_MAXIMIZE;
        // Repeats are left out by the state last reported, not by the style: ShowWindow sets
        // the style before the screen reports the change.
        if (Record->ReportedShowState == ShowState)
            return;
        bWasMinimized = Record->ReportedShowState == 1;
        Record->ReportedShowState = ShowState;
        // Maximized or restored: the size the window has now, so that WM_SIZE carries it (the
        // resize event after it then changes nothing).
        if (ShowState != 1 && ClientWidth > 0 && ClientHeight > 0)
        {
            Record->Rect.right = Record->Rect.left + ClientWidth;
            Record->Rect.bottom = Record->Rect.top + ClientHeight;
        }
        Rect = Record->Rect;
        Reason = SizeReason(Record->Style);
    }
    // A minimized window's client area is empty, as Windows reports it.
    int const Width = Reason == SIZE_MINIMIZED ? 0 : Rect.right - Rect.left;
    int const Height = Reason == SIZE_MINIMIZED ? 0 : Rect.bottom - Rect.top;
    SendMessageA(Target, WM_SIZE, Reason, MAKELONG(Width, Height));
    // The owner's state as it is after WM_SIZE: a handler that pumps messages may have taken a
    // newer show change (a restore inside a minimize), which has then moved the popups itself.
    bool bMinimizedNow = ShowState == 1;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        if (FWindow const* const Record = Find(Target))
            bMinimizedNow = Record->ReportedShowState == 1;
    }
    if (bMinimizedNow)
        FollowOwner(Target, true);
    else if (bWasMinimized)
        FollowOwner(Target, false);
}

// A key released for a window that is gone: the live key state lets go of it (and of the
// generic modifier when the other side is up), with no message.
void SinkReleaseKey(unsigned PhysicalKey)
{
    if (PhysicalKey == 0 || PhysicalKey > 0xFF)
        return;
    UINT OtherSide = 0;
    UINT const Generic = GenericModifier(PhysicalKey, &OtherSide);
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    SHORT* const Keys = State().LiveKeys;
    Keys[PhysicalKey] = static_cast<SHORT>(Keys[PhysicalKey] & 1);
    if (Generic != 0 && (Keys[OtherSide] & 0x8000) == 0)
        Keys[Generic] = static_cast<SHORT>(Keys[Generic] & 1);
}

// The application's focus: WM_ACTIVATEAPP and WM_ACTIVATE, then the keyboard focus. A handler
// that pumps the screen (a nested message loop) may take a newer focus change: the rest of this
// one is then left out, so it cannot restore a stale focus.
void SinkActivate(std::uint32_t Window, bool bActive)
{
    HWND const Target = WindowOfId(Window);
    std::uint64_t Transition = 0;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        if (Find(Target) == nullptr)
            return;
        State().Active = bActive ? Target : nullptr;
        Transition = ++State().Activations;
    }
    auto const Current = [Transition]
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        return State().Activations == Transition;
    };
    SendMessageA(Target, WM_ACTIVATEAPP, bActive ? TRUE : FALSE, 0);
    if (!Current())
        return;
    SendMessageA(Target, WM_ACTIVATE, bActive ? WA_ACTIVE : WA_INACTIVE, 0);
    if (!Current())
        return;
    SetFocus(bActive ? Target : nullptr);
}

const FUser32Sink GSink = {&SinkPost,      &SinkSend,      &SinkKey,    &SinkChar,
                           &SinkMouseMove, &SinkMouseRaw,  &SinkBounds, &SinkActivate,
                           &SinkShowState, &SinkReleaseKey};

// Takes the screen's events, on the thread that made the screen's windows. A window procedure
// the sink calls may pump again (a nested message loop): the backend then goes on with the
// events after the one being handled.
void PumpScreen()
{
    const FUser32Backend* Screen = nullptr;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FState& S = State();
        if (S.Backend == nullptr || S.ScreenThread != std::this_thread::get_id())
            return;
        Screen = S.Backend;
    }
    Screen->Pump(&GSink);
}

// Whether this thread takes the screen's events.
bool OnScreenThread()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().Backend != nullptr && State().ScreenThread == std::this_thread::get_id();
}
} // namespace

BOOL PeekMessageA(LPMSG Message, HWND Window, UINT First, UINT Last, UINT Remove)
{
    PumpScreen();
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return TakeMessage(Message, Window, First, Last, (Remove & PM_REMOVE) != 0) ? TRUE : FALSE;
}

// Waits for a message of the calling thread; FALSE for WM_QUIT, -1 for a null output or a window
// that does not exist (null and (HWND)-1 are filters, not windows). On the screen's thread it
// waits on the screen's events too, 10 ms at a time.
BOOL GetMessageA(LPMSG Message, HWND Window, UINT First, UINT Last)
{
    MSG Taken;
    // A window filter whose window is gone (destroyed while this waits, by the screen's events
    // or another thread): -1, as for a window that never was.
    bool const bWindowFilter =
        Window != nullptr && Window != reinterpret_cast<HWND>(static_cast<std::intptr_t>(-1));
    auto const Gone = [&] { return bWindowFilter && Find(Window) == nullptr; };
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        if (Message == nullptr || Gone())
            return -1;
    }
    for (;;)
    {
        PumpScreen();
        {
            std::unique_lock<std::recursive_mutex> Guard(State().Lock);
            if (TakeMessage(&Taken, Window, First, Last, true))
                break;
            if (Gone())
                return -1;
            if (!OnScreenThread())
            {
                // Counted as blocked while the predicate sends it back to waiting: under the
                // lock that the wait gives up atomically, so a thread that sees the count sees a
                // waiter that is blocked.
                bool bTaken = false;
                bool bCounted = false;
                State().Posted.wait(Guard,
                                    [&]
                                    {
                                        bTaken = TakeMessage(&Taken, Window, First, Last, true);
                                        bool const bDone = bTaken || Gone();
                                        if (bDone == bCounted)
                                        {
                                            State().BlockedWaiters += bDone ? -1 : 1;
                                            bCounted = !bDone;
                                        }
                                        return bDone;
                                    });
                if (!bTaken)
                    return -1;
                break;
            }
        }
        LoadedBackend()->Wait(10);
    }
    if (Message != nullptr)
        *Message = Taken;
    return Taken.message == WM_QUIT ? FALSE : TRUE;
}

// Character messages come from the screen's text events and its key events (SinkKey), not from
// here.
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

// The threads blocked in GetMessageA's wait (not the screen's thread), for tests that must act
// while one waits. PORT(macOS), not part of retail; declared where it is used.
FL_USER32_API int PosixUser32BlockedWaiters()
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return State().BlockedWaiters;
}

// Whether the calling thread's queue holds input of the wake mask's classes that arrived since
// the thread last looked (PeekMessageA / GetMessageA), as MsgWaitForMultipleObjects wakes for it
// (posix/winuser.h). WM_QUIT always counts.
bool PosixUser32HasInput(DWORD WakeMask)
{
    PumpScreen();
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FThreadQueue const& Queue = ThisQueue();
    for (const FQueued& Entry : Queue.Messages)
        if (Entry.Sequence > Queue.Seen && (Entry.Message.message == WM_QUIT ||
                                            (InputClass(Entry.Message.message) & WakeMask) != 0))
            return true;
    return false;
}

// The DirectInput system mouse's queue (posix/dinput.h), one per process.
PosixDirectInput::FMouseQueue& PosixDirectInputMouse()
{
    return State().Mouse;
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

// The count crossing 0 shows or hides the screen's cursor.
// The count's change and the screen's call in one step (under the lock), so that crossings from
// different threads reach the screen in the order the count made them; the backend only queues
// a call from another thread, never waits.
int ShowCursor(BOOL Show)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    int const Before = State().CursorCount;
    int const Count = State().CursorCount += Show ? 1 : -1;
    if ((Before >= 0) != (Count >= 0))
        if (const FUser32Backend* const Screen = State().Backend)
            Screen->ShowCursor(Count >= 0);
    return Count;
}

BOOL GetCursorPos(LPPOINT Point)
{
    if (Point == nullptr)
        return FALSE;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    *Point = State().CursorPos;
    return TRUE;
}

// The cursor moves within the active window on the screen (desktop coordinates, made the
// window's).
BOOL SetCursorPos(int X, int Y)
{
    POINT const Desktop = DesktopSize();
    HWND Active = nullptr;
    POINT Client = {};
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        State().CursorPos = {std::clamp(X, 0, Desktop.x - 1), std::clamp(Y, 0, Desktop.y - 1)};
        Client = State().CursorPos;
        Active = State().Active;
    }
    if (Active != nullptr && IsScreenWindow(Active) && ScreenToClient(Active, &Client))
        LoadedBackend()->WarpMouse(ScreenId(Active), Client.x, Client.y);
    return TRUE;
}

// A rectangle keeps the cursor in the active window (the screen has no finer clip); nullptr
// frees it from the window that has it, active or not.
BOOL ClipCursor(const RECT* Rect)
{
    HWND Release = nullptr;
    HWND Clip = nullptr;
    {
        std::lock_guard<std::recursive_mutex> Guard(State().Lock);
        FState& S = State();
        HWND const Active = S.Active;
        if (Rect != nullptr && Active != nullptr && IsScreenWindow(Active))
            Clip = Active;
        if (S.Grab != nullptr && S.Grab != Clip)
            Release = S.Grab;
        S.Grab = Clip;
    }
    if (Release != nullptr && IsScreenWindow(Release))
        LoadedBackend()->ClipCursor(ScreenId(Release), false);
    if (Clip != nullptr)
        LoadedBackend()->ClipCursor(ScreenId(Clip), true);
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

// The key as the screen has it now, not as of the last key message taken.
SHORT GetAsyncKeyState(int Key)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return static_cast<SHORT>(State().LiveKeys[Key & 0xFF] & 0x8000);
}

// MAPVK_VK_TO_VSC: a key's scan code (left and right modifiers by side, a generic one as its
// left key's). MAPVK_VSC_TO_VK(_EX): the key for a scan code, an extended one with its 0xE0 /
// 0xE1 prefix in the high byte; _EX names the modifiers' sides, the plain form the generic
// VK_SHIFT / VK_CONTROL / VK_MENU. MAPVK_VK_TO_CHAR: digits and letters.
UINT MapVirtualKeyA(UINT Code, UINT Type)
{
    switch (Type)
    {
    case MAPVK_VK_TO_VSC:
    {
        UINT Key = Code;
        if (Key == VK_LSHIFT)
            Key = VK_SHIFT;
        else if (Key == VK_LCONTROL)
            Key = VK_CONTROL;
        else if (Key == VK_LMENU)
            Key = VK_MENU;
        for (const FKeyName& Entry : kKeys)
            if (Entry.Vk == Key && !Entry.bExtended)
                return Entry.Scan;
        for (const FKeyName& Entry : kKeys)
            if (Entry.Vk == Key)
                return Entry.Scan;
        return 0;
    }
    case MAPVK_VSC_TO_VK:
    case MAPVK_VSC_TO_VK_EX:
    {
        UINT const Prefix = Code & 0xFF00;
        bool const bExtended = Prefix == 0xE000 || Prefix == 0xE100;
        if (Prefix != 0 && !bExtended)
            return 0;
        UINT Key = 0;
        for (const FKeyName& Entry : kKeys)
            if (Entry.Scan == (Code & 0xFF) && Entry.bExtended == bExtended)
            {
                Key = Entry.Vk;
                break;
            }
        if (Type == MAPVK_VSC_TO_VK_EX)
        {
            if (Key == VK_SHIFT)
                return VK_LSHIFT;
            if (Key == VK_CONTROL)
                return VK_LCONTROL;
            if (Key == VK_MENU)
                return VK_LMENU;
            return Key;
        }
        if (Key == VK_RSHIFT)
            return VK_SHIFT;
        if (Key == VK_RCONTROL)
            return VK_CONTROL;
        if (Key == VK_RMENU)
            return VK_MENU;
        return Key;
    }
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

// Windows' defaults.
UINT GetCaretBlinkTime()
{
    return 530;
}

UINT GetDoubleClickTime()
{
    return 500;
}
