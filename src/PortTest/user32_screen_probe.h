// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): user32_screen_tests' view of SDL (user32_screen_probe.cpp), kept out of
// user32_tests.cpp, which is built against the stand-in <windows.h>. It reads the SDL windows
// behind user32's screen windows and queues SDL events as the screen would have them. Not part
// of retail.

#pragma once

#include <cstdint>
#include <string>

namespace ScreenProbe
{
bool VideoInitialized();
bool WindowSize(std::uint32_t Id, int* Width, int* Height);
bool WindowPosition(std::uint32_t Id, int* X, int* Y);
std::string WindowTitle(std::uint32_t Id);
bool IsFullscreen(std::uint32_t Id);
bool IsResizable(std::uint32_t Id);
bool IsMouseGrabbed(std::uint32_t Id);
bool CursorVisible();
// Whether SDL_HINT_WINDOW_ACTIVATE_WHEN_SHOWN is set at all.
bool ActivateHintSet();

enum class EModifier
{
    LeftShift,
    RightShift,
};
void PushModifier(std::uint32_t Id, EModifier Key, bool bDown);
void PushKeyA(std::uint32_t Id, bool bDown);
// Button 1 is the left button.
void PushButton(std::uint32_t Id, int Button, bool bDown, float X, float Y);
void PushMotion(std::uint32_t Id, float X, float Y, float DeltaX, float DeltaY);
void PushWheel(std::uint32_t Id, float Amount, float X, float Y);
void PushMoved(std::uint32_t Id, int X, int Y);
void PushResized(std::uint32_t Id, int Width, int Height);
void PushFocus(std::uint32_t Id, bool bGained);
// The window's own minimize (1) / maximize (2) buttons, or a restore (0).
void PushShowState(std::uint32_t Id, int State);
void PushCloseRequest(std::uint32_t Id);
void PushQuit();
} // namespace ScreenProbe
