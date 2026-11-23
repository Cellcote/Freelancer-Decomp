// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): the seam between user32 (user32.cpp) and the screen behind its windows
// (user32_sdl.cpp, SDL3). Not part of retail.
//
// user32 loads the backend module (user32_sdl) the first time a top-level window is created,
// and only then, so a program that makes no window (the dedicated server) never loads SDL.
// Without the module, or with FL_USER32_HEADLESS set in the environment, windows stay records
// without a screen. The backend reports what happens on screen through FUser32Sink; user32
// turns that into the window messages and the state its calls answer.
//
// A backed window's HWND is its SDL window id, which is how RPGPU finds the window it draws
// into (SDL_GetWindowFromID).

#pragma once

#include <cstdint>

// Events from the backend, in the order they happened. Coordinates are in the desktop's
// (window positions, the cursor) or in the window's client area (mouse messages).
struct FUser32Sink
{
    // A message for a window, queued (Post) or handed to its window procedure at once (Send),
    // as Windows does with the messages it sends from inside PeekMessage.
    void (*Post)(std::uint32_t Window, unsigned Message, std::uintptr_t WParam,
                 std::intptr_t LParam);
    void (*Send)(std::uint32_t Window, unsigned Message, std::uintptr_t WParam,
                 std::intptr_t LParam);
    // A key went down or up: its virtual key, whether it is an extended key, and whether it is
    // a repeat. user32 keeps the key state and makes WM_(SYS)KEYDOWN / WM_(SYS)KEYUP.
    void (*Key)(std::uint32_t Window, unsigned VirtualKey, bool bExtended, bool bDown,
                bool bRepeat);
    // Typed text: one UTF-32 character, which user32 posts as WM_CHAR.
    void (*Char)(std::uint32_t Window, std::uint32_t Character);
    // The mouse: its position in the window's client area and on the desktop, and, for
    // DirectInput's system mouse, a relative motion or a button (DIMOFS_ offset and data).
    void (*MouseMove)(std::uint32_t Window, int ClientX, int ClientY, int ScreenX, int ScreenY);
    void (*MouseRaw)(std::uint32_t Offset, std::uint32_t Data);
    // A window was moved or resized by the user or the system (client size).
    void (*Bounds)(std::uint32_t Window, int X, int Y, int Width, int Height);
    // The application's windows got or lost the focus.
    void (*Activate)(std::uint32_t Window, bool bActive);
};

struct FUser32Backend
{
    // kUser32BackendVersion: user32 refuses a backend of another version.
    int Version;
    // A top-level window: its title (UTF-8), position and client size, whether it has a frame
    // (a caption) and whether it starts shown. Its id, never 0, or false.
    bool (*CreateWindow)(const char* Title, int X, int Y, int Width, int Height, bool bFramed,
                         bool bVisible, std::uint32_t* OutId);
    void (*DestroyWindow)(std::uint32_t Window);
    // Position (desktop) and client size.
    void (*SetBounds)(std::uint32_t Window, int X, int Y, int Width, int Height);
    // The frame, and whether the window covers the display as its fullscreen window.
    void (*SetFrame)(std::uint32_t Window, bool bFramed, bool bFullscreen);
    // SW_ commands.
    void (*Show)(std::uint32_t Window, int Command);
    void (*SetTitle)(std::uint32_t Window, const char* Title);
    // Takes the screen's pending events, through Sink.
    void (*Pump)(const FUser32Sink* Sink);
    // Waits up to Milliseconds for an event.
    void (*Wait)(int Milliseconds);
    // The cursor: shown or hidden, moved (client coordinates of Window), and kept inside the
    // window's client area (bClip) or free.
    void (*ShowCursor)(bool bShow);
    void (*WarpMouse)(std::uint32_t Window, int X, int Y);
    void (*ClipCursor)(std::uint32_t Window, bool bClip);
    // The display's modes: Index -1 is the desktop's, then the fullscreen modes from the
    // largest. False past the last.
    bool (*DisplayMode)(int Index, int* Width, int* Height, int* Bits, int* RefreshHz);
};

inline constexpr int kUser32BackendVersion = 1;

// The backend module's one export.
extern "C" typedef const FUser32Backend* (*FUser32BackendEntry)();
#define FL_USER32_BACKEND_ENTRY "FlUser32Backend"
