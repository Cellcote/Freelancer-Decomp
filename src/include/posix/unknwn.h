// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the SDK's <unknwn.h> and the COM basics of <objbase.h> on macOS and
// Linux (src/include/posix, non-Windows only; posix/windows.h includes it, as the SDK's
// <windows.h> brings COM in through <ole2.h>).
//
// - GUID with the SDK's layout (a 32-bit Data1), IID / CLSID and their REF types, and == on
//   them, as <guiddef.h> has for C++.
// - IUnknown and IClassFactory with COM's vtable order, and the STDMETHOD macros. The calling
//   convention is empty here.
// - The common HRESULTs, with the SDK's values.
// - CoInitialize / CoUninitialize do nothing, and CoCreateInstance finds no class
//   (REGDB_E_CLASSNOTREG): there is no COM here. A port that needs an object makes it itself
//   (DALib's DirectPlay server comes from DPNet, src/DALib/DPNetAdapter.h).

#pragma once

#if defined(_WIN32)
#error "posix/unknwn.h is the stand-in for macOS and Linux; Windows uses the SDK's unknwn.h"
#endif

#include <windows.h>

#include <cstring>

// ---------------------------------------------------------------------------------------------
// GUID
// ---------------------------------------------------------------------------------------------

typedef struct _GUID
{
    DWORD Data1;
    WORD Data2;
    WORD Data3;
    BYTE Data4[8];
} GUID;
static_assert(sizeof(GUID) == 16, "GUID is 16 bytes, as on Windows");

typedef GUID IID;
typedef GUID CLSID;
typedef GUID* LPGUID;
typedef GUID const* LPCGUID;
typedef IID* LPIID;
typedef CLSID* LPCLSID;
typedef GUID const& REFGUID;
typedef IID const& REFIID;
typedef CLSID const& REFCLSID;

inline bool IsEqualGUID(REFGUID Left, REFGUID Right)
{
    return std::memcmp(&Left, &Right, sizeof(GUID)) == 0;
}
#define IsEqualIID(Left, Right) IsEqualGUID(Left, Right)
#define IsEqualCLSID(Left, Right) IsEqualGUID(Left, Right)
inline bool operator==(REFGUID Left, REFGUID Right)
{
    return IsEqualGUID(Left, Right);
}
inline bool operator!=(REFGUID Left, REFGUID Right)
{
    return !IsEqualGUID(Left, Right);
}

// ---------------------------------------------------------------------------------------------
// HRESULTs
// ---------------------------------------------------------------------------------------------

#define MAKE_HRESULT(Severity, Facility, Code)                                                     \
    ((HRESULT)(((DWORD)(Severity) << 31) | ((DWORD)(Facility) << 16) | ((DWORD)(Code))))
#define HRESULT_CODE(Result) ((Result) & 0xFFFF)
#define HRESULT_FACILITY(Result) (((Result) >> 16) & 0x1FFF)
#define HRESULT_SEVERITY(Result) (((Result) >> 31) & 0x1)

#define E_NOTIMPL ((HRESULT)0x80004001)
#define E_NOINTERFACE ((HRESULT)0x80004002)
#define E_POINTER ((HRESULT)0x80004003)
#define E_ABORT ((HRESULT)0x80004004)
#define E_UNEXPECTED ((HRESULT)0x8000FFFF)
#define E_ACCESSDENIED ((HRESULT)0x80070005)
#define E_HANDLE ((HRESULT)0x80070006)
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#define E_INVALIDARG ((HRESULT)0x80070057)
#define E_PENDING ((HRESULT)0x8000000A)
#define CLASS_E_NOAGGREGATION ((HRESULT)0x80040110)
#define CLASS_E_CLASSNOTAVAILABLE ((HRESULT)0x80040111)
#define REGDB_E_CLASSNOTREG ((HRESULT)0x80040154)
#define CO_E_NOTINITIALIZED ((HRESULT)0x800401F0)

// ---------------------------------------------------------------------------------------------
// Interfaces
// ---------------------------------------------------------------------------------------------

#define STDMETHODCALLTYPE
#define STDMETHODVCALLTYPE
#define STDAPICALLTYPE
#define STDMETHOD(Method) virtual HRESULT STDMETHODCALLTYPE Method
#define STDMETHOD_(Type, Method) virtual Type STDMETHODCALLTYPE Method
#define STDMETHODIMP HRESULT STDMETHODCALLTYPE
#define STDMETHODIMP_(Type) Type STDMETHODCALLTYPE
#define STDAPI extern "C" HRESULT STDAPICALLTYPE
#define PURE = 0
#define THIS_
#define THIS void

// {00000000-0000-0000-C000-000000000046}
inline constexpr IID IID_IUnknown = {
    0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
// {00000001-0000-0000-C000-000000000046}
inline constexpr IID IID_IClassFactory = {
    0x00000001, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

// No virtual destructor, as COM's: an object goes with its last Release.
struct IUnknown
{
    STDMETHOD(QueryInterface)(REFIID Iid, void** Object) PURE;
    STDMETHOD_(ULONG, AddRef)() PURE;
    STDMETHOD_(ULONG, Release)() PURE;
};
typedef IUnknown* LPUNKNOWN;

struct IClassFactory : public IUnknown
{
    STDMETHOD(CreateInstance)(IUnknown* Outer, REFIID Iid, void** Object) PURE;
    STDMETHOD(LockServer)(BOOL Lock) PURE;
};
typedef IClassFactory* LPCLASSFACTORY;

// __uuidof(Interface): MSVC reads the IID from the interface's __declspec(uuid). Here a stand-in
// header names it with FL_POSIX_UUIDOF(Interface, the IID's parts), after the interface.
template <class Interface>
struct FPosixUuidOf;
#define FL_POSIX_UUIDOF(Interface, Data1, Data2, Data3, ...)                                       \
    template <>                                                                                    \
    struct FPosixUuidOf<Interface>                                                                 \
    {                                                                                              \
        static constexpr IID Value = {Data1, Data2, Data3, __VA_ARGS__};                           \
    };
#define __uuidof(Interface) (FPosixUuidOf<Interface>::Value)
FL_POSIX_UUIDOF(IUnknown, 0x00000000, 0x0000, 0x0000,
                {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46})
FL_POSIX_UUIDOF(IClassFactory, 0x00000001, 0x0000, 0x0000,
                {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46})

// ---------------------------------------------------------------------------------------------
// The COM library
// ---------------------------------------------------------------------------------------------

#define CLSCTX_INPROC_SERVER 0x1
#define CLSCTX_INPROC_HANDLER 0x2
#define CLSCTX_LOCAL_SERVER 0x4
#define CLSCTX_REMOTE_SERVER 0x10
#define CLSCTX_INPROC (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER)
#define CLSCTX_ALL                                                                                 \
    (CLSCTX_INPROC_SERVER | CLSCTX_INPROC_HANDLER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)
#define CLSCTX_SERVER (CLSCTX_INPROC_SERVER | CLSCTX_LOCAL_SERVER | CLSCTX_REMOTE_SERVER)

// Nothing to set up: S_OK every time (Windows answers S_FALSE when the thread already is).
inline HRESULT CoInitialize(LPVOID)
{
    return S_OK;
}
inline void CoUninitialize() {}

// There are no registered classes here: every class is not registered, as on a Windows without
// it, and *Object is cleared.
inline HRESULT CoCreateInstance(REFCLSID, LPUNKNOWN, DWORD, REFIID, LPVOID* Object)
{
    if (Object == nullptr)
        return E_POINTER;
    *Object = nullptr;
    return REGDB_E_CLASSNOTREG;
}
