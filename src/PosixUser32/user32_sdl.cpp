// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): user32's screen on macOS and Linux, over SDL3 (user32_backend.h). Not part of
// retail; user32 loads it when the first top-level window is made.
//
// Each top-level window is an SDL window, and its SDL id is its HWND. SDL's events become the
// sink's calls: keys (SDL scancodes to the US layout's virtual keys), typed text, the mouse
// (client positions for the window messages, relative motion and buttons for DirectInput),
// moves and resizes, focus and the close button. Window sizes are SDL's (points on a
// high-density display): the game's resolution is the window's size, and RPGPU scales its frame
// to the window's pixels.

#include "user32_backend.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <pthread.h>
#else
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace
{
// The SDK's values (posix/winuser.h, posix/dinput.h), repeated here so that this module needs
// no Windows header.
constexpr unsigned kWmClose = 0x0010;
constexpr unsigned kWmSysCommand = 0x0112;
constexpr unsigned kWmMouseMove = 0x0200;
constexpr unsigned kWmLButtonDown = 0x0201;
constexpr unsigned kWmLButtonUp = 0x0202;
constexpr unsigned kWmRButtonDown = 0x0204;
constexpr unsigned kWmRButtonUp = 0x0205;
constexpr unsigned kWmMButtonDown = 0x0207;
constexpr unsigned kWmMButtonUp = 0x0208;
constexpr unsigned kWmMouseWheel = 0x020A;
constexpr std::uintptr_t kScClose = 0xF060;
constexpr std::uint32_t kDimofsX = 0;
constexpr std::uint32_t kDimofsY = 4;
constexpr std::uint32_t kDimofsZ = 8;
constexpr std::uint32_t kDimofsButton0 = 12;
constexpr int kWheelDelta = 120;
constexpr int kSwHide = 0;
constexpr int kSwShowMinimized = 2;
constexpr int kSwMaximize = 3;
constexpr int kSwShowNoActivate = 4;
constexpr int kSwMinimize = 6;
constexpr int kSwShowMinNoActive = 7;
constexpr int kSwShowNa = 8;
constexpr int kSwRestore = 9;
constexpr unsigned kVkLShift = 0xA0;
constexpr unsigned kVkRShift = 0xA1;
constexpr unsigned kVkLControl = 0xA2;
constexpr unsigned kVkRControl = 0xA3;
constexpr unsigned kVkLMenu = 0xA4;
constexpr unsigned kVkRMenu = 0xA5;

// SDL's video, started on the main thread by the first window. Everything below that touches
// SDL or the state here runs on the main thread (SDL's windows are the main thread's on macOS):
// a call from another thread is forwarded to it (OnMain), and a window is made only there.
std::atomic<bool> GReady = false;

// The process's main thread, by the system (SDL_IsMainThread takes any thread for the main one
// before SDL is started).
bool OnMainThread()
{
#if defined(__APPLE__)
    return pthread_main_np() != 0;
#else
    return static_cast<pid_t>(syscall(SYS_gettid)) == getpid();
#endif
}

// Calls forwarded from other threads, in the order they were made, for the main thread.
std::mutex GForwardedLock;
std::deque<std::function<void()>> GForwarded;

// Runs the forwarded calls (on the main thread), oldest first, including any one of them
// forwards.
void RunForwarded()
{
    for (;;)
    {
        std::function<void()> Next;
        {
            std::lock_guard<std::mutex> Guard(GForwardedLock);
            if (GForwarded.empty())
                return;
            Next = std::move(GForwarded.front());
            GForwarded.pop_front();
        }
        Next();
    }
}

// Runs Call on the main thread, after every call forwarded before it: at once there, else
// queued and run when the main thread next takes its events or makes a call of its own
// (SDL_RunOnMainThread wakes it; not waited for, so that a thread the main thread waits on
// cannot deadlock). False when it cannot be forwarded; dropped before SDL is started, when
// there is no window.
bool OnMain(std::function<void()> Call)
{
    if (OnMainThread())
    {
        RunForwarded();
        Call();
        return true;
    }
    if (!GReady)
        return false;
    {
        std::lock_guard<std::mutex> Guard(GForwardedLock);
        GForwarded.push_back(std::move(Call));
    }
    if (!SDL_RunOnMainThread([](void*) { RunForwarded(); }, nullptr, false))
    {
        // Not queued with SDL: still run by the main thread's next Pump.
        std::fprintf(stderr, "[user32_sdl] SDL_RunOnMainThread: %s\n", SDL_GetError());
    }
    return true;
}

// Windows user32 has destroyed, from any thread, before the main thread destroys their SDL
// window: their buffered input is dropped from then on.
std::mutex GDestroyedLock;
std::vector<SDL_WindowID> GDestroyed;

bool IsDestroyed(SDL_WindowID Id)
{
    std::lock_guard<std::mutex> Guard(GDestroyedLock);
    return std::find(GDestroyed.begin(), GDestroyed.end(), Id) != GDestroyed.end();
}

// Windows whose forwarded show command SDL refused: their show state goes back to user32 as
// SDL has it, at the next Pump (main thread only).
// Each with the number of the command SDL refused: the window's show commands are numbered as
// they are made, on any thread (GShowCommands), and a window that got a newer command since is
// left to that command.
struct FReconcile
{
    SDL_WindowID Id;
    std::uint64_t Command;
};
std::vector<FReconcile> GReconcile;
std::mutex GShowCommandsLock;
std::map<SDL_WindowID, std::uint64_t> GShowCommands;

std::uint64_t NextShowCommand(SDL_WindowID Id)
{
    std::lock_guard<std::mutex> Guard(GShowCommandsLock);
    return ++GShowCommands[Id];
}

bool IsLatestShowCommand(SDL_WindowID Id, std::uint64_t Command)
{
    std::lock_guard<std::mutex> Guard(GShowCommandsLock);
    auto const Found = GShowCommands.find(Id);
    return Found != GShowCommands.end() && Found->second == Command;
}

// The display's modes, read on the main thread when SDL starts and when the displays change,
// for DisplayMode from any thread.
struct FMode
{
    int Width = 0;
    int Height = 0;
    int Bits = 0;
    int RefreshHz = 0;
};
std::mutex GModesLock;
FMode GDesktopMode;
std::vector<FMode> GModes;

FMode ModeOf(const SDL_DisplayMode& Mode)
{
    FMode Out;
    Out.Width = Mode.w;
    Out.Height = Mode.h;
    Out.Bits = static_cast<int>(SDL_BITSPERPIXEL(Mode.format)) >= 24 ? 32 : 16;
    Out.RefreshHz = Mode.refresh_rate > 0.f ? static_cast<int>(Mode.refresh_rate + 0.5f) : 60;
    return Out;
}

void ReadModes()
{
    SDL_DisplayID const Display = SDL_GetPrimaryDisplay();
    FMode Desktop;
    if (const SDL_DisplayMode* const Mode = SDL_GetDesktopDisplayMode(Display))
        Desktop = ModeOf(*Mode);
    std::vector<FMode> Modes;
    int Count = 0;
    SDL_DisplayMode** const List = SDL_GetFullscreenDisplayModes(Display, &Count);
    for (int Index = 0; List != nullptr && Index < Count; ++Index)
        Modes.push_back(ModeOf(*List[Index]));
    SDL_free(List);
    std::lock_guard<std::mutex> Guard(GModesLock);
    GDesktopMode = Desktop;
    GModes = std::move(Modes);
}

// A window's geometry as the events delivered so far left it (desktop position, client size),
// and the windowed bounds asked for while it was fullscreen, applied when it leaves fullscreen.
struct FScreenWindow
{
    int X = 0;
    int Y = 0;
    int Width = 0;
    int Height = 0;
    bool bWindowedPending = false;
    int WindowedX = 0;
    int WindowedY = 0;
    int WindowedWidth = 0;
    int WindowedHeight = 0;
};
std::map<SDL_WindowID, FScreenWindow> GWindows;

// Events taken from SDL and not yet delivered: Pump takes everything SDL has, then delivers one
// at a time, so a window procedure that pumps again (a nested message loop) gets the events
// after the one it is handling, and new ones. Typed text is copied out of the event.
struct FPending
{
    SDL_Event Event;
    std::string Text;
};
std::deque<FPending> GPending;

// The state as of the event being delivered, not SDL's current state, which may be later: the
// mouse buttons held, the modifiers, and the fractions of relative motion and of wheel notches
// not handed on yet.
SDL_MouseButtonFlags GButtons = 0;
SDL_Keymod GMods = SDL_KMOD_NONE;
float GMotionRestX = 0.f;
float GMotionRestY = 0.f;
float GWheelRest = 0.f;

bool Ready()
{
    if (!GReady)
    {
        // SDL_EVENT_QUIT comes only from the application (the macOS application menu's Quit, a
        // signal), not after a window's close button, which is a close request of its own.
        SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0");
        GReady = SDL_InitSubSystem(SDL_INIT_VIDEO);
        if (GReady)
            ReadModes();
    }
    if (!GReady)
        std::fprintf(stderr, "[user32_sdl] SDL_InitSubSystem(VIDEO): %s\n", SDL_GetError());
    return GReady;
}

SDL_Window* WindowOf(std::uint32_t Id)
{
    return SDL_GetWindowFromID(static_cast<SDL_WindowID>(Id));
}

// The US layout's virtual key for a physical key, and whether Windows calls it extended.
struct FKey
{
    unsigned Vk;
    bool bExtended;
};
FKey KeyOf(SDL_Scancode Code)
{
    if (Code >= SDL_SCANCODE_A && Code <= SDL_SCANCODE_Z)
        return {static_cast<unsigned>('A' + (Code - SDL_SCANCODE_A)), false};
    if (Code >= SDL_SCANCODE_1 && Code <= SDL_SCANCODE_9)
        return {static_cast<unsigned>('1' + (Code - SDL_SCANCODE_1)), false};
    if (Code >= SDL_SCANCODE_F1 && Code <= SDL_SCANCODE_F12)
        return {static_cast<unsigned>(0x70 + (Code - SDL_SCANCODE_F1)), false};
    if (Code >= SDL_SCANCODE_KP_1 && Code <= SDL_SCANCODE_KP_9)
        return {static_cast<unsigned>(0x61 + (Code - SDL_SCANCODE_KP_1)), false};
    switch (Code)
    {
    case SDL_SCANCODE_0:
        return {'0', false};
    case SDL_SCANCODE_RETURN:
        return {0x0D, false};
    case SDL_SCANCODE_ESCAPE:
        return {0x1B, false};
    case SDL_SCANCODE_BACKSPACE:
        return {0x08, false};
    case SDL_SCANCODE_TAB:
        return {0x09, false};
    case SDL_SCANCODE_SPACE:
        return {0x20, false};
    case SDL_SCANCODE_MINUS:
        return {0xBD, false};
    case SDL_SCANCODE_EQUALS:
        return {0xBB, false};
    case SDL_SCANCODE_LEFTBRACKET:
        return {0xDB, false};
    case SDL_SCANCODE_RIGHTBRACKET:
        return {0xDD, false};
    case SDL_SCANCODE_BACKSLASH:
        return {0xDC, false};
    case SDL_SCANCODE_SEMICOLON:
        return {0xBA, false};
    case SDL_SCANCODE_APOSTROPHE:
        return {0xDE, false};
    case SDL_SCANCODE_GRAVE:
        return {0xC0, false};
    case SDL_SCANCODE_COMMA:
        return {0xBC, false};
    case SDL_SCANCODE_PERIOD:
        return {0xBE, false};
    case SDL_SCANCODE_SLASH:
        return {0xBF, false};
    case SDL_SCANCODE_CAPSLOCK:
        return {0x14, false};
    case SDL_SCANCODE_PRINTSCREEN:
        return {0x2C, true};
    case SDL_SCANCODE_SCROLLLOCK:
        return {0x91, false};
    case SDL_SCANCODE_PAUSE:
        return {0x13, false};
    case SDL_SCANCODE_INSERT:
        return {0x2D, true};
    case SDL_SCANCODE_HOME:
        return {0x24, true};
    case SDL_SCANCODE_PAGEUP:
        return {0x21, true};
    case SDL_SCANCODE_DELETE:
        return {0x2E, true};
    case SDL_SCANCODE_END:
        return {0x23, true};
    case SDL_SCANCODE_PAGEDOWN:
        return {0x22, true};
    case SDL_SCANCODE_RIGHT:
        return {0x27, true};
    case SDL_SCANCODE_LEFT:
        return {0x25, true};
    case SDL_SCANCODE_DOWN:
        return {0x28, true};
    case SDL_SCANCODE_UP:
        return {0x26, true};
    case SDL_SCANCODE_NUMLOCKCLEAR:
        return {0x90, true};
    case SDL_SCANCODE_KP_DIVIDE:
        return {0x6F, true};
    case SDL_SCANCODE_KP_MULTIPLY:
        return {0x6A, false};
    case SDL_SCANCODE_KP_MINUS:
        return {0x6D, false};
    case SDL_SCANCODE_KP_PLUS:
        return {0x6B, false};
    case SDL_SCANCODE_KP_ENTER:
        return {0x0D, true};
    case SDL_SCANCODE_KP_0:
        return {0x60, false};
    case SDL_SCANCODE_KP_PERIOD:
        return {0x6E, false};
    case SDL_SCANCODE_APPLICATION:
        return {0x5D, true};
    // The modifiers by side (user32 keeps each side's state, and its messages carry VK_SHIFT,
    // VK_CONTROL and VK_MENU); right Ctrl and right Alt are extended.
    case SDL_SCANCODE_LCTRL:
        return {kVkLControl, false};
    case SDL_SCANCODE_RCTRL:
        return {kVkRControl, true};
    case SDL_SCANCODE_LSHIFT:
        return {kVkLShift, false};
    case SDL_SCANCODE_RSHIFT:
        return {kVkRShift, false};
    case SDL_SCANCODE_LALT:
        return {kVkLMenu, false};
    case SDL_SCANCODE_RALT:
        return {kVkRMenu, true};
    case SDL_SCANCODE_LGUI:
        return {0x5B, true};
    case SDL_SCANCODE_RGUI:
        return {0x5C, true};
    default:
        return {0, false};
    }
}

// Decodes one UTF-8 character at Text; the bytes it took in Length.
std::uint32_t NextUtf8(const char* Text, int* Length)
{
    unsigned char const Lead = static_cast<unsigned char>(Text[0]);
    int const Extra = Lead < 0x80 ? 0 : (Lead >> 5) == 0x6 ? 1 : (Lead >> 4) == 0xE ? 2 : 3;
    std::uint32_t Code = Extra == 0   ? Lead
                         : Extra == 1 ? (Lead & 0x1Fu)
                         : Extra == 2 ? (Lead & 0x0Fu)
                                      : (Lead & 0x07u);
    int Index = 1;
    for (; Index <= Extra && (static_cast<unsigned char>(Text[Index]) & 0xC0) == 0x80; ++Index)
        Code = (Code << 6) | (static_cast<unsigned char>(Text[Index]) & 0x3Fu);
    *Length = Index;
    return Code;
}

// MK_ flags of the buttons and modifiers held as of the event delivered, for the mouse
// messages' wParam.
std::uintptr_t ButtonFlags()
{
    std::uintptr_t Flags = 0;
    if ((GButtons & SDL_BUTTON_LMASK) != 0)
        Flags |= 0x0001;
    if ((GButtons & SDL_BUTTON_RMASK) != 0)
        Flags |= 0x0002;
    if ((GMods & SDL_KMOD_SHIFT) != 0)
        Flags |= 0x0004;
    if ((GMods & SDL_KMOD_CTRL) != 0)
        Flags |= 0x0008;
    if ((GButtons & SDL_BUTTON_MMASK) != 0)
        Flags |= 0x0010;
    return Flags;
}

// A point in a window's client area on the desktop, from the window's position as the events
// delivered so far left it.
void ToDesktop(SDL_WindowID Id, float ClientX, float ClientY, int* X, int* Y)
{
    auto const Found = GWindows.find(Id);
    int const OriginX = Found != GWindows.end() ? Found->second.X : 0;
    int const OriginY = Found != GWindows.end() ? Found->second.Y : 0;
    *X = OriginX + static_cast<int>(ClientX);
    *Y = OriginY + static_cast<int>(ClientY);
}

// The whole units of Value plus the fraction left from before, keeping the new fraction.
int WholeUnits(float Value, float* Rest)
{
    *Rest += Value;
    int const Whole = static_cast<int>(*Rest);
    *Rest -= static_cast<float>(Whole);
    return Whole;
}

void ApplyWindowed(SDL_Window* Window, FScreenWindow& State)
{
    if (!State.bWindowedPending)
        return;
    State.bWindowedPending = false;
    SDL_SetWindowPosition(Window, State.WindowedX, State.WindowedY);
    if (State.WindowedWidth > 0 && State.WindowedHeight > 0)
        SDL_SetWindowSize(Window, State.WindowedWidth, State.WindowedHeight);
}

std::intptr_t PointParam(int X, int Y)
{
    return static_cast<std::intptr_t>(
        static_cast<std::uint32_t>((X & 0xFFFF) | ((Y & 0xFFFF) << 16)));
}

// The modifier flag of a modifier key; none for other keys.
SDL_Keymod ModifierOf(SDL_Scancode Code)
{
    switch (Code)
    {
    case SDL_SCANCODE_LSHIFT:
        return SDL_KMOD_LSHIFT;
    case SDL_SCANCODE_RSHIFT:
        return SDL_KMOD_RSHIFT;
    case SDL_SCANCODE_LCTRL:
        return SDL_KMOD_LCTRL;
    case SDL_SCANCODE_RCTRL:
        return SDL_KMOD_RCTRL;
    case SDL_SCANCODE_LALT:
        return SDL_KMOD_LALT;
    case SDL_SCANCODE_RALT:
        return SDL_KMOD_RALT;
    case SDL_SCANCODE_LGUI:
        return SDL_KMOD_LGUI;
    case SDL_SCANCODE_RGUI:
        return SDL_KMOD_RGUI;
    default:
        return SDL_KMOD_NONE;
    }
}

// DirectInput's button for an SDL button: left, right, middle, then X1 and X2 as 3 and 4.
std::uint32_t DirectInputButton(Uint8 Button)
{
    switch (Button)
    {
    case SDL_BUTTON_LEFT:
        return 0;
    case SDL_BUTTON_RIGHT:
        return 1;
    case SDL_BUTTON_MIDDLE:
        return 2;
    default:
        return Button - 1u;
    }
}

// The window an input event is for; 0 for other events.
SDL_WindowID InputWindow(const SDL_Event& Event)
{
    switch (Event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        return Event.key.windowID;
    case SDL_EVENT_TEXT_INPUT:
        return Event.text.windowID;
    case SDL_EVENT_MOUSE_MOTION:
        return Event.motion.windowID;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        return Event.button.windowID;
    case SDL_EVENT_MOUSE_WHEEL:
        return Event.wheel.windowID;
    default:
        return 0;
    }
}

bool IsInput(const SDL_Event& Event)
{
    switch (Event.type)
    {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
        return true;
    default:
        return false;
    }
}

void Deliver(const FPending& Pending, const FUser32Sink* Sink)
{
    SDL_Event const& Event = Pending.Event;
    // Input for a window that is gone (destroyed by an earlier event's handler) or not one of
    // user32's: dropped before it changes the mouse, the modifiers or DirectInput.
    if (IsInput(Event) &&
        (GWindows.count(InputWindow(Event)) == 0 || IsDestroyed(InputWindow(Event))))
    {
        // A release still lets go of what it releases (the live key state, the buttons held,
        // DirectInput's button), without a message: it starts nothing new.
        if (Event.type == SDL_EVENT_KEY_UP)
        {
            // Only the modifier this releases: the event's whole modifier state may hold
            // presses that were dropped with the window.
            GMods = static_cast<SDL_Keymod>(GMods & ~ModifierOf(Event.key.scancode));
            FKey const Key = KeyOf(Event.key.scancode);
            if (Key.Vk != 0)
                Sink->ReleaseKey(Key.Vk);
        }
        else if (Event.type == SDL_EVENT_MOUSE_BUTTON_UP)
        {
            if (Event.button.button >= 1 && Event.button.button <= 32)
                GButtons &= ~SDL_BUTTON_MASK(Event.button.button);
            std::uint32_t const Button = DirectInputButton(Event.button.button);
            if (Button < 8)
                Sink->MouseRaw(kDimofsButton0 + Button, 0u);
        }
        return;
    }
    if (Event.type >= SDL_EVENT_DISPLAY_FIRST && Event.type <= SDL_EVENT_DISPLAY_LAST)
    {
        ReadModes();
        return;
    }
    switch (Event.type)
    {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        // The close button: WM_SYSCOMMAND SC_CLOSE, as Windows sends it.
        Sink->Send(Event.window.windowID, kWmSysCommand, kScClose, 0);
        break;
    case SDL_EVENT_QUIT:
    {
        // The application is asked to quit (the macOS application menu, a signal): each window
        // gets the close button's WM_SYSCOMMAND SC_CLOSE. A window's own close button does not
        // also raise this (SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE is off).
        std::vector<SDL_WindowID> Ids;
        for (auto const& Entry : GWindows)
            Ids.push_back(Entry.first);
        for (SDL_WindowID const Id : Ids)
            if (GWindows.count(Id) != 0)
                Sink->Send(Id, kWmSysCommand, kScClose, 0);
        break;
    }
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        Sink->Activate(Event.window.windowID, Event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
        break;
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
    {
        // The event's own geometry (data1, data2), with the rest as the events before it left
        // it: a later move or resize may already be in SDL's state.
        auto const Found = GWindows.find(Event.window.windowID);
        if (Found == GWindows.end())
            break;
        FScreenWindow& Window = Found->second;
        if (Event.type == SDL_EVENT_WINDOW_MOVED)
        {
            Window.X = Event.window.data1;
            Window.Y = Event.window.data2;
        }
        else
        {
            Window.Width = Event.window.data1;
            Window.Height = Event.window.data2;
        }
        Sink->Bounds(Event.window.windowID, Window.X, Window.Y, Window.Width, Window.Height);
        break;
    }
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    {
        // The window's own minimize and maximize buttons, the system's restore, with the size
        // of this transition: the resize SDL queued after it (before the window's next show
        // change), else, if another show change of the window is queued, the size the events
        // left (this one did not resize), else the size the window has now (SDL may report the
        // resize later; the one after this then changes nothing).
        auto const Found = GWindows.find(Event.window.windowID);
        SDL_Window* const Window = WindowOf(Event.window.windowID);
        if (Found == GWindows.end() || Window == nullptr)
            break;
        FScreenWindow& State = Found->second;
        if (Event.type != SDL_EVENT_WINDOW_MINIMIZED)
        {
            bool bSized = false;
            bool bLaterShowChange = false;
            for (const FPending& Later : GPending)
            {
                SDL_EventType const Type = static_cast<SDL_EventType>(Later.Event.type);
                bool const bResize = Type == SDL_EVENT_WINDOW_RESIZED;
                bool const bShowChange = Type == SDL_EVENT_WINDOW_MINIMIZED ||
                                         Type == SDL_EVENT_WINDOW_MAXIMIZED ||
                                         Type == SDL_EVENT_WINDOW_RESTORED;
                if ((!bResize && !bShowChange) ||
                    Later.Event.window.windowID != Event.window.windowID)
                    continue;
                if (bResize)
                {
                    State.Width = Later.Event.window.data1;
                    State.Height = Later.Event.window.data2;
                    bSized = true;
                }
                else
                    bLaterShowChange = true;
                break;
            }
            if (!bSized && !bLaterShowChange)
                SDL_GetWindowSize(Window, &State.Width, &State.Height);
        }
        Sink->ShowState(Event.window.windowID,
                        Event.type == SDL_EVENT_WINDOW_MINIMIZED   ? 1
                        : Event.type == SDL_EVENT_WINDOW_MAXIMIZED ? 2
                                                                   : 0,
                        State.Width, State.Height);
        break;
    }
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    {
        // The windowed bounds asked for while fullscreen, if SetFrame could not apply them; not
        // while the window is fullscreen again (a late event), which keeps them for its next
        // exit.
        auto const Found = GWindows.find(Event.window.windowID);
        SDL_Window* const Window = WindowOf(Event.window.windowID);
        if (Found != GWindows.end() && Window != nullptr &&
            (SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN) == 0)
            ApplyWindowed(Window, Found->second);
        break;
    }
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        GMods = Event.key.mod;
        FKey const Key = KeyOf(Event.key.scancode);
        if (Key.Vk != 0)
            Sink->Key(Event.key.windowID, Key.Vk, Key.bExtended, Event.key.down, Event.key.repeat);
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        for (const char* Text = Pending.Text.c_str(); *Text != 0;)
        {
            int Length = 1;
            std::uint32_t const Character = NextUtf8(Text, &Length);
            Sink->Char(Event.text.windowID, Character);
            Text += Length;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
    {
        GButtons = Event.motion.state;
        int const X = static_cast<int>(Event.motion.x);
        int const Y = static_cast<int>(Event.motion.y);
        int ScreenX = 0;
        int ScreenY = 0;
        ToDesktop(Event.motion.windowID, Event.motion.x, Event.motion.y, &ScreenX, &ScreenY);
        Sink->MouseMove(Event.motion.windowID, X, Y, ScreenX, ScreenY);
        Sink->Post(Event.motion.windowID, kWmMouseMove, ButtonFlags(), PointParam(X, Y));
        // DirectInput's counts are whole; a fraction of a motion waits for the next.
        int const DeltaX = WholeUnits(Event.motion.xrel, &GMotionRestX);
        int const DeltaY = WholeUnits(Event.motion.yrel, &GMotionRestY);
        if (DeltaX != 0)
            Sink->MouseRaw(kDimofsX, static_cast<std::uint32_t>(DeltaX));
        if (DeltaY != 0)
            Sink->MouseRaw(kDimofsY, static_cast<std::uint32_t>(DeltaY));
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        bool const bDown = Event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        // The buttons held after this one, as Windows reports them with the button's message.
        if (Event.button.button >= 1 && Event.button.button <= 32)
        {
            if (bDown)
                GButtons |= SDL_BUTTON_MASK(Event.button.button);
            else
                GButtons &= ~SDL_BUTTON_MASK(Event.button.button);
        }
        int const X = static_cast<int>(Event.button.x);
        int const Y = static_cast<int>(Event.button.y);
        // The cursor where the button was pressed, for the message's MSG.pt.
        int ScreenX = 0;
        int ScreenY = 0;
        ToDesktop(Event.button.windowID, Event.button.x, Event.button.y, &ScreenX, &ScreenY);
        Sink->MouseMove(Event.button.windowID, X, Y, ScreenX, ScreenY);
        unsigned Message = 0;
        std::uint32_t Button = 0;
        switch (Event.button.button)
        {
        case SDL_BUTTON_LEFT:
            Message = bDown ? kWmLButtonDown : kWmLButtonUp;
            Button = 0;
            break;
        case SDL_BUTTON_RIGHT:
            Message = bDown ? kWmRButtonDown : kWmRButtonUp;
            Button = 1;
            break;
        case SDL_BUTTON_MIDDLE:
            Message = bDown ? kWmMButtonDown : kWmMButtonUp;
            Button = 2;
            break;
        default:
            Button = Event.button.button - 1u; // X1, X2: DirectInput's buttons 3 and 4
            break;
        }
        if (Message != 0)
            Sink->Post(Event.button.windowID, Message, ButtonFlags(), PointParam(X, Y));
        if (Button < 8)
            Sink->MouseRaw(kDimofsButton0 + Button, bDown ? 0x80u : 0u);
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
    {
        // Whole notches, the fraction kept for the next event in the same direction (a mouse
        // wheel's notches, as WM_MOUSEWHEEL's multiples of WHEEL_DELTA; a trackpad's scrolling
        // adds up to them).
        float Amount = Event.wheel.y;
        if (Event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            Amount = -Amount;
        if ((Amount > 0.f && GWheelRest < 0.f) || (Amount < 0.f && GWheelRest > 0.f))
            GWheelRest = 0.f;
        int const Notches = WholeUnits(Amount, &GWheelRest);
        if (Notches == 0)
            break;
        int ScreenX = 0;
        int ScreenY = 0;
        ToDesktop(Event.wheel.windowID, Event.wheel.mouse_x, Event.wheel.mouse_y, &ScreenX,
                  &ScreenY);
        Sink->MouseMove(Event.wheel.windowID, static_cast<int>(Event.wheel.mouse_x),
                        static_cast<int>(Event.wheel.mouse_y), ScreenX, ScreenY);
        std::uintptr_t const WParam =
            ButtonFlags() | (static_cast<std::uintptr_t>(static_cast<std::uint16_t>(
                                 static_cast<std::int16_t>(Notches * kWheelDelta)))
                             << 16);
        // WM_MOUSEWHEEL carries the cursor's desktop position, as Windows sends it.
        Sink->Post(Event.wheel.windowID, kWmMouseWheel, WParam, PointParam(ScreenX, ScreenY));
        Sink->MouseRaw(kDimofsZ, static_cast<std::uint32_t>(Notches * kWheelDelta));
        break;
    }
    default:
        break;
    }
}

// A window is made on the main thread only (SDL_CreateWindow's rule, and where macOS keeps its
// windows): on another thread, false, and user32 keeps the window as a record.
bool CreateWindow(const char* Title, int X, int Y, int Width, int Height, bool bFramed,
                  bool bResizable, bool bVisible, std::uint32_t* OutId)
{
    if (OutId == nullptr)
        return false;
    if (!OnMainThread())
    {
        std::fprintf(stderr, "[user32_sdl] a window made off the main thread has no screen\n");
        return false;
    }
    if (!Ready())
        return false;
    SDL_WindowFlags Flags = 0;
    if (!bFramed)
        Flags |= SDL_WINDOW_BORDERLESS;
    if (bResizable)
        Flags |= SDL_WINDOW_RESIZABLE;
    if (!bVisible)
        Flags |= SDL_WINDOW_HIDDEN;
    SDL_Window* const Window = SDL_CreateWindow(
        Title != nullptr ? Title : "", Width > 0 ? Width : 1, Height > 0 ? Height : 1, Flags);
    if (Window == nullptr)
    {
        std::fprintf(stderr, "[user32_sdl] SDL_CreateWindow: %s\n", SDL_GetError());
        return false;
    }
    SDL_SetWindowPosition(Window, X, Y);
    // Typed text comes as SDL_EVENT_TEXT_INPUT only while text input is on.
    SDL_StartTextInput(Window);
    *OutId = static_cast<std::uint32_t>(SDL_GetWindowID(Window));
    if (*OutId == 0)
        return false;
    // The geometry user32 records for it, until its events say otherwise.
    FScreenWindow& State = GWindows[*OutId];
    State = {};
    State.X = X;
    State.Y = Y;
    State.Width = Width;
    State.Height = Height;
    return true;
}

void DestroyWindow(std::uint32_t Id)
{
    {
        std::lock_guard<std::mutex> Guard(GDestroyedLock);
        GDestroyed.push_back(Id);
    }
    OnMain(
        [Id]
        {
            GWindows.erase(Id);
            {
                std::lock_guard<std::mutex> Guard(GShowCommandsLock);
                GShowCommands.erase(Id);
            }
            if (SDL_Window* const Window = WindowOf(Id))
                SDL_DestroyWindow(Window);
            // Its buffered input is dropped by GWindows from now on (SDL's ids are not reused).
            std::lock_guard<std::mutex> Guard(GDestroyedLock);
            GDestroyed.erase(std::remove(GDestroyed.begin(), GDestroyed.end(), Id),
                             GDestroyed.end());
        });
}

// Position and size, asked for. The geometry the events report stays as they left it (earlier
// queued events are placed by it) until SDL's own move and resize events. A fullscreen window
// keeps the bounds for when it leaves fullscreen (SDL ignores both while it is fullscreen).
void SetBounds(std::uint32_t Id, int X, int Y, int Width, int Height)
{
    OnMain(
        [=]
        {
            SDL_Window* const Window = WindowOf(Id);
            auto const Found = GWindows.find(Id);
            if (Window == nullptr || Found == GWindows.end())
                return;
            FScreenWindow& State = Found->second;
            State.bWindowedPending = true;
            State.WindowedX = X;
            State.WindowedY = Y;
            State.WindowedWidth = Width;
            State.WindowedHeight = Height;
            if ((SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN) == 0)
                ApplyWindowed(Window, State);
        });
}

void SetFrame(std::uint32_t Id, bool bFramed, bool bResizable, bool bFullscreen)
{
    OnMain(
        [=]
        {
            SDL_Window* const Window = WindowOf(Id);
            if (Window == nullptr)
                return;
            bool const bWasFullscreen = (SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN) != 0;
            SDL_SetWindowBordered(Window, bFramed);
            SDL_SetWindowResizable(Window, bResizable);
            // Fullscreen on the desktop's mode: the game's frame is scaled to it by RPGPU.
            SDL_SetWindowFullscreenMode(Window, nullptr);
            SDL_SetWindowFullscreen(Window, bFullscreen);
            if (bWasFullscreen && !bFullscreen)
            {
                // The windowed bounds asked for while fullscreen, once SDL has left it (or, if
                // leaving takes longer, when SDL_EVENT_WINDOW_LEAVE_FULLSCREEN comes).
                SDL_SyncWindow(Window);
                auto const Found = GWindows.find(Id);
                if ((SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN) == 0 &&
                    Found != GWindows.end())
                    ApplyWindowed(Window, Found->second);
            }
        });
}

// Shown without being activated (made key on macOS): SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN off
// for the call, then as it was.
// The hint is set at override priority, which wins over the environment's; if it still does
// not take, the window is not shown rather than shown activated. Afterwards the hint is back
// to the environment's (or the default), and to a value set earlier at normal priority.
bool ShowWithoutActivating(SDL_Window* Window)
{
    // Activation already off (the environment, or the application at any priority): shown as
    // it is, the hint untouched.
    if (!SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true))
        return SDL_ShowWindow(Window);
    const char* const Before = SDL_GetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
    bool const bHadValue = Before != nullptr;
    std::string const Saved = bHadValue ? Before : "";
    bool const bSet =
        SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, "0", SDL_HINT_OVERRIDE) &&
        !SDL_GetHintBoolean(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, true);
    bool const bShown = bSet && SDL_ShowWindow(Window);
    // Back to the value from before at the lowest priority that holds it: the reset brings
    // back the environment's (or none); a value the application set over it comes back at
    // normal priority, or at override where the environment's would win (SDL has no way to
    // read a hint's priority).
    SDL_ResetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
    auto const Holds = [&]
    {
        const char* const After = SDL_GetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
        return bHadValue ? After != nullptr && Saved == After : After == nullptr;
    };
    if (bHadValue && !Holds())
    {
        SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, Saved.c_str());
        if (!Holds())
            SDL_SetHintWithPriority(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, Saved.c_str(),
                                    SDL_HINT_OVERRIDE);
    }
    return bShown;
}

bool ShowOnMain(std::uint32_t Id, int Command)
{
    SDL_Window* const Window = WindowOf(Id);
    if (Window == nullptr)
        return false;
    switch (Command)
    {
    case kSwHide:
        return SDL_HideWindow(Window);
    case kSwMinimize:
        return SDL_MinimizeWindow(Window);
    case kSwShowMinimized:
    {
        // Shown as well: a hidden window takes the minimized state when it is shown.
        bool const bMinimized = SDL_MinimizeWindow(Window);
        return SDL_ShowWindow(Window) && bMinimized;
    }
    case kSwShowMinNoActive:
    {
        bool const bMinimized = SDL_MinimizeWindow(Window);
        return ShowWithoutActivating(Window) && bMinimized;
    }
    case kSwMaximize:
    {
        bool const bShown = SDL_ShowWindow(Window);
        return SDL_MaximizeWindow(Window) && bShown;
    }
    case kSwRestore:
    {
        bool const bShown = SDL_ShowWindow(Window);
        return SDL_RestoreWindow(Window) && bShown;
    }
    case kSwShowNoActivate:
    case kSwShowNa:
        // Shown, neither activated nor raised in front of the others.
        return ShowWithoutActivating(Window);
    default:
    {
        // SW_SHOW, SW_SHOWNORMAL: a minimized window is restored, as Windows shows it.
        bool const bShown = SDL_ShowWindow(Window);
        if ((SDL_GetWindowFlags(Window) & SDL_WINDOW_MINIMIZED) != 0)
            SDL_RestoreWindow(Window);
        SDL_RaiseWindow(Window);
        return bShown;
    }
    }
}

// False when SDL refuses the command (maximizing a window that cannot be resized); from another
// thread the command is forwarded, and taken as done.
bool Show(std::uint32_t Id, int Command)
{
    // Numbered when it is made (user32 has recorded its style by then), on whatever thread.
    std::uint64_t const Number = NextShowCommand(Id);
    if (OnMainThread())
    {
        RunForwarded();
        return ShowOnMain(Id, Command);
    }
    // Forwarded: a command SDL then refuses sends the window's show state back to user32 as SDL
    // has it (the next Pump), so a maximize recorded for it is undone, unless a newer command
    // for the window has been made by then.
    return OnMain(
        [=]
        {
            if (!ShowOnMain(Id, Command))
                GReconcile.push_back({Id, Number});
        });
}

void SetTitle(std::uint32_t Id, const char* Title)
{
    OnMain(
        [Id, Text = std::string(Title != nullptr ? Title : "")]
        {
            if (SDL_Window* const Window = WindowOf(Id))
                SDL_SetWindowTitle(Window, Text.c_str());
        });
}

void Pump(const FUser32Sink* Sink)
{
    if (!GReady || Sink == nullptr || !OnMainThread())
        return;
    RunForwarded();
    // Show states SDL kept against a forwarded command.
    std::vector<FReconcile> Reconcile;
    Reconcile.swap(GReconcile);
    for (FReconcile const& Refused : Reconcile)
    {
        SDL_WindowID const Id = Refused.Id;
        SDL_Window* const Window = WindowOf(Id);
        auto const Found = GWindows.find(Id);
        if (Window == nullptr || Found == GWindows.end() ||
            !IsLatestShowCommand(Id, Refused.Command))
            continue;
        SDL_WindowFlags const Flags = SDL_GetWindowFlags(Window);
        Sink->ShowState(Id,
                        (Flags & SDL_WINDOW_MINIMIZED) != 0   ? 1
                        : (Flags & SDL_WINDOW_MAXIMIZED) != 0 ? 2
                                                              : 0,
                        Found->second.Width, Found->second.Height);
    }
    FPending Pending;
    while (SDL_PollEvent(&Pending.Event))
    {
        Pending.Text =
            Pending.Event.type == SDL_EVENT_TEXT_INPUT && Pending.Event.text.text != nullptr
                ? Pending.Event.text.text
                : "";
        GPending.push_back(Pending);
    }
    // Each event leaves the queue before it is delivered: a nested Pump goes on with the next.
    while (!GPending.empty())
    {
        FPending const Next = std::move(GPending.front());
        GPending.pop_front();
        Deliver(Next, Sink);
    }
}

void Wait(int Milliseconds)
{
    if (GReady && OnMainThread())
        SDL_WaitEventTimeout(nullptr, Milliseconds);
}

void ShowCursor(bool bShow)
{
    OnMain(
        [bShow]
        {
            if (!GReady)
                return;
            if (bShow)
                SDL_ShowCursor();
            else
                SDL_HideCursor();
        });
}

void WarpMouse(std::uint32_t Id, int X, int Y)
{
    OnMain(
        [=]
        {
            if (SDL_Window* const Window = WindowOf(Id))
                SDL_WarpMouseInWindow(Window, static_cast<float>(X), static_cast<float>(Y));
        });
}

void ClipCursor(std::uint32_t Id, bool bClip)
{
    OnMain(
        [=]
        {
            if (SDL_Window* const Window = WindowOf(Id))
                SDL_SetWindowMouseGrab(Window, bClip);
        });
}

// SDL started (on the main thread only), so that the display's modes are known before the first
// window is sized.
bool Start()
{
    return OnMainThread() && Ready();
}

// From the modes read on the main thread, so any thread may ask; false before SDL is started.
bool DisplayMode(int Index, int* Width, int* Height, int* Bits, int* RefreshHz)
{
    if (!GReady)
        return false;
    std::lock_guard<std::mutex> Guard(GModesLock);
    FMode Mode;
    if (Index < 0)
        Mode = GDesktopMode;
    else if (static_cast<std::size_t>(Index) < GModes.size())
        Mode = GModes[static_cast<std::size_t>(Index)];
    if (Mode.Width <= 0 || Mode.Height <= 0)
        return false;
    *Width = Mode.Width;
    *Height = Mode.Height;
    *Bits = Mode.Bits;
    *RefreshHz = Mode.RefreshHz;
    return true;
}

const FUser32Backend GBackend = {
    kUser32BackendVersion,
    &CreateWindow,
    &DestroyWindow,
    &SetBounds,
    &SetFrame,
    &Show,
    &SetTitle,
    &Pump,
    &Wait,
    &ShowCursor,
    &WarpMouse,
    &ClipCursor,
    &DisplayMode,
    &Start,
};
} // namespace

extern "C" __attribute__((visibility("default"))) const FUser32Backend* FlUser32Backend()
{
    return &GBackend;
}
