// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): see user32_screen_probe.h. Not part of retail.

#include "user32_screen_probe.h"

#include <SDL3/SDL.h>

namespace ScreenProbe
{
namespace
{
SDL_Window* WindowOf(std::uint32_t Id)
{
    return SDL_GetWindowFromID(static_cast<SDL_WindowID>(Id));
}

void Push(SDL_Event& Event)
{
    Event.common.timestamp = SDL_GetTicksNS();
    SDL_PushEvent(&Event);
}

void PushWindowEvent(SDL_EventType Type, std::uint32_t Id, int Data1, int Data2)
{
    SDL_Event Event = {};
    Event.type = Type;
    Event.window.windowID = Id;
    Event.window.data1 = Data1;
    Event.window.data2 = Data2;
    Push(Event);
}

void PushKey(std::uint32_t Id, SDL_Scancode Code, SDL_Keymod Mod, bool bDown)
{
    SDL_Event Event = {};
    Event.type = bDown ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
    Event.key.windowID = Id;
    Event.key.scancode = Code;
    Event.key.mod = Mod;
    Event.key.down = bDown;
    Push(Event);
}
} // namespace

bool VideoInitialized()
{
    return SDL_WasInit(SDL_INIT_VIDEO) != 0;
}

bool WindowSize(std::uint32_t Id, int* Width, int* Height)
{
    SDL_Window* const Window = WindowOf(Id);
    return Window != nullptr && SDL_GetWindowSize(Window, Width, Height);
}

bool WindowPosition(std::uint32_t Id, int* X, int* Y)
{
    SDL_Window* const Window = WindowOf(Id);
    return Window != nullptr && SDL_GetWindowPosition(Window, X, Y);
}

std::string WindowTitle(std::uint32_t Id)
{
    SDL_Window* const Window = WindowOf(Id);
    return Window != nullptr ? SDL_GetWindowTitle(Window) : "";
}

bool IsFullscreen(std::uint32_t Id)
{
    SDL_Window* const Window = WindowOf(Id);
    return Window != nullptr && (SDL_GetWindowFlags(Window) & SDL_WINDOW_FULLSCREEN) != 0;
}

bool IsResizable(std::uint32_t Id)
{
    SDL_Window* const Window = WindowOf(Id);
    return Window != nullptr && (SDL_GetWindowFlags(Window) & SDL_WINDOW_RESIZABLE) != 0;
}

bool IsMouseGrabbed(std::uint32_t Id)
{
    SDL_Window* const Window = WindowOf(Id);
    return Window != nullptr && (SDL_GetWindowFlags(Window) & SDL_WINDOW_MOUSE_GRABBED) != 0;
}

bool CursorVisible()
{
    return SDL_CursorVisible();
}

std::string ActivateHint(bool* bSet)
{
    const char* const Value = SDL_GetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
    *bSet = Value != nullptr;
    return Value != nullptr ? Value : "";
}

void SetActivateHint(const char* Value)
{
    SDL_SetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN, Value);
}

void ResetActivateHint()
{
    SDL_ResetHint(SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN);
}

void PushModifier(std::uint32_t Id, EModifier Key, bool bDown)
{
    bool const bLeft = Key == EModifier::LeftShift;
    PushKey(Id, bLeft ? SDL_SCANCODE_LSHIFT : SDL_SCANCODE_RSHIFT,
            bDown ? (bLeft ? SDL_KMOD_LSHIFT : SDL_KMOD_RSHIFT) : SDL_KMOD_NONE, bDown);
}

void PushKeyA(std::uint32_t Id, bool bDown)
{
    PushKey(Id, SDL_SCANCODE_A, SDL_KMOD_NONE, bDown);
}

void PushButton(std::uint32_t Id, int Button, bool bDown, float X, float Y)
{
    SDL_Event Event = {};
    Event.type = bDown ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
    Event.button.windowID = Id;
    Event.button.button = static_cast<Uint8>(Button);
    Event.button.down = bDown;
    Event.button.clicks = 1;
    Event.button.x = X;
    Event.button.y = Y;
    Push(Event);
}

void PushMotion(std::uint32_t Id, float X, float Y, float DeltaX, float DeltaY)
{
    SDL_Event Event = {};
    Event.type = SDL_EVENT_MOUSE_MOTION;
    Event.motion.windowID = Id;
    Event.motion.x = X;
    Event.motion.y = Y;
    Event.motion.xrel = DeltaX;
    Event.motion.yrel = DeltaY;
    Push(Event);
}

void PushWheel(std::uint32_t Id, float Amount, float X, float Y)
{
    SDL_Event Event = {};
    Event.type = SDL_EVENT_MOUSE_WHEEL;
    Event.wheel.windowID = Id;
    Event.wheel.y = Amount;
    Event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    Event.wheel.mouse_x = X;
    Event.wheel.mouse_y = Y;
    Push(Event);
}

void PushMoved(std::uint32_t Id, int X, int Y)
{
    PushWindowEvent(SDL_EVENT_WINDOW_MOVED, Id, X, Y);
}

void PushResized(std::uint32_t Id, int Width, int Height)
{
    PushWindowEvent(SDL_EVENT_WINDOW_RESIZED, Id, Width, Height);
}

void PushFocus(std::uint32_t Id, bool bGained)
{
    PushWindowEvent(bGained ? SDL_EVENT_WINDOW_FOCUS_GAINED : SDL_EVENT_WINDOW_FOCUS_LOST, Id, 0,
                    0);
}

void PushShowState(std::uint32_t Id, int State)
{
    PushWindowEvent(State == 1   ? SDL_EVENT_WINDOW_MINIMIZED
                    : State == 2 ? SDL_EVENT_WINDOW_MAXIMIZED
                                 : SDL_EVENT_WINDOW_RESTORED,
                    Id, 0, 0);
}

void PushCloseRequest(std::uint32_t Id)
{
    PushWindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED, Id, 0, 0);
}

void PushQuit()
{
    SDL_Event Event = {};
    Event.type = SDL_EVENT_QUIT;
    Push(Event);
}
} // namespace ScreenProbe
