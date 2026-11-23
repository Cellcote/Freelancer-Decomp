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

#include <cstdio>
#include <cstring>
#include <cwchar>
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
} // namespace

int main()
{
    TestWindows();
    TestKeyboardAndText();
    TestDirectInput();
    TestComAndMisc();
    std::printf("%d failure(s)\n", GFailures);
    return GFailures == 0 ? 0 : 1;
}
