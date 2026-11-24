// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): checks of the client's stand-ins on Linux and macOS (src/include/posix): user32
// (src/PosixUser32: classes, windows, the message queue, the keyboard tables), the DirectInput
// mouse, and the kernel32, CRT and COM pieces the client added. Not part of retail; built only
// by the non-Windows build, with ctest.

#include <windows.h>

#include <ctfutb.h>
#include <dinput.h>
#include <imm.h>
#include <process.h>
#include <wrl/client.h>

#if FL_USER32_SCREEN_EXPECTED
#include "user32_screen_probe.h"
#endif

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <string>
#include <thread>
#include <vector>

namespace
{
int GFailures = 0;

void Check(bool bOk, const char* What)
{
    std::printf("%s: %s\n", bOk ? "ok  " : "FAIL", What);
    if (!bOk)
        ++GFailures;
}

// What the test window procedure saw.
struct FSeen
{
    std::vector<UINT> Messages;
    LPARAM CreateParam = 0;
    WPARAM LastWParam = 0;
};
FSeen GSeen;

LRESULT CALLBACK TestProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    GSeen.Messages.push_back(Message);
    switch (Message)
    {
    case WM_CREATE:
        GSeen.CreateParam =
            reinterpret_cast<LPARAM>(reinterpret_cast<CREATESTRUCTA*>(LParam)->lpCreateParams);
        return 0;
    case WM_USER + 1:
        GSeen.LastWParam = WParam;
        return static_cast<LRESULT>(WParam * 2);
    default:
        return DefWindowProcA(Window, Message, WParam, LParam);
    }
}

// A subclass: SubclassBase installs SubclassProc in WM_NCCREATE; SubclassProc puts the base
// back in WM_DESTROY.
std::vector<UINT> GSubclassSaw;
LRESULT CALLBACK SubclassBase(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam);
LRESULT CALLBACK SubclassProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    GSubclassSaw.push_back(Message);
    if (Message == WM_DESTROY)
        SetWindowLongPtrA(Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassBase));
    return DefWindowProcA(Window, Message, WParam, LParam);
}
LRESULT CALLBACK SubclassBase(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    if (Message == WM_NCCREATE)
        SetWindowLongPtrA(Window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&SubclassProc));
    return DefWindowProcA(Window, Message, WParam, LParam);
}

bool Saw(UINT Message)
{
    for (UINT Seen : GSeen.Messages)
        if (Seen == Message)
            return true;
    return false;
}

void TestWindows()
{
    WNDCLASSA Class = {};
    Class.lpfnWndProc = &TestProc;
    Class.lpszClassName = "TestClass";
    Check(RegisterClassA(&Class) != 0, "RegisterClassA registers a class");
    Check(RegisterClassA(&Class) == 0, "RegisterClassA refuses the same name twice");
    WNDCLASSA Subclass = {};
    Subclass.lpfnWndProc = &SubclassBase;
    Subclass.lpszClassName = "SubclassClass";
    RegisterClassA(&Subclass);
    Check(CreateWindowExA(0, "NoSuchClass", "", 0, 0, 0, 10, 10, nullptr, nullptr, nullptr,
                          nullptr) == nullptr,
          "CreateWindowExA fails for an unknown class");

    int Param = 0;
    HWND const Window = CreateWindowExA(0, "testclass", "Title", WS_POPUP, 10, 20, 640, 480,
                                        nullptr, nullptr, nullptr, &Param);
    Check(Window != nullptr && IsWindow(Window), "CreateWindowExA makes a window (class name "
                                                 "without regard to case)");
    Check(Saw(WM_NCCREATE) && Saw(WM_CREATE) &&
              GSeen.CreateParam == reinterpret_cast<LPARAM>(&Param),
          "the window procedure gets WM_NCCREATE and WM_CREATE with the create parameter");

    RECT Rect = {};
    Check(GetWindowRect(Window, &Rect) && Rect.left == 10 && Rect.top == 20 && Rect.right == 650 &&
              Rect.bottom == 500,
          "GetWindowRect is the rectangle the window was made with");
    Check(GetClientRect(Window, &Rect) && Rect.left == 0 && Rect.right == 640 && Rect.bottom == 480,
          "GetClientRect is the whole window (no frame)");
    SetWindowPos(Window, nullptr, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER);
    Check(GetWindowRect(Window, &Rect) && Rect.left == 10 && Rect.right == 810 &&
              Rect.bottom == 620,
          "SetWindowPos with SWP_NOMOVE resizes in place");
    Check(static_cast<DWORD>(GetWindowLongA(Window, GWL_STYLE)) == WS_POPUP,
          "GetWindowLongA(GWL_STYLE) is the window's style");
    SetWindowLongA(Window, GWL_STYLE, static_cast<LONG>(WS_POPUP | WS_VISIBLE));
    Check(IsWindowVisible(Window), "SetWindowLongA(GWL_STYLE) changes the style");
    Check(ShowWindow(Window, SW_HIDE) == TRUE && !IsWindowVisible(Window),
          "ShowWindow answers the previous visibility");

    Check(SendMessageA(Window, WM_USER + 1, 21, 0) == 42 && GSeen.LastWParam == 21,
          "SendMessageA calls the window procedure and returns its result");

    // The queue.
    MSG Message = {};
    Check(!PeekMessageA(&Message, nullptr, 0, 0, PM_REMOVE), "the queue starts empty");
    PostMessageA(Window, WM_USER + 1, 7, 0);
    PostMessageA(Window, WM_USER + 2, 8, 0);
    Check(PeekMessageA(&Message, nullptr, WM_USER + 2, WM_USER + 2, PM_NOREMOVE) &&
              Message.message == WM_USER + 2,
          "PeekMessageA finds a message by its range");
    Check(PeekMessageA(&Message, Window, 0, 0, PM_REMOVE) && Message.message == WM_USER + 1 &&
              Message.hwnd == Window,
          "PeekMessageA takes the oldest message first");
    Check(DispatchMessageA(&Message) == 14 && GSeen.LastWParam == 7,
          "DispatchMessageA hands the message to its window procedure");
    // WM_USER + 2 is still queued, but this thread has looked at it: it is not new input.
    Check(MsgWaitForMultipleObjects(0, nullptr, FALSE, 0, QS_ALLINPUT) == WAIT_TIMEOUT,
          "MsgWaitForMultipleObjects does not wake for a message already looked at");
    PostMessageA(Window, WM_USER + 3, 9, 0);
    Check(MsgWaitForMultipleObjects(0, nullptr, FALSE, 0, QS_ALLINPUT) == WAIT_OBJECT_0,
          "MsgWaitForMultipleObjects wakes for new input");
    Check(MsgWaitForMultipleObjects(0, nullptr, FALSE, 0, QS_KEY) == WAIT_TIMEOUT,
          "a posted message is not keyboard input (the wake mask)");
    while (PeekMessageA(&Message, nullptr, 0, 0, PM_REMOVE))
    {
    }
    Check(MsgWaitForMultipleObjects(0, nullptr, FALSE, 20, QS_ALLINPUT) == WAIT_TIMEOUT,
          "MsgWaitForMultipleObjects times out on an empty queue");
    PostQuitMessage(3);
    // WM_QUIT passes a message range; checked without blocking first.
    bool const bQuitQueued =
        PeekMessageA(&Message, nullptr, WM_KEYFIRST, WM_KEYLAST, PM_NOREMOVE) &&
        Message.message == WM_QUIT;
    Check(bQuitQueued, "WM_QUIT passes a message range filter");
    Check(bQuitQueued && !GetMessageA(&Message, nullptr, WM_KEYFIRST, WM_KEYLAST) &&
              Message.message == WM_QUIT && Message.wParam == 3,
          "GetMessageA returns FALSE for WM_QUIT, with the exit code");
    Check(GetMessageA(nullptr, nullptr, 0, 0) == -1, "GetMessageA refuses a null message");

    // Each thread has its own queue: another thread's PostQuitMessage stays on its queue, and
    // a window's posted messages go to the thread that made it.
    std::thread Other(
        [Window]()
        {
            PostQuitMessage(5);
            PostMessageA(Window, WM_USER + 4, 0, 0);
            MSG Theirs = {};
            Check(PeekMessageA(&Theirs, nullptr, 0, 0, PM_REMOVE) && Theirs.message == WM_QUIT &&
                      Theirs.wParam == 5,
                  "PostQuitMessage goes to the posting thread's queue");
        });
    Other.join();
    // A thread that ended takes its queue with it: a new thread (which may get the same
    // std::thread::id) starts empty.
    std::thread([]() { PostQuitMessage(7); }).join();
    std::thread(
        []()
        {
            MSG Theirs = {};
            Check(!PeekMessageA(&Theirs, nullptr, 0, 0, PM_REMOVE),
                  "a new thread does not get an ended thread's messages");
        })
        .join();
    Check(PeekMessageA(&Message, nullptr, 0, 0, PM_REMOVE) && Message.message == WM_USER + 4 &&
              !PeekMessageA(&Message, nullptr, 0, 0, PM_NOREMOVE),
          "a window's message from another thread reaches the window's thread, without its quit");

    // A procedure installed during WM_NCCREATE gets WM_CREATE; one restored during WM_DESTROY
    // gets no WM_NCDESTROY.
    GSubclassSaw.clear();
    HWND const Subclassed = CreateWindowExA(0, "SubclassClass", "", 0, 0, 0, 10, 10, nullptr,
                                            nullptr, nullptr, nullptr);
    Check(Subclassed != nullptr && GSubclassSaw.size() == 1 && GSubclassSaw[0] == WM_CREATE,
          "a procedure installed in WM_NCCREATE receives WM_CREATE");
    GSubclassSaw.clear();
    DestroyWindow(Subclassed);
    Check(GSubclassSaw.size() == 1 && GSubclassSaw[0] == WM_DESTROY,
          "a procedure restored in WM_DESTROY receives no WM_NCDESTROY");

    // Focus.
    Check(SetFocus(Window) == nullptr && GetFocus() == Window && Saw(WM_SETFOCUS),
          "SetFocus sends WM_SETFOCUS and GetFocus answers the window");

    // WM_CLOSE through DefWindowProcA destroys the window.
    GSeen.Messages.clear();
    SendMessageA(Window, WM_CLOSE, 0, 0);
    Check(Saw(WM_DESTROY) && Saw(WM_NCDESTROY) && !IsWindow(Window) && GetFocus() == nullptr,
          "DefWindowProcA's WM_CLOSE destroys the window");
    Check(!DestroyWindow(Window), "DestroyWindow fails for a window that is gone");

    // Atoms, a disabled start, children.
    WNDCLASSA Second = Class;
    Second.lpszClassName = "SecondClass";
    ATOM const SecondAtom = RegisterClassA(&Second);
    Check(SecondAtom != 0, "a second class is registered");
    HWND const Parent = CreateWindowExA(0, MAKEINTRESOURCEA(SecondAtom), "", WS_POPUP, 100, 100,
                                        300, 200, nullptr, nullptr, nullptr, nullptr);
    Check(Parent != nullptr, "CreateWindowExA takes a class atom");
    HWND const Child = CreateWindowExA(0, "SecondClass", "", WS_CHILD | WS_VISIBLE | WS_DISABLED,
                                       10, 20, 50, 40, Parent, nullptr, nullptr, nullptr);
    GSeen.Messages.clear();
    Check(Child != nullptr && EnableWindow(Child, TRUE) == TRUE,
          "a window made WS_DISABLED reports it was disabled");
    Check((static_cast<DWORD>(GetWindowLongA(Child, GWL_STYLE)) & WS_DISABLED) == 0 &&
              Saw(WM_ENABLE),
          "EnableWindow clears WS_DISABLED and sends WM_ENABLE");
    GSeen.Messages.clear();
    Check(EnableWindow(Child, TRUE) == FALSE && !Saw(WM_ENABLE),
          "EnableWindow sends nothing when the state does not change");
    // A message for the child passes a filter naming its parent.
    PostMessageA(Child, WM_USER + 5, 0, 0);
    MSG ChildMessage = {};
    Check(PeekMessageA(&ChildMessage, Parent, 0, 0, PM_REMOVE) && ChildMessage.hwnd == Child,
          "PeekMessageA with a window takes its child windows' messages too");
    // At any depth: a message for a child 70 levels down passes the top window's filter.
    {
        HWND Deepest = Parent;
        for (int Level = 0; Level < 70 && Deepest != nullptr; ++Level)
            Deepest = CreateWindowExA(0, "SecondClass", "", WS_CHILD, 0, 0, 1, 1, Deepest, nullptr,
                                      nullptr, nullptr);
        PostMessageA(Deepest, WM_USER + 6, 0, 0);
        MSG Deep = {};
        Check(Deepest != nullptr && PeekMessageA(&Deep, Parent, 0, 0, PM_REMOVE) &&
                  Deep.hwnd == Deepest,
              "PeekMessageA with a window takes a message of a child 70 levels down");
    }
    // An owned popup's position is the desktop's, not relative to its owner.
    HWND const Popup = CreateWindowExA(0, "SecondClass", "", WS_POPUP, 10, 20, 30, 30, Parent,
                                       nullptr, nullptr, nullptr);
    POINT PopupOrigin = {0, 0};
    Check(Popup != nullptr && ClientToScreen(Popup, &PopupOrigin) && PopupOrigin.x == 10 &&
              PopupOrigin.y == 20,
          "an owned popup is placed on the desktop");
    POINT Origin = {0, 0};
    Check(ClientToScreen(Child, &Origin) && Origin.x == 110 && Origin.y == 120,
          "a child's position is relative to its parent");
    Check(!IsWindowVisible(Child), "a visible child of a hidden parent is not visible");
    ShowWindow(Parent, SW_SHOW);
    Check(IsWindowVisible(Child), "and is once the parent is shown");
    Check(DestroyWindow(Parent) && !IsWindow(Child), "destroying a parent destroys its children");
    Check(UnregisterClassA(MAKEINTRESOURCEA(SecondAtom), nullptr) &&
              CreateWindowExA(0, "SecondClass", "", 0, 0, 0, 1, 1, nullptr, nullptr, nullptr,
                              nullptr) == nullptr,
          "UnregisterClassA takes an atom");
    Check(DialogBoxParamA(nullptr, MAKEINTRESOURCEA(0x82), nullptr, nullptr, 0) == -1,
          "DialogBoxParamA fails without dialog templates");
}

void TestKeyboardAndText()
{
    Check(MapVirtualKeyA('A', MAPVK_VK_TO_VSC) == 0x1E, "MapVirtualKeyA('A') is scan code 0x1E");
    Check(MapVirtualKeyA(0x1E, MAPVK_VSC_TO_VK) == 'A', "MapVirtualKeyA(0x1E) is 'A'");
    Check(MapVirtualKeyA(0xE01D, MAPVK_VSC_TO_VK_EX) == VK_RCONTROL &&
              MapVirtualKeyA(0xE01D, MAPVK_VSC_TO_VK) == VK_CONTROL &&
              MapVirtualKeyA(0x2A, MAPVK_VSC_TO_VK_EX) == VK_LSHIFT,
          "MapVirtualKeyA decodes extended scan codes and names the modifiers' sides");
    wchar_t Name[32];
    Check(GetKeyNameTextW(0x1E << 16, Name, 32) == 1 && std::wcscmp(Name, L"A") == 0,
          "GetKeyNameTextW names scan code 0x1E 'A'");
    Check(GetKeyNameTextW((0x48 << 16) | 0x01000000, Name, 32) == 2 &&
              std::wcscmp(Name, L"Up") == 0,
          "GetKeyNameTextW names extended scan code 0x48 'Up'");
    Check(GetKeyNameTextW(0x48 << 16, Name, 4) == 3 && std::wcscmp(Name, L"Num") == 0,
          "GetKeyNameTextW cuts the name to the buffer");

    wchar_t Text[] = L"freelancer";
    Check(std::wcscmp(CharUpperW(Text), L"FREELANCER") == 0, "CharUpperW upper-cases a string");
    Check(reinterpret_cast<std::uintptr_t>(CharUpperW(reinterpret_cast<LPWSTR>(L'q'))) == L'Q',
          "CharUpperW upper-cases one character in the pointer");

    wchar_t Out[64];
    Check(wsprintfW(Out, L"%s=%d %S", L"wide", 5, "narrow") == 13 &&
              std::wcscmp(Out, L"wide=5 narrow") == 0,
          "wsprintfW takes %s as wide and %S as narrow");
    Check(_snwprintf(Out, 4, L"%s", L"abcdef") == -1 && std::wmemcmp(Out, L"abcd", 4) == 0,
          "_snwprintf cuts without a terminator and returns -1");
    Check(_snwprintf(nullptr, 0, L"%s", L"abc") == 3 && _snwprintf(nullptr, 0, L"") == 0,
          "_snwprintf with a null buffer and a zero count returns the length needed");
    Check(_snwprintf(Out, 0, L"%s", L"abc") == -1,
          "_snwprintf with a buffer and a zero count writes nothing and returns -1");
    Check(_snwprintf(Out, 8, L"%s", L"abc") == 3 && std::wcscmp(Out, L"abc") == 0,
          "_snwprintf writes a short text with its terminator");
    Check(std::wcscmp(_itow(-42, Out, 10), L"-42") == 0 &&
              std::wcscmp(_ultow(255, Out, 16), L"ff") == 0,
          "_itow / _ultow write the digits");
    Check(_wcsicoll(L"Alpha", L"ALPHA") == 0 && _wcsicoll(L"a", L"B") < 0,
          "_wcsicoll compares without regard to case");

    // "é€" in UTF-8, then one invalid byte (Latin-1).
    Check(MultiByteToWideChar(CP_ACP, 0, "\xC3\xA9\xE2\x82\xAC\xFF", 6, Out, 64) == 3 &&
              Out[0] == 0xE9 && Out[1] == 0x20AC && Out[2] == 0xFF,
          "MultiByteToWideChar decodes UTF-8, an invalid byte as Latin-1");
    Check(MultiByteToWideChar(CP_ACP, 0, "\xC0\x80", 2, Out, 64) == 2 && Out[0] == 0xC0 &&
              Out[1] == 0x80,
          "MultiByteToWideChar refuses an overlong sequence (Latin-1 instead)");
    Check(MultiByteToWideChar(CP_ACP, 0, "abc", -1, nullptr, 0) == 4,
          "MultiByteToWideChar asks for the size with the terminator");
    WORD Types[3] = {};
    Check(GetStringTypeExW(LOCALE_USER_DEFAULT, CT_CTYPE1, L"\x07 a", 3, Types) &&
              Types[0] == C1_CNTRL && Types[1] == C1_SPACE && Types[2] == 0,
          "GetStringTypeExW classes control characters and spaces");
    Check(CompareStringA(0x409, NORM_IGNORECASE, "ImeKr.ime", -1, "IMEKR.IME", -1) == CSTR_EQUAL,
          "CompareStringA with NORM_IGNORECASE ignores case");
    wchar_t Copy[4];
    Check(std::wcscmp(lstrcpynW(Copy, L"abcdef", 4), L"abc") == 0,
          "lstrcpynW copies Size - 1 characters and the terminator");
}

void TestDirectInput()
{
    IDirectInput8A* Input = nullptr;
    Check(DirectInput8Create(nullptr, DIRECTINPUT_VERSION, IID_IDirectInput8A,
                             reinterpret_cast<void**>(&Input), nullptr) == DI_OK &&
              Input != nullptr,
          "DirectInput8Create succeeds");
    if (Input == nullptr)
        return;
    IDirectInputDevice8A* Mouse = nullptr;
    Check(Input->CreateDevice(GUID_SysKeyboard, &Mouse, nullptr) != DI_OK && Mouse == nullptr,
          "only the system mouse is a device");
    Check(Input->CreateDevice(GUID_SysMouse, &Mouse, nullptr) == DI_OK && Mouse != nullptr,
          "CreateDevice makes the system mouse");
    if (Mouse != nullptr)
    {
        Check(Mouse->SetDataFormat(&c_dfDIMouse2) == DI_OK, "the mouse takes c_dfDIMouse2");
        DIPROPDWORD Property = {};
        Property.diph.dwSize = sizeof(DIPROPDWORD);
        Property.diph.dwHeaderSize = sizeof(DIPROPHEADER);
        Property.diph.dwHow = DIPH_DEVICE;
        Property.dwData = 2;
        Check(Mouse->SetProperty(DIPROP_BUFFERSIZE, &Property.diph) == DI_OK,
              "the mouse takes a buffer size");
        DIMOUSESTATE2 State = {};
        Check(Mouse->GetDeviceState(sizeof(State), &State) == DIERR_NOTACQUIRED,
              "an unacquired mouse has no state");
        Check(Mouse->Acquire() == DI_OK, "the mouse is acquired");
        PosixDirectInput::MouseEvent(DIMOFS_X, 5);
        PosixDirectInput::MouseEvent(DIMOFS_BUTTON0, 0x80);
        DIDEVICEOBJECTDATA Records[4] = {};
        DWORD Count = 4;
        Check(Mouse->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), Records, &Count, 0) == DI_OK &&
                  Count == 2 && Records[0].dwOfs == DIMOFS_X && Records[0].dwData == 5 &&
                  Records[1].dwOfs == DIMOFS_BUTTON0 && Records[1].dwData == 0x80,
              "GetDeviceData hands out the queued records in order");
        Check(Mouse->GetDeviceState(sizeof(State), &State) == DI_OK && State.lX == 5 &&
                  State.rgbButtons[0] == 0x80,
              "GetDeviceState has the motion and the button");
        Check(Mouse->GetDeviceState(sizeof(State), &State) == DI_OK && State.lX == 0 &&
                  State.rgbButtons[0] == 0x80,
              "the axes read back as zero, the button stays down");
        for (DWORD Index = 0; Index < 3; ++Index)
            PosixDirectInput::MouseEvent(DIMOFS_Y, Index + 1);
        Count = 4;
        Check(Mouse->GetDeviceData(sizeof(DIDEVICEOBJECTDATA), Records, &Count, 0) ==
                      DI_BUFFEROVERFLOW &&
                  Count == 2 && Records[0].dwOfs == DIMOFS_Y && Records[0].dwData == 2 &&
                  Records[1].dwOfs == DIMOFS_Y && Records[1].dwData == 3,
              "an overflow drops the oldest record and reports DI_BUFFEROVERFLOW");
        Mouse->Unacquire();
        Mouse->Release();
    }
    Input->Release();
}

void TestComAndMisc()
{
    const IID& LangBar = __uuidof(ITfLangBarMgr);
    Check(LangBar.Data1 == 0x87955690 && LangBar.Data4[7] == 0xB5,
          "__uuidof(ITfLangBarMgr) is the SDK's IID");
    ITfLangBarMgr* Manager = nullptr;
    Check(FAILED(CoCreateInstance(GUID{}, nullptr, CLSCTX_INPROC_SERVER, __uuidof(ITfLangBarMgr),
                                  reinterpret_cast<void**>(&Manager))) &&
              Manager == nullptr,
          "CoCreateInstance finds no language bar");
    Check(!ImmIsIME(GetKeyboardLayout(0)) && ImmGetContext(nullptr) == nullptr, "there is no IME");
    Check(_spawnl(_P_WAIT, "Flasher.exe", "Flasher.exe", nullptr) == -1,
          "_spawnl fails for a Windows program");
    Microsoft::WRL::ComPtr<IUnknown> Empty;
    Check(Empty == nullptr && Empty.GetAddressOf() != nullptr, "ComPtr starts empty");
    bool bRan = false;
    __try
    {
        bRan = true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        bRan = false;
    }
    Check(bRan, "a __try body runs and its __except block does not");
}
// The screen (src/PosixUser32/user32_sdl, SDL3): run by ctest with SDL's dummy video driver,
// which makes windows without a display. Where user32_sdl is built (FL_USER32_SCREEN_EXPECTED),
// a window without a screen fails; otherwise the checks are skipped (passing).
#if FL_USER32_SCREEN_EXPECTED
struct FScreenMessage
{
    UINT Message;
    WPARAM WParam;
    LPARAM LParam;
};
std::vector<FScreenMessage> GScreenSeen;
bool GNestOnClose = false;
bool GNestOnActivate = false;
HWND GDestroyOnClose = nullptr; // destroyed by another thread from the close handler
bool GDestroyedOnClose = false;
int GNestedKeys = 0;

std::uint32_t IdOf(HWND Window)
{
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(Window));
}

// Takes the screen's events into the queue without taking a message (a filter no message
// matches).
void PumpOnly()
{
    MSG Message;
    PeekMessageA(&Message, nullptr, WM_USER + 0x7000, WM_USER + 0x7000, PM_NOREMOVE);
}

// Takes what the screen has, dispatching it.
void DrainScreen()
{
    MSG Message;
    for (int Pass = 0; Pass < 10; ++Pass)
        while (PeekMessageA(&Message, nullptr, 0, 0, PM_REMOVE))
            DispatchMessageA(&Message);
}

LRESULT CALLBACK ScreenProc(HWND Window, UINT Message, WPARAM WParam, LPARAM LParam)
{
    GScreenSeen.push_back({Message, WParam, LParam});
    if (Message == WM_ACTIVATEAPP && WParam != FALSE && GNestOnActivate)
    {
        // The handler takes the screen's events itself, and the focus goes again.
        GNestOnActivate = false;
        ScreenProbe::PushFocus(IdOf(Window), false);
        MSG Nested;
        for (int Pass = 0; Pass < 5; ++Pass)
            while (PeekMessageA(&Nested, nullptr, 0, 0, PM_REMOVE))
                DispatchMessageA(&Nested);
    }
    if (Message == WM_SYSCOMMAND && (WParam & 0xFFF0) == SC_CLOSE && GDestroyOnClose != nullptr)
    {
        // Another window destroyed by a worker while the screen's events are being delivered.
        HWND const Doomed = GDestroyOnClose;
        GDestroyOnClose = nullptr;
        std::thread([Doomed] { GDestroyedOnClose = DestroyWindow(Doomed) != FALSE; }).join();
        return 0;
    }
    if (Message == WM_SYSCOMMAND && (WParam & 0xFFF0) == SC_CLOSE)
    {
        // A close handler with a message loop of its own (a confirmation): it takes input
        // that comes after the close request.
        if (GNestOnClose)
        {
            GNestOnClose = false;
            ScreenProbe::PushKeyA(IdOf(Window), true);
            MSG Nested;
            for (int Pass = 0; Pass < 20 && GNestedKeys == 0; ++Pass)
                while (PeekMessageA(&Nested, nullptr, 0, 0, PM_REMOVE))
                    if (Nested.message == WM_KEYDOWN && Nested.wParam == 'A')
                        ++GNestedKeys;
        }
        return 0; // not closed: the checks go on with the window
    }
    return DefWindowProcA(Window, Message, WParam, LParam);
}

const FScreenMessage* FindScreen(UINT Message, std::size_t From = 0)
{
    for (std::size_t Index = From; Index < GScreenSeen.size(); ++Index)
        if (GScreenSeen[Index].Message == Message)
            return &GScreenSeen[Index];
    return nullptr;
}

int CountScreen(UINT Message)
{
    int Count = 0;
    for (const FScreenMessage& Seen : GScreenSeen)
        Count += Seen.Message == Message ? 1 : 0;
    return Count;
}
#endif

int TestScreen()
{
#if !FL_USER32_SCREEN_EXPECTED
    std::printf("skip: no screen backend (user32_sdl) in this build\n");
    return 0;
#else
    // Before the first window: desktop queries answer without loading SDL.
    DEVMODEA Mode = {};
    Mode.dmSize = sizeof(Mode);
    Check(GetSystemMetrics(SM_CXSCREEN) == 1024 &&
              EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &Mode) &&
              !ScreenProbe::VideoInitialized(),
          "desktop queries before the first window do not start SDL");
    Check(!EnumDisplaySettingsA(nullptr, 0x80000000u, &Mode),
          "EnumDisplaySettingsA refuses a mode index past int's range");
    Check(ShowCursor(FALSE) == -1, "ShowCursor counts before there is a screen");

    WNDCLASSA Class = {};
    Class.lpfnWndProc = &ScreenProc;
    Class.lpszClassName = "ScreenClass";
    RegisterClassA(&Class);
    // WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN, as the client makes its window;
    // "Caf\xE9": the title's bytes are characters (Latin-1), given to SDL in UTF-8.
    HWND const Window = CreateWindowExA(0, "ScreenClass", "Caf\xE9", 0x2CA0000, 10, 10, 640, 480,
                                        nullptr, nullptr, nullptr, nullptr);
    Check(Window != nullptr && reinterpret_cast<std::uintptr_t>(Window) < 0x40000000,
          "a window with a screen is numbered by the backend (its SDL window id)");
    if (Window == nullptr || reinterpret_cast<std::uintptr_t>(Window) >= 0x40000000)
        return 1;
    std::uint32_t const Id = IdOf(Window);
    Check(ScreenProbe::VideoInitialized(), "the first top-level window starts SDL");
    Check(!ScreenProbe::CursorVisible(), "the first screen takes ShowCursor's earlier count");
    Check(ShowCursor(TRUE) == 0 && ScreenProbe::CursorVisible(),
          "ShowCursor crossing 0 shows the screen's cursor");
    Check(ScreenProbe::WindowTitle(Id) == "Caf\xC3\xA9",
          "the screen's title is the bytes in UTF-8");
    SetWindowTextA(Window, "Caf\xE9 2");
    Check(ScreenProbe::WindowTitle(Id) == "Caf\xC3\xA9 2", "SetWindowTextA encodes as creation");
    Check(!ScreenProbe::IsResizable(Id), "a window without a sizing border cannot be resized");

    Check(GetSystemMetrics(SM_CXSCREEN) > 0 && GetSystemMetrics(SM_CYSCREEN) > 0,
          "the desktop has the display's size");
    Check(EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &Mode) && Mode.dmPelsWidth > 0,
          "EnumDisplaySettingsA describes the display's mode");
    ShowWindow(Window, SW_SHOWNORMAL);
    SetWindowPos(Window, nullptr, 0, 0, 800, 600, SWP_NOMOVE | SWP_NOZORDER);
    DrainScreen();
    RECT Rect = {};
    int Width = 0;
    int Height = 0;
    Check(GetClientRect(Window, &Rect) && Rect.right == 800 && Rect.bottom == 600 &&
              ScreenProbe::WindowSize(Id, &Width, &Height) && Width == 800 && Height == 600,
          "SetWindowPos resizes the screen's window, and its events keep the size");
    ShowWindow(Window, SW_MAXIMIZE);
    Check((GetWindowLongA(Window, GWL_STYLE) & WS_MAXIMIZE) == 0,
          "a maximize the screen refuses leaves the window unmaximized");

    // Moves as each event had them, in order.
    GScreenSeen.clear();
    ScreenProbe::PushMoved(Id, 100, 50);
    ScreenProbe::PushMoved(Id, 200, 50);
    DrainScreen();
    const FScreenMessage* const FirstMove = FindScreen(WM_MOVE);
    const FScreenMessage* const SecondMove =
        FirstMove != nullptr ? FindScreen(WM_MOVE, (FirstMove - GScreenSeen.data()) + 1) : nullptr;
    Check(FirstMove != nullptr && LOWORD(FirstMove->LParam) == 100 && SecondMove != nullptr &&
              LOWORD(SecondMove->LParam) == 200,
          "each move is reported with its own position");
    ScreenProbe::PushResized(Id, 640, 400);
    DrainScreen();
    Check(GetWindowRect(Window, &Rect) && Rect.left == 200 && Rect.right - Rect.left == 640 &&
              Rect.bottom - Rect.top == 400,
          "a resize keeps the position the moves before it gave");

    // Mouse buttons with the state as of each event.
    GScreenSeen.clear();
    ScreenProbe::PushButton(Id, 1, true, 5.f, 6.f);
    ScreenProbe::PushButton(Id, 1, false, 5.f, 6.f);
    DrainScreen();
    const FScreenMessage* const Down = FindScreen(WM_LBUTTONDOWN);
    const FScreenMessage* const Up = FindScreen(WM_LBUTTONUP);
    Check(Down != nullptr && (Down->WParam & MK_LBUTTON) != 0 && Up != nullptr &&
              (Up->WParam & MK_LBUTTON) == 0 && LOWORD(Down->LParam) == 5,
          "a press and release taken together keep MK_LBUTTON as each happened");

    // The wheel: whole notches, at the event's position on the desktop.
    GScreenSeen.clear();
    ScreenProbe::PushWheel(Id, 0.5f, 10.f, 20.f);
    ScreenProbe::PushWheel(Id, 0.5f, 10.f, 20.f);
    DrainScreen();
    const FScreenMessage* const Wheel = FindScreen(WM_MOUSEWHEEL);
    Check(CountScreen(WM_MOUSEWHEEL) == 1 && Wheel != nullptr &&
              static_cast<short>(HIWORD(Wheel->WParam)) == 120 && LOWORD(Wheel->LParam) == 210 &&
              HIWORD(Wheel->LParam) == 70,
          "two half notches make one WM_MOUSEWHEEL at the event's desktop position");

    // DirectInput's relative motion keeps the fractions.
    IDirectInput8A* Input = nullptr;
    IDirectInputDevice8A* Mouse = nullptr;
    Check(DirectInput8Create(nullptr, DIRECTINPUT_VERSION, IID_IDirectInput8A,
                             reinterpret_cast<void**>(&Input), nullptr) == DI_OK &&
              Input != nullptr,
          "DirectInput8Create succeeds with a screen");
    if (Input != nullptr)
        Check(Input->CreateDevice(GUID_SysMouse, &Mouse, nullptr) == DI_OK && Mouse != nullptr,
              "the system mouse is made with a screen");
    if (Mouse != nullptr)
    {
        Mouse->SetDataFormat(&c_dfDIMouse2);
        Check(Mouse->Acquire() == DI_OK, "the mouse is acquired with a screen");
        DIMOUSESTATE2 MouseState = {};
        Mouse->GetDeviceState(sizeof(MouseState), &MouseState);
        ScreenProbe::PushMotion(Id, 1.f, 1.f, 0.5f, 0.f);
        ScreenProbe::PushMotion(Id, 1.f, 1.f, 0.5f, 0.f);
        DrainScreen();
        Check(Mouse->GetDeviceState(sizeof(MouseState), &MouseState) == DI_OK && MouseState.lX == 1,
              "two half-unit motions reach DirectInput as one");
        // Input queued for a window that another thread destroys before it is taken is dropped
        // (the SDL window goes later, on the main thread); the same window's input arrives while
        // it lives.
        HWND const Doomed = CreateWindowExA(0, "ScreenClass", "Doomed", 0x2CA0000, 0, 0, 320, 240,
                                            nullptr, nullptr, nullptr, nullptr);
        bool const bDoomedOnScreen =
            Doomed != nullptr && reinterpret_cast<std::uintptr_t>(Doomed) < 0x40000000;
        Check(bDoomedOnScreen, "a second window gets a screen");
        if (bDoomedOnScreen)
        {
            ScreenProbe::PushMotion(IdOf(Doomed), 1.f, 1.f, 2.f, 0.f);
            DrainScreen();
            Check(Mouse->GetDeviceState(sizeof(MouseState), &MouseState) == DI_OK &&
                      MouseState.lX == 2,
                  "a live window's motion reaches DirectInput");
            // The close request's handler has a worker destroy Doomed; Doomed's motion is taken
            // from SDL in the same pump, before the main thread removes its SDL window.
            GDestroyOnClose = Doomed;
            ScreenProbe::PushCloseRequest(Id);
            ScreenProbe::PushMotion(IdOf(Doomed), 1.f, 1.f, 3.f, 0.f);
            DrainScreen();
            Check(GDestroyedOnClose, "another thread destroys the window");
            Check(Mouse->GetDeviceState(sizeof(MouseState), &MouseState) == DI_OK &&
                      MouseState.lX == 0,
                  "a destroyed window's queued motion does not reach DirectInput");
        }
        Mouse->Release();
    }
    if (Input != nullptr)
        Input->Release();

    // Left and right Shift each keep their state.
    ScreenProbe::PushModifier(Id, ScreenProbe::EModifier::LeftShift, true);
    ScreenProbe::PushModifier(Id, ScreenProbe::EModifier::RightShift, true);
    ScreenProbe::PushModifier(Id, ScreenProbe::EModifier::LeftShift, false);
    DrainScreen();
    Check((GetKeyState(VK_SHIFT) & 0x8000) != 0 && (GetKeyState(VK_RSHIFT) & 0x8000) != 0 &&
              (GetKeyState(VK_LSHIFT) & 0x8000) == 0,
          "Shift stays down while the other Shift is held");
    ScreenProbe::PushModifier(Id, ScreenProbe::EModifier::RightShift, false);
    DrainScreen();
    Check((GetKeyState(VK_SHIFT) & 0x8000) == 0, "Shift is up when both are");
    // GetKeyState follows the messages taken; GetAsyncKeyState the screen's state now.
    ScreenProbe::PushKeyA(Id, true);
    ScreenProbe::PushKeyA(Id, false);
    MSG KeyMessage = {};
    bool bKeyDownSeen = false;
    for (int Pass = 0; Pass < 10 && !bKeyDownSeen; ++Pass)
        while (!bKeyDownSeen && PeekMessageA(&KeyMessage, nullptr, 0, 0, PM_REMOVE))
            bKeyDownSeen = KeyMessage.message == WM_KEYDOWN && KeyMessage.wParam == 'A';
    Check(bKeyDownSeen && (GetKeyState('A') & 0x8000) != 0 && (GetAsyncKeyState('A') & 0x8000) == 0,
          "while A's key-down is handled, GetKeyState has A down though the key is up again");
    DrainScreen();
    Check((GetKeyState('A') & 0x8000) == 0, "A's key-up message brings A up");
    // Each key message brings the whole keyboard: Shift's key-up is dropped with its window,
    // and A's next key-down still brings Shift up.
    HWND const Other = CreateWindowExA(0, "ScreenClass", "Other", 0x2CA0000, 0, 0, 320, 240,
                                       nullptr, nullptr, nullptr, nullptr);
    if (Other != nullptr)
    {
        ScreenProbe::PushModifier(IdOf(Other), ScreenProbe::EModifier::LeftShift, true);
        DrainScreen();
        Check((GetKeyState(VK_SHIFT) & 0x8000) != 0, "Shift's key-down is taken");
        ScreenProbe::PushModifier(IdOf(Other), ScreenProbe::EModifier::LeftShift, false);
        // The key-up is queued for Other, which is then destroyed with it.
        PumpOnly();
        DestroyWindow(Other);
        ScreenProbe::PushKeyA(Id, true);
        ScreenProbe::PushKeyA(Id, false);
        DrainScreen();
        Check((GetKeyState(VK_SHIFT) & 0x8000) == 0,
              "a later key message brings Shift up though Shift's key-up was dropped");
    }

    // MSG.pt is the position of the event the message came from.
    ScreenProbe::PushWheel(Id, 1.f, 30.f, 40.f);
    MSG WheelMessage = {};
    bool bWheelSeen = false;
    for (int Pass = 0; Pass < 10 && !bWheelSeen; ++Pass)
        while (!bWheelSeen && PeekMessageA(&WheelMessage, nullptr, 0, 0, PM_REMOVE))
            bWheelSeen = WheelMessage.message == WM_MOUSEWHEEL;
    Check(bWheelSeen && WheelMessage.pt.x == static_cast<LONG>(LOWORD(WheelMessage.lParam)) &&
              WheelMessage.pt.y == static_cast<LONG>(HIWORD(WheelMessage.lParam)),
          "a wheel message's MSG.pt is the wheel event's position");
    DrainScreen();
    // An event queued before a move is placed where the window was when it happened.
    RECT Before = {};
    GetWindowRect(Window, &Before);
    GScreenSeen.clear();
    ScreenProbe::PushWheel(Id, 1.f, 10.f, 20.f);
    SetWindowPos(Window, nullptr, Before.left + 100, Before.top, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    DrainScreen();
    const FScreenMessage* const EarlierWheel = FindScreen(WM_MOUSEWHEEL);
    Check(EarlierWheel != nullptr &&
              static_cast<LONG>(LOWORD(EarlierWheel->LParam)) == Before.left + 10,
          "a wheel event queued before SetWindowPos keeps the window's position then");

    // The window's own maximize button and a restore.
    GScreenSeen.clear();
    ScreenProbe::PushShowState(Id, 2);
    DrainScreen();
    const FScreenMessage* const Maximized = FindScreen(WM_SIZE);
    Check((GetWindowLongA(Window, GWL_STYLE) & WS_MAXIMIZE) != 0 && Maximized != nullptr &&
              Maximized->WParam == SIZE_MAXIMIZED,
          "a maximize on screen sets WS_MAXIMIZE and sends WM_SIZE SIZE_MAXIMIZED");
    GScreenSeen.clear();
    ScreenProbe::PushShowState(Id, 0);
    DrainScreen();
    const FScreenMessage* const Restored = FindScreen(WM_SIZE);
    Check((GetWindowLongA(Window, GWL_STYLE) & WS_MAXIMIZE) == 0 && Restored != nullptr &&
              Restored->WParam == SIZE_RESTORED,
          "a restore on screen clears it and sends WM_SIZE SIZE_RESTORED");
    // SW_SHOWNA leaves SDL's activation hint as it found it, set or not.
    bool bHintBefore = false;
    std::string const HintBefore = ScreenProbe::ActivateHint(&bHintBefore);
    ShowWindow(Window, SW_SHOWNA);
    bool bHintAfter = false;
    std::string const HintAfter = ScreenProbe::ActivateHint(&bHintAfter);
    Check(bHintBefore == bHintAfter && HintBefore == HintAfter,
          "SW_SHOWNA leaves SDL's activation hint as it was");
    ScreenProbe::SetActivateHint("1");
    ShowWindow(Window, SW_SHOWNA);
    Check(ScreenProbe::ActivateHint(&bHintAfter) == "1" && bHintAfter,
          "SW_SHOWNA leaves a hint set to 1 at 1");
    ScreenProbe::ResetActivateHint();

    // A focus change taken by a nested loop inside WM_ACTIVATEAPP wins over the outer one.
    GNestOnActivate = true;
    ScreenProbe::PushFocus(Id, true);
    DrainScreen();
    Check(GetFocus() == nullptr, "an activation superseded inside its handler leaves the focus");

    // The application's Quit, and a close handler's nested message loop.
    GScreenSeen.clear();
    ScreenProbe::PushQuit();
    DrainScreen();
    const FScreenMessage* const Quit = FindScreen(WM_SYSCOMMAND);
    Check(Quit != nullptr && (Quit->WParam & 0xFFF0) == SC_CLOSE && CountScreen(WM_SYSCOMMAND) == 1,
          "the application's Quit is the close button's WM_SYSCOMMAND SC_CLOSE");
    GScreenSeen.clear();
    GNestOnClose = true;
    ScreenProbe::PushCloseRequest(Id);
    DrainScreen();
    Check(GNestedKeys == 1 && CountScreen(WM_SYSCOMMAND) == 1,
          "a nested message loop takes the screen's input, without the event it is handling");

    // ClipCursor lets go of the window it clipped to after the focus went.
    ScreenProbe::PushFocus(Id, true);
    DrainScreen();
    RECT Clip = {0, 0, 100, 100};
    ClipCursor(&Clip);
    Check(ScreenProbe::IsMouseGrabbed(Id), "ClipCursor keeps the cursor in the active window");
    ScreenProbe::PushFocus(Id, false);
    DrainScreen();
    ClipCursor(nullptr);
    Check(!ScreenProbe::IsMouseGrabbed(Id), "ClipCursor(nullptr) frees it after the focus went");

    // A popup over the whole desktop is fullscreen from creation; one beside it is not.
    int const DesktopWidth = GetSystemMetrics(SM_CXSCREEN);
    int const DesktopHeight = GetSystemMetrics(SM_CYSCREEN);
    HWND const Beside =
        CreateWindowExA(0, "ScreenClass", "Beside", WS_POPUP | WS_VISIBLE, -100, 0, DesktopWidth,
                        DesktopHeight, nullptr, nullptr, nullptr, nullptr);
    Check(Beside != nullptr && !ScreenProbe::IsFullscreen(IdOf(Beside)),
          "a popup that leaves part of the desktop uncovered is not fullscreen");
    SetWindowPos(Beside, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    Check(Beside != nullptr && ScreenProbe::IsFullscreen(IdOf(Beside)),
          "moving it over the desktop without resizing makes it fullscreen");
    DestroyWindow(Beside);
    HWND const Owned = CreateWindowExA(0, "ScreenClass", "Owned", WS_POPUP, 10, 10, 200, 100,
                                       Window, nullptr, nullptr, nullptr);
    Check(Owned != nullptr && reinterpret_cast<std::uintptr_t>(Owned) < 0x40000000,
          "an owned popup is a top-level window with a screen");
    DestroyWindow(Owned);

    // Off the main thread: no screen for a new window; a title is forwarded to the main thread.
    HWND Worker = nullptr;
    std::thread(
        [&Worker]
        {
            Worker = CreateWindowExA(0, "ScreenClass", "Worker", 0x2CA0000, 0, 0, 320, 240, nullptr,
                                     nullptr, nullptr, nullptr);
            SetWindowTextA(Worker, "Worker");
        })
        .join();
    Check(Worker != nullptr && reinterpret_cast<std::uintptr_t>(Worker) >= 0x40000000,
          "a window made off the main thread has no screen");
    std::thread([Window] { SetWindowTextA(Window, "Forwarded"); }).join();
    DrainScreen();
    Check(ScreenProbe::WindowTitle(Id) == "Forwarded",
          "a title set off the main thread reaches the screen through the main thread");
    // A title forwarded earlier does not overwrite one the main thread sets after it.
    std::thread([Window] { SetWindowTextA(Window, "Older"); }).join();
    SetWindowTextA(Window, "Newer");
    DrainScreen();
    Check(ScreenProbe::WindowTitle(Id) == "Newer",
          "a forwarded title is applied before the main thread's later one");
    // A maximize forwarded from another thread that SDL refuses is undone in user32 too.
    std::thread([Window] { ShowWindow(Window, SW_MAXIMIZE); }).join();
    DrainScreen();
    Check((GetWindowLongA(Window, GWL_STYLE) & WS_MAXIMIZE) == 0,
          "a forwarded maximize the screen refuses leaves the window unmaximized");

    // A GetMessageA waiting for a window's messages returns -1 when another thread destroys it.
    // The worker says it is about to wait; the main thread then checks that it is still waiting
    // a while later (no message can end the wait: none is posted to the window) before it
    // destroys the window. Without DestroyWindow's wake-up the waiter never returns, and the
    // test process ends with the failure rather than leave the waiter with this frame's state.
    std::atomic<HWND> Waited = nullptr;
    std::atomic<bool> bWaiting = false;
    std::atomic<int> WaitResult = 0;
    std::thread Waiter(
        [&]
        {
            Waited = CreateWindowExA(0, "ScreenClass", "Waited", 0x2CA0000, 0, 0, 320, 240, nullptr,
                                     nullptr, nullptr, nullptr);
            MSG Taken;
            bWaiting = true;
            WaitResult = GetMessageA(&Taken, Waited, 0, 0) == -1 ? 1 : 2;
        });
    for (int Spin = 0; Spin < 400 && !bWaiting.load(); ++Spin)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    Check(bWaiting.load() && WaitResult.load() == 0, "the waiter is blocked in GetMessageA");
    DestroyWindow(Waited.load());
    for (int Spin = 0; Spin < 400 && WaitResult.load() == 0; ++Spin)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    Check(WaitResult.load() == 1, "GetMessageA for a window destroyed meanwhile returns -1");
    if (WaitResult.load() == 0)
    {
        std::printf("%d failure(s)\n", GFailures);
        std::fflush(stdout);
        std::_Exit(1);
    }
    Waiter.join();
    HWND const Full =
        CreateWindowExA(0, "ScreenClass", "Full", WS_POPUP | WS_VISIBLE, 0, 0, DesktopWidth,
                        DesktopHeight, nullptr, nullptr, nullptr, nullptr);
    Check(Full != nullptr && ScreenProbe::IsFullscreen(IdOf(Full)),
          "a popup made over the desktop is fullscreen from creation");
    // Back to a window, at the bounds asked for with the frame.
    SetWindowLongA(Full, GWL_STYLE, static_cast<LONG>(0x2CA0000 | WS_VISIBLE));
    SetWindowPos(Full, nullptr, 100, 100, 800, 600, SWP_NOZORDER | SWP_FRAMECHANGED);
    DrainScreen();
    int X = 0;
    int Y = 0;
    Check(!ScreenProbe::IsFullscreen(IdOf(Full)) &&
              ScreenProbe::WindowSize(IdOf(Full), &Width, &Height) && Width == 800 &&
              Height == 600 && ScreenProbe::WindowPosition(IdOf(Full), &X, &Y) && X == 100 &&
              Y == 100,
          "leaving fullscreen takes the windowed bounds asked for");
    DestroyWindow(Full);
    HWND const Sizable = CreateWindowExA(0, "ScreenClass", "Sizable", WS_OVERLAPPEDWINDOW, 0, 0,
                                         320, 240, nullptr, nullptr, nullptr, nullptr);
    Check(Sizable != nullptr && ScreenProbe::IsResizable(IdOf(Sizable)),
          "a window with a sizing border can be resized and maximized");
    DestroyWindow(Sizable);

    GSeen.Messages.clear();
    GScreenSeen.clear();
    Check(DestroyWindow(Window) && FindScreen(WM_DESTROY) != nullptr && !IsWindow(Window),
          "DestroyWindow closes the screen's window");
    return 0;
#endif
}
} // namespace

int main(int Argc, char** Argv)
{
    if (Argc > 1 && std::strcmp(Argv[1], "--screen") == 0)
    {
        TestScreen();
        std::printf("%d failure(s)\n", GFailures);
        return GFailures == 0 ? 0 : 1;
    }
    TestWindows();
    TestKeyboardAndText();
    TestDirectInput();
    TestComAndMisc();
    std::printf("%d failure(s)\n", GFailures);
    return GFailures == 0 ? 0 : 1;
}
