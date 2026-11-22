// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the DirectX 8 SDK's <dpaddr.h> on macOS and Linux
// (src/include/posix, non-Windows only): IDirectPlay8Address with the SDK's vtable order, its
// class and interface ids, and the DPNA_ keys and data types, with the SDK's values. There is no
// dpnet.dll here: the address objects are DPNet's (src/DALib/DPNetAdapter.cpp), compiled from
// this header like their callers, so the layouts only have to agree within the process.

#pragma once

#if defined(_WIN32)
#error "posix/dpaddr.h is the stand-in for macOS and Linux; Windows uses the DirectX 8 SDK"
#endif

#include <windows.h>

// ---------------------------------------------------------------------------------------------
// Ids
// ---------------------------------------------------------------------------------------------

// {934A9523-A3CA-4bc5-ADA0-D6D95D979421}
inline constexpr CLSID CLSID_DirectPlay8Address = {
    0x934a9523, 0xa3ca, 0x4bc5, {0xad, 0xa0, 0xd6, 0xd9, 0x5d, 0x97, 0x94, 0x21}};
// {83783300-4063-4c8a-9DB3-82830A7FEB31}
inline constexpr IID IID_IDirectPlay8Address = {
    0x83783300, 0x4063, 0x4c8a, {0x9d, 0xb3, 0x82, 0x83, 0x0a, 0x7f, 0xeb, 0x31}};
// {E5A0E990-2BAD-430b-87DA-A142CF75DE58}
inline constexpr IID IID_IDirectPlay8AddressIP = {
    0xe5a0e990, 0x2bad, 0x430b, {0x87, 0xda, 0xa1, 0x42, 0xcf, 0x75, 0xde, 0x58}};

// ---------------------------------------------------------------------------------------------
// Address components
// ---------------------------------------------------------------------------------------------

#define DPNA_DATATYPE_STRING 0x00000001
#define DPNA_DATATYPE_DWORD 0x00000002
#define DPNA_DATATYPE_GUID 0x00000003
#define DPNA_DATATYPE_BINARY 0x00000004
#define DPNA_DATATYPE_STRING_ANSI 0x00000005

#define DPNA_DPNSVR_PORT 6073
#define DPNA_INDEX_INVALID 0xFFFFFFFF

#define DPNA_SEPARATOR_KEYVALUE L'='
#define DPNA_SEPARATOR_USERDATA L'#'
#define DPNA_SEPARATOR_COMPONENT L';'
#define DPNA_ESCAPECHAR L'%'

#define DPNA_HEADER L"x-directplay:/"

#define DPNA_KEY_APPLICATION_INSTANCE L"applicationinstance"
#define DPNA_KEY_BAUD L"baud"
#define DPNA_KEY_DEVICE L"device"
#define DPNA_KEY_FLOWCONTROL L"flowcontrol"
#define DPNA_KEY_HOSTNAME L"hostname"
#define DPNA_KEY_PARITY L"parity"
#define DPNA_KEY_PHONENUMBER L"phonenumber"
#define DPNA_KEY_PORT L"port"
#define DPNA_KEY_PROGRAM L"program"
#define DPNA_KEY_PROVIDER L"provider"
#define DPNA_KEY_STOPBITS L"stopbits"

// ---------------------------------------------------------------------------------------------
// IDirectPlay8Address
// ---------------------------------------------------------------------------------------------

struct IDirectPlay8Address;
typedef IDirectPlay8Address* LPDIRECTPLAY8ADDRESS;
typedef IDirectPlay8Address* PDIRECTPLAY8ADDRESS;

struct IDirectPlay8Address : public IUnknown
{
    STDMETHOD(BuildFromURLW)(WCHAR* Url) PURE;
    STDMETHOD(BuildFromURLA)(CHAR* Url) PURE;
    STDMETHOD(Duplicate)(PDIRECTPLAY8ADDRESS* Out) PURE;
    STDMETHOD(SetEqual)(PDIRECTPLAY8ADDRESS Address) PURE;
    STDMETHOD(IsEqual)(PDIRECTPLAY8ADDRESS Address) PURE;
    STDMETHOD(Clear)() PURE;
    STDMETHOD(GetURLW)(WCHAR* Url, PDWORD NumChars) PURE;
    STDMETHOD(GetURLA)(CHAR* Url, PDWORD NumChars) PURE;
    STDMETHOD(GetSP)(GUID* Sp) PURE;
    STDMETHOD(GetUserData)(LPVOID Buffer, PDWORD Size) PURE;
    STDMETHOD(SetSP)(const GUID* Sp) PURE;
    STDMETHOD(SetUserData)(const void* Data, DWORD Size) PURE;
    STDMETHOD(GetNumComponents)(PDWORD Count) PURE;
    STDMETHOD(GetComponentByName)(const WCHAR* Name, LPVOID Buffer, PDWORD Size, PDWORD Type) PURE;
    STDMETHOD(GetComponentByIndex)(DWORD Index, WCHAR* Name, PDWORD NameLength, void* Buffer,
                                   PDWORD Size, PDWORD Type) PURE;
    STDMETHOD(AddComponent)(const WCHAR* Name, const void* Data, DWORD Size, DWORD Type) PURE;
    STDMETHOD(GetDevice)(GUID* Device) PURE;
    STDMETHOD(SetDevice)(const GUID* Device) PURE;
    STDMETHOD(BuildFromDPADDRESS)(LPVOID Address, DWORD Size) PURE;
};
