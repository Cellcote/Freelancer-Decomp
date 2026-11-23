// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the DirectX 8 SDK's <dinput.h> on macOS and Linux
// (src/include/posix, non-Windows only). The client reads the mouse through DirectInput 8
// (input_manager.cpp) and, as retail, does not survive a failed DirectInput8Create: so here
// DirectInput8Create succeeds and hands out a system mouse with the SDK's interfaces, vtable
// order, values and record layouts. The screen's mouse events feed it (src/PosixUser32, SDL):
// PosixDirectInput::MouseEvent queues a record for GetDeviceData and updates the state
// GetDeviceState reads. The queue is the user32 module's, one per process, so the events reach
// the device whichever module made it.

#pragma once

#if defined(_WIN32)
#error "posix/dinput.h is the stand-in for macOS and Linux; Windows uses the DirectX 8 SDK"
#endif

#include <windows.h>

#include <cstring>
#include <deque>

#ifndef DIRECTINPUT_VERSION
#define DIRECTINPUT_VERSION 0x0800
#endif

// ---------------------------------------------------------------------------------------------
// Values and records (the SDK's)
// ---------------------------------------------------------------------------------------------

#define DI_OK S_OK
#define DI_BUFFEROVERFLOW S_FALSE
#define DIERR_NOTACQUIRED ((HRESULT)0x8007000C)
#define DIERR_INPUTLOST ((HRESULT)0x8007001E)
#define DIERR_INVALIDPARAM E_INVALIDARG
#define DIERR_UNSUPPORTED E_NOTIMPL
#define DIERR_DEVICENOTREG REGDB_E_CLASSNOTREG

#define DISCL_EXCLUSIVE 0x00000001
#define DISCL_NONEXCLUSIVE 0x00000002
#define DISCL_FOREGROUND 0x00000004
#define DISCL_BACKGROUND 0x00000008
#define DISCL_NOWINKEY 0x00000010

#define DIPH_DEVICE 0
#define DIPH_BYOFFSET 1
#define DIPH_BYID 2

#define DIGDD_PEEK 0x00000001

// The properties. The SDK's are numbers in a GUID reference (MAKEDIPROP(1): a reference to
// address 1), which is undefined behaviour in C++ and which UBSan stops on; the only reader here
// is the mouse below, so each property is an object of its own, told apart by its address.
namespace PosixDirectInput
{
inline constexpr GUID kPropBufferSize = {1, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
inline constexpr GUID kPropAxisMode = {2, 0, 0, {0, 0, 0, 0, 0, 0, 0, 0}};
} // namespace PosixDirectInput
#define DIPROP_BUFFERSIZE (PosixDirectInput::kPropBufferSize)
#define DIPROP_AXISMODE (PosixDirectInput::kPropAxisMode)
#define DIPROPAXISMODE_ABS 0
#define DIPROPAXISMODE_REL 1

// DIMOUSESTATE2's fields, the offsets GetDeviceData's records name.
#define DIMOFS_X 0
#define DIMOFS_Y 4
#define DIMOFS_Z 8
#define DIMOFS_BUTTON0 12
#define DIMOFS_BUTTON1 13
#define DIMOFS_BUTTON2 14
#define DIMOFS_BUTTON3 15
#define DIMOFS_BUTTON4 16
#define DIMOFS_BUTTON5 17
#define DIMOFS_BUTTON6 18
#define DIMOFS_BUTTON7 19

typedef struct DIPROPHEADER
{
    DWORD dwSize;
    DWORD dwHeaderSize;
    DWORD dwObj;
    DWORD dwHow;
} DIPROPHEADER, *LPDIPROPHEADER;
typedef const DIPROPHEADER* LPCDIPROPHEADER;

typedef struct DIPROPDWORD
{
    DIPROPHEADER diph;
    DWORD dwData;
} DIPROPDWORD, *LPDIPROPDWORD;

typedef struct _DIMOUSESTATE2
{
    LONG lX;
    LONG lY;
    LONG lZ;
    BYTE rgbButtons[8];
} DIMOUSESTATE2, *LPDIMOUSESTATE2;

// DirectInput 8's record: uAppData is pointer-sized (0x14 bytes on Win32, 0x18 here).
typedef struct DIDEVICEOBJECTDATA
{
    DWORD dwOfs;
    DWORD dwData;
    DWORD dwTimeStamp;
    DWORD dwSequence;
    UINT_PTR uAppData;
} DIDEVICEOBJECTDATA, *LPDIDEVICEOBJECTDATA;

typedef struct _DIOBJECTDATAFORMAT
{
    const GUID* pguid;
    DWORD dwOfs;
    DWORD dwType;
    DWORD dwFlags;
} DIOBJECTDATAFORMAT;

typedef struct _DIDATAFORMAT
{
    DWORD dwSize;
    DWORD dwObjSize;
    DWORD dwFlags;
    DWORD dwDataSize;
    DWORD dwNumObjs;
    DIOBJECTDATAFORMAT* rgodf;
} DIDATAFORMAT, *LPDIDATAFORMAT;
typedef const DIDATAFORMAT* LPCDIDATAFORMAT;

// dxguid's and dinput8's statics. c_dfDIMouse2 is only compared by address here: the mouse
// takes no other format.
inline constexpr GUID GUID_SysMouse = {
    0x6F1D2B60, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr GUID GUID_SysKeyboard = {
    0x6F1D2B61, 0xD5A0, 0x11CF, {0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00}};
inline constexpr IID IID_IDirectInput8A = {
    0xBF798030, 0x483A, 0x4DA2, {0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00}};
inline const DIDATAFORMAT c_dfDIMouse2 = {sizeof(DIDATAFORMAT),
                                          sizeof(DIOBJECTDATAFORMAT),
                                          2 /* DIDF_RELAXIS */,
                                          sizeof(DIMOUSESTATE2),
                                          0,
                                          nullptr};

// ---------------------------------------------------------------------------------------------
// Interfaces (the SDK's vtable order; the methods the client never calls take void*)
// ---------------------------------------------------------------------------------------------

struct IDirectInputDevice8A : public IUnknown
{
    STDMETHOD(GetCapabilities)(void*) PURE;
    STDMETHOD(EnumObjects)(void*, void*, DWORD) PURE;
    STDMETHOD(GetProperty)(REFGUID, LPDIPROPHEADER) PURE;
    STDMETHOD(SetProperty)(REFGUID, LPCDIPROPHEADER) PURE;
    STDMETHOD(Acquire)() PURE;
    STDMETHOD(Unacquire)() PURE;
    STDMETHOD(GetDeviceState)(DWORD, LPVOID) PURE;
    STDMETHOD(GetDeviceData)(DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD) PURE;
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT) PURE;
    STDMETHOD(SetEventNotification)(HANDLE) PURE;
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) PURE;
    STDMETHOD(GetObjectInfo)(void*, DWORD, DWORD) PURE;
    STDMETHOD(GetDeviceInfo)(void*) PURE;
    STDMETHOD(RunControlPanel)(HWND, DWORD) PURE;
    STDMETHOD(Initialize)(HINSTANCE, DWORD, REFGUID) PURE;
    STDMETHOD(CreateEffect)(REFGUID, void const*, void**, LPUNKNOWN) PURE;
    STDMETHOD(EnumEffects)(void*, LPVOID, DWORD) PURE;
    STDMETHOD(GetEffectInfo)(void*, REFGUID) PURE;
    STDMETHOD(GetForceFeedbackState)(LPDWORD) PURE;
    STDMETHOD(SendForceFeedbackCommand)(DWORD) PURE;
    STDMETHOD(EnumCreatedEffectObjects)(void*, LPVOID, DWORD) PURE;
    STDMETHOD(Escape)(void*) PURE;
    STDMETHOD(Poll)() PURE;
    STDMETHOD(SendDeviceData)(DWORD, void const*, LPDWORD, DWORD) PURE;
    STDMETHOD(EnumEffectsInFile)(LPCSTR, void*, LPVOID, DWORD) PURE;
    STDMETHOD(WriteEffectToFile)(LPCSTR, DWORD, void*, DWORD) PURE;
    STDMETHOD(BuildActionMap)(void*, LPCSTR, DWORD) PURE;
    STDMETHOD(SetActionMap)(void*, LPCSTR, DWORD) PURE;
    STDMETHOD(GetImageInfo)(void*) PURE;
};
typedef IDirectInputDevice8A* LPDIRECTINPUTDEVICE8A;

struct IDirectInput8A : public IUnknown
{
    STDMETHOD(CreateDevice)(REFGUID, IDirectInputDevice8A**, LPUNKNOWN) PURE;
    STDMETHOD(EnumDevices)(DWORD, void*, LPVOID, DWORD) PURE;
    STDMETHOD(GetDeviceStatus)(REFGUID) PURE;
    STDMETHOD(RunControlPanel)(HWND, DWORD) PURE;
    STDMETHOD(Initialize)(HINSTANCE, DWORD) PURE;
    STDMETHOD(FindDevice)(REFGUID, LPCSTR, LPGUID) PURE;
    STDMETHOD(EnumDevicesBySemantics)(LPCSTR, void*, void*, LPVOID, DWORD) PURE;
    STDMETHOD(ConfigureDevices)(void*, void*, DWORD, LPVOID) PURE;
};
typedef IDirectInput8A* LPDIRECTINPUT8A;

// ---------------------------------------------------------------------------------------------
// The system mouse
// ---------------------------------------------------------------------------------------------

namespace PosixDirectInput
{
// The mouse's state and its buffered records (DIPROP_BUFFERSIZE entries at most; an overflow
// drops the oldest and makes the next GetDeviceData answer DI_BUFFEROVERFLOW, as DirectInput).
struct FMouseQueue
{
    DIMOUSESTATE2 State = {};
    std::deque<DIDEVICEOBJECTDATA> Records;
    DWORD BufferSize = 0;
    DWORD Sequence = 0;
    bool bOverflow = false;
    bool bAcquired = false;
};
} // namespace PosixDirectInput

// The process's mouse queue (src/PosixUser32).
FL_USER32_API PosixDirectInput::FMouseQueue& PosixDirectInputMouse();

namespace PosixDirectInput
{
inline FMouseQueue& Mouse()
{
    return PosixDirectInputMouse();
}

// One mouse event: Offset is a DIMOFS_ value; Data is the relative motion for an axis, or 0x80
// / 0 for a button down / up. The window layer calls it for its mouse events.
inline void MouseEvent(DWORD Offset, DWORD Data)
{
    FMouseQueue& Queue = Mouse();
    if (!Queue.bAcquired)
        return;
    if (Offset <= DIMOFS_Z)
        (&Queue.State.lX)[Offset / 4] += static_cast<LONG>(Data);
    else if (Offset <= DIMOFS_BUTTON7)
        Queue.State.rgbButtons[Offset - DIMOFS_BUTTON0] = static_cast<BYTE>(Data);
    else
        return;
    if (Queue.BufferSize == 0)
        return;
    if (Queue.Records.size() >= Queue.BufferSize)
    {
        Queue.Records.pop_front();
        Queue.bOverflow = true;
    }
    DIDEVICEOBJECTDATA Record = {};
    Record.dwOfs = Offset;
    Record.dwData = Data;
    Record.dwTimeStamp = GetTickCount();
    Record.dwSequence = ++Queue.Sequence;
    Queue.Records.push_back(Record);
}

class FMouseDevice final : public IDirectInputDevice8A
{
public:
    STDMETHOD(QueryInterface)(REFIID, void** Out) override
    {
        if (Out != nullptr)
            *Out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++References; }
    STDMETHOD_(ULONG, Release)() override
    {
        ULONG const Count = --References;
        if (Count == 0)
            delete this;
        return Count;
    }
    STDMETHOD(GetCapabilities)(void*) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(EnumObjects)(void*, void*, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetProperty)(REFGUID Property, LPDIPROPHEADER Header) override
    {
        if (&Property == &DIPROP_BUFFERSIZE && Header != nullptr &&
            Header->dwSize >= sizeof(DIPROPDWORD))
        {
            reinterpret_cast<DIPROPDWORD*>(Header)->dwData = Mouse().BufferSize;
            return DI_OK;
        }
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(SetProperty)(REFGUID Property, LPCDIPROPHEADER Header) override
    {
        if (&Property == &DIPROP_BUFFERSIZE && Header != nullptr &&
            Header->dwSize >= sizeof(DIPROPDWORD))
        {
            FMouseQueue& Queue = Mouse();
            Queue.BufferSize = reinterpret_cast<const DIPROPDWORD*>(Header)->dwData;
            while (Queue.Records.size() > Queue.BufferSize)
                Queue.Records.pop_front();
            return DI_OK;
        }
        return DI_OK; // the axis mode and the rest: accepted, as a relative mouse
    }
    STDMETHOD(Acquire)() override
    {
        FMouseQueue& Queue = Mouse();
        if (Queue.bAcquired)
            return S_FALSE;
        Queue.bAcquired = true;
        return DI_OK;
    }
    STDMETHOD(Unacquire)() override
    {
        FMouseQueue& Queue = Mouse();
        if (!Queue.bAcquired)
            return S_FALSE;
        Queue.bAcquired = false;
        Queue.Records.clear();
        Queue.State = {};
        return DI_OK;
    }
    // The state since the last call: the axes are relative, so they read back as zero next time.
    STDMETHOD(GetDeviceState)(DWORD Size, LPVOID Data) override
    {
        FMouseQueue& Queue = Mouse();
        if (!Queue.bAcquired)
            return DIERR_NOTACQUIRED;
        if (Data == nullptr || Size != sizeof(DIMOUSESTATE2))
            return DIERR_INVALIDPARAM;
        std::memcpy(Data, &Queue.State, sizeof(DIMOUSESTATE2));
        Queue.State.lX = Queue.State.lY = Queue.State.lZ = 0;
        return DI_OK;
    }
    STDMETHOD(GetDeviceData)(DWORD RecordSize, LPDIDEVICEOBJECTDATA Records, LPDWORD InOut,
                             DWORD Flags) override
    {
        FMouseQueue& Queue = Mouse();
        if (!Queue.bAcquired)
            return DIERR_NOTACQUIRED;
        if (InOut == nullptr || RecordSize != sizeof(DIDEVICEOBJECTDATA))
            return DIERR_INVALIDPARAM;
        DWORD Count = 0;
        while (Count < *InOut && Count < Queue.Records.size())
        {
            if (Records != nullptr)
                Records[Count] = Queue.Records[Count];
            ++Count;
        }
        if ((Flags & DIGDD_PEEK) == 0)
            Queue.Records.erase(Queue.Records.begin(), Queue.Records.begin() + Count);
        *InOut = Count;
        HRESULT const Result = Queue.bOverflow ? DI_BUFFEROVERFLOW : DI_OK;
        if ((Flags & DIGDD_PEEK) == 0)
            Queue.bOverflow = false;
        return Result;
    }
    STDMETHOD(SetDataFormat)(LPCDIDATAFORMAT Format) override
    {
        return Format == &c_dfDIMouse2 ? DI_OK : DIERR_INVALIDPARAM;
    }
    STDMETHOD(SetEventNotification)(HANDLE) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(SetCooperativeLevel)(HWND, DWORD) override { return DI_OK; }
    STDMETHOD(GetObjectInfo)(void*, DWORD, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetDeviceInfo)(void*) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(RunControlPanel)(HWND, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(Initialize)(HINSTANCE, DWORD, REFGUID) override { return DI_OK; }
    STDMETHOD(CreateEffect)(REFGUID, void const*, void**, LPUNKNOWN) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(EnumEffects)(void*, LPVOID, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetEffectInfo)(void*, REFGUID) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetForceFeedbackState)(LPDWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(SendForceFeedbackCommand)(DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(EnumCreatedEffectObjects)(void*, LPVOID, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(Escape)(void*) override { return DIERR_UNSUPPORTED; }
    // A mouse needs no polling: DI_NOEFFECT (S_FALSE).
    STDMETHOD(Poll)() override { return Mouse().bAcquired ? S_FALSE : DIERR_NOTACQUIRED; }
    STDMETHOD(SendDeviceData)(DWORD, void const*, LPDWORD, DWORD) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(EnumEffectsInFile)(LPCSTR, void*, LPVOID, DWORD) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(WriteEffectToFile)(LPCSTR, DWORD, void*, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(BuildActionMap)(void*, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(SetActionMap)(void*, LPCSTR, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetImageInfo)(void*) override { return DIERR_UNSUPPORTED; }

private:
    ULONG References = 1;
};

class FDirectInput final : public IDirectInput8A
{
public:
    STDMETHOD(QueryInterface)(REFIID, void** Out) override
    {
        if (Out != nullptr)
            *Out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return ++References; }
    STDMETHOD_(ULONG, Release)() override
    {
        ULONG const Count = --References;
        if (Count == 0)
            delete this;
        return Count;
    }
    // The system mouse only (the client asks for nothing else).
    STDMETHOD(CreateDevice)(REFGUID Guid, IDirectInputDevice8A** Out, LPUNKNOWN) override
    {
        if (Out == nullptr)
            return DIERR_INVALIDPARAM;
        *Out = nullptr;
        if (Guid != GUID_SysMouse)
            return DIERR_DEVICENOTREG;
        *Out = new FMouseDevice();
        return DI_OK;
    }
    STDMETHOD(EnumDevices)(DWORD, void*, LPVOID, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(GetDeviceStatus)(REFGUID Guid) override
    {
        return Guid == GUID_SysMouse ? DI_OK : DIERR_DEVICENOTREG;
    }
    STDMETHOD(RunControlPanel)(HWND, DWORD) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(Initialize)(HINSTANCE, DWORD) override { return DI_OK; }
    STDMETHOD(FindDevice)(REFGUID, LPCSTR, LPGUID) override { return DIERR_UNSUPPORTED; }
    STDMETHOD(EnumDevicesBySemantics)(LPCSTR, void*, void*, LPVOID, DWORD) override
    {
        return DIERR_UNSUPPORTED;
    }
    STDMETHOD(ConfigureDevices)(void*, void*, DWORD, LPVOID) override { return DIERR_UNSUPPORTED; }

private:
    ULONG References = 1;
};
} // namespace PosixDirectInput

inline HRESULT DirectInput8Create(HINSTANCE, DWORD Version, REFIID Iid, LPVOID* Out, LPUNKNOWN)
{
    if (Out == nullptr)
        return DIERR_INVALIDPARAM;
    *Out = nullptr;
    if (Version != 0x0800 || Iid != IID_IDirectInput8A)
        return E_NOINTERFACE;
    *Out = static_cast<IDirectInput8A*>(new PosixDirectInput::FDirectInput());
    return DI_OK;
}
