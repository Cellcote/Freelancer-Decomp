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

#include <cstdio>
#include <vector>

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
constexpr int kSwMinimize = 6;
constexpr int kSwRestore = 9;

bool GReady = false;

bool Ready()
{
    if (!GReady)
        GReady = SDL_InitSubSystem(SDL_INIT_VIDEO);
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
    // The modifiers as the game sees them in its key messages: VK_SHIFT, VK_CONTROL and VK_MENU
    // (right Ctrl and right Alt are extended).
    case SDL_SCANCODE_LCTRL:
        return {0x11, false};
    case SDL_SCANCODE_RCTRL:
        return {0x11, true};
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
        return {0x10, false};
    case SDL_SCANCODE_LALT:
        return {0x12, false};
    case SDL_SCANCODE_RALT:
        return {0x12, true};
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

// MK_ flags of the buttons held, for the mouse messages' wParam.
std::uintptr_t ButtonFlags()
{
    SDL_MouseButtonFlags const Buttons = SDL_GetMouseState(nullptr, nullptr);
    SDL_Keymod const Mods = SDL_GetModState();
    std::uintptr_t Flags = 0;
    if ((Buttons & SDL_BUTTON_LMASK) != 0)
        Flags |= 0x0001;
    if ((Buttons & SDL_BUTTON_RMASK) != 0)
        Flags |= 0x0002;
    if ((Mods & SDL_KMOD_SHIFT) != 0)
        Flags |= 0x0004;
    if ((Mods & SDL_KMOD_CTRL) != 0)
        Flags |= 0x0008;
    if ((Buttons & SDL_BUTTON_MMASK) != 0)
        Flags |= 0x0010;
    return Flags;
}

std::intptr_t PointParam(int X, int Y)
{
    return static_cast<std::intptr_t>(
        static_cast<std::uint32_t>((X & 0xFFFF) | ((Y & 0xFFFF) << 16)));
}

void Deliver(const SDL_Event& Event, const FUser32Sink* Sink)
{
    switch (Event.type)
    {
    case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
        // The close button: WM_SYSCOMMAND SC_CLOSE, as Windows sends it.
        Sink->Send(Event.window.windowID, kWmSysCommand, kScClose, 0);
        break;
    case SDL_EVENT_QUIT:
        break; // each window's close request comes on its own
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        Sink->Activate(Event.window.windowID, Event.type == SDL_EVENT_WINDOW_FOCUS_GAINED);
        break;
    case SDL_EVENT_WINDOW_MOVED:
    case SDL_EVENT_WINDOW_RESIZED:
        if (SDL_Window* const Window = WindowOf(Event.window.windowID))
        {
            int X = 0;
            int Y = 0;
            int W = 0;
            int H = 0;
            SDL_GetWindowPosition(Window, &X, &Y);
            SDL_GetWindowSize(Window, &W, &H);
            Sink->Bounds(Event.window.windowID, X, Y, W, H);
        }
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    {
        FKey const Key = KeyOf(Event.key.scancode);
        if (Key.Vk != 0)
            Sink->Key(Event.key.windowID, Key.Vk, Key.bExtended, Event.key.down, Event.key.repeat);
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        for (const char* Text = Event.text.text; Text != nullptr && *Text != 0;)
        {
            int Length = 1;
            std::uint32_t const Character = NextUtf8(Text, &Length);
            Sink->Char(Event.text.windowID, Character);
            Text += Length;
        }
        break;
    case SDL_EVENT_MOUSE_MOTION:
    {
        float GlobalX = 0.f;
        float GlobalY = 0.f;
        SDL_GetGlobalMouseState(&GlobalX, &GlobalY);
        int const X = static_cast<int>(Event.motion.x);
        int const Y = static_cast<int>(Event.motion.y);
        Sink->MouseMove(Event.motion.windowID, X, Y, static_cast<int>(GlobalX),
                        static_cast<int>(GlobalY));
        Sink->Post(Event.motion.windowID, kWmMouseMove, ButtonFlags(), PointParam(X, Y));
        int const DeltaX = static_cast<int>(Event.motion.xrel);
        int const DeltaY = static_cast<int>(Event.motion.yrel);
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
        int const X = static_cast<int>(Event.button.x);
        int const Y = static_cast<int>(Event.button.y);
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
        int Notches = Event.wheel.integer_y;
        if (Event.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            Notches = -Notches;
        if (Notches == 0)
            break;
        float GlobalX = 0.f;
        float GlobalY = 0.f;
        SDL_GetGlobalMouseState(&GlobalX, &GlobalY);
        std::uintptr_t const WParam =
            ButtonFlags() | (static_cast<std::uintptr_t>(static_cast<std::uint16_t>(
                                 static_cast<std::int16_t>(Notches * kWheelDelta)))
                             << 16);
        // WM_MOUSEWHEEL carries the cursor's desktop position, as Windows sends it.
        Sink->Post(Event.wheel.windowID, kWmMouseWheel, WParam,
                   PointParam(static_cast<int>(GlobalX), static_cast<int>(GlobalY)));
        Sink->MouseRaw(kDimofsZ, static_cast<std::uint32_t>(Notches * kWheelDelta));
        break;
    }
    default:
        break;
    }
}

bool CreateWindow(const char* Title, int X, int Y, int Width, int Height, bool bFramed,
                  bool bVisible, std::uint32_t* OutId)
{
    if (!Ready() || OutId == nullptr)
        return false;
    SDL_WindowFlags Flags = 0;
    if (!bFramed)
        Flags |= SDL_WINDOW_BORDERLESS;
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
    return *OutId != 0;
}

void DestroyWindow(std::uint32_t Id)
{
    if (SDL_Window* const Window = WindowOf(Id))
        SDL_DestroyWindow(Window);
}

void SetBounds(std::uint32_t Id, int X, int Y, int Width, int Height)
{
    SDL_Window* const Window = WindowOf(Id);
    if (Window == nullptr)
        return;
    if ((SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN) == 0)
        SDL_SetWindowPosition(Window, X, Y);
    if (Width > 0 && Height > 0)
        SDL_SetWindowSize(Window, Width, Height);
}

void SetFrame(std::uint32_t Id, bool bFramed, bool bFullscreen)
{
    SDL_Window* const Window = WindowOf(Id);
    if (Window == nullptr)
        return;
    SDL_SetWindowBordered(Window, bFramed);
    // Fullscreen on the desktop's mode: the game's frame is scaled to it by RPGPU.
    SDL_SetWindowFullscreenMode(Window, nullptr);
    SDL_SetWindowFullscreen(Window, bFullscreen);
}

void Show(std::uint32_t Id, int Command)
{
    SDL_Window* const Window = WindowOf(Id);
    if (Window == nullptr)
        return;
    switch (Command)
    {
    case kSwHide:
        SDL_HideWindow(Window);
        break;
    case kSwMinimize:
    case kSwShowMinimized:
        SDL_MinimizeWindow(Window);
        break;
    case kSwMaximize:
        SDL_ShowWindow(Window);
        SDL_MaximizeWindow(Window);
        break;
    case kSwRestore:
        SDL_ShowWindow(Window);
        SDL_RestoreWindow(Window);
        break;
    default:
        SDL_ShowWindow(Window);
        SDL_RaiseWindow(Window);
        break;
    }
}

void SetTitle(std::uint32_t Id, const char* Title)
{
    if (SDL_Window* const Window = WindowOf(Id))
        SDL_SetWindowTitle(Window, Title != nullptr ? Title : "");
}

void Pump(const FUser32Sink* Sink)
{
    if (!GReady || Sink == nullptr)
        return;
    SDL_Event Event;
    while (SDL_PollEvent(&Event))
        Deliver(Event, Sink);
}

void Wait(int Milliseconds)
{
    if (GReady)
        SDL_WaitEventTimeout(nullptr, Milliseconds);
}

void ShowCursor(bool bShow)
{
    if (!GReady)
        return;
    if (bShow)
        SDL_ShowCursor();
    else
        SDL_HideCursor();
}

void WarpMouse(std::uint32_t Id, int X, int Y)
{
    if (SDL_Window* const Window = WindowOf(Id))
        SDL_WarpMouseInWindow(Window, static_cast<float>(X), static_cast<float>(Y));
}

void ClipCursor(std::uint32_t Id, bool bClip)
{
    if (SDL_Window* const Window = WindowOf(Id))
        SDL_SetWindowMouseGrab(Window, bClip);
}

bool DisplayMode(int Index, int* Width, int* Height, int* Bits, int* RefreshHz)
{
    if (!Ready())
        return false;
    SDL_DisplayID const Display = SDL_GetPrimaryDisplay();
    const SDL_DisplayMode* Mode = nullptr;
    SDL_DisplayMode** Modes = nullptr;
    if (Index < 0)
        Mode = SDL_GetDesktopDisplayMode(Display);
    else
    {
        int Count = 0;
        Modes = SDL_GetFullscreenDisplayModes(Display, &Count);
        if (Modes != nullptr && Index < Count)
            Mode = Modes[Index];
    }
    bool const bFound = Mode != nullptr;
    if (bFound)
    {
        *Width = Mode->w;
        *Height = Mode->h;
        *Bits = static_cast<int>(SDL_BITSPERPIXEL(Mode->format)) >= 24 ? 32 : 16;
        *RefreshHz = Mode->refresh_rate > 0.f ? static_cast<int>(Mode->refresh_rate + 0.5f) : 60;
    }
    SDL_free(Modes);
    return bFound;
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
};
} // namespace

extern "C" __attribute__((visibility("default"))) const FUser32Backend* FlUser32Backend()
{
    return &GBackend;
}
