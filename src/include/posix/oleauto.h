// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): <oleauto.h> on macOS and Linux (src/include/posix, non-Windows only): BSTR and
// VARIANT as far as the code uses them. A BSTR keeps its byte length in the four bytes before
// the text and a terminating null, as OLE's; OLECHAR is wchar_t (32-bit here, 16 on Windows:
// no BSTR crosses a file or the network).

#pragma once

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

typedef wchar_t OLECHAR;
typedef OLECHAR* BSTR;
typedef OLECHAR* LPOLESTR;
typedef const OLECHAR* LPCOLESTR;
typedef short VARIANT_BOOL;
typedef unsigned short VARTYPE;
#define VARIANT_TRUE ((VARIANT_BOOL) - 1)
#define VARIANT_FALSE ((VARIANT_BOOL)0)

enum VARENUM
{
    VT_EMPTY = 0,
    VT_NULL = 1,
    VT_I2 = 2,
    VT_I4 = 3,
    VT_BSTR = 8,
    VT_BOOL = 11,
    VT_UNKNOWN = 13,
};

inline BSTR SysAllocStringByteLen(LPCSTR Source, UINT Bytes)
{
    unsigned char* const Block = static_cast<unsigned char*>(
        std::calloc(1, sizeof(std::uint32_t) + Bytes + sizeof(OLECHAR)));
    if (Block == nullptr)
        return nullptr;
    const std::uint32_t Length = Bytes;
    std::memcpy(Block, &Length, sizeof(Length));
    if (Source != nullptr)
        std::memcpy(Block + sizeof(Length), Source, Bytes);
    return reinterpret_cast<BSTR>(Block + sizeof(Length));
}
inline BSTR SysAllocString(const OLECHAR* Source)
{
    if (Source == nullptr)
        return nullptr;
    return SysAllocStringByteLen(reinterpret_cast<LPCSTR>(Source),
                                 (UINT)(std::wcslen(Source) * sizeof(OLECHAR)));
}
inline void SysFreeString(BSTR Text)
{
    if (Text != nullptr)
        std::free(reinterpret_cast<unsigned char*>(Text) - sizeof(std::uint32_t));
}
inline UINT SysStringByteLen(BSTR Text)
{
    std::uint32_t Length = 0;
    if (Text != nullptr)
        std::memcpy(&Length, reinterpret_cast<unsigned char*>(Text) - sizeof(Length),
                    sizeof(Length));
    return Length;
}
inline UINT SysStringLen(BSTR Text)
{
    return SysStringByteLen(Text) / sizeof(OLECHAR);
}

typedef struct tagVARIANT
{
    VARTYPE vt;
    WORD wReserved1;
    WORD wReserved2;
    WORD wReserved3;
    union
    {
        LONG lVal;
        short iVal;
        VARIANT_BOOL boolVal;
        BSTR bstrVal;
        IUnknown* punkVal;
        void* byref;
    };
} VARIANT;
typedef VARIANT VARIANTARG;

inline void VariantInit(VARIANT* Value)
{
    if (Value != nullptr)
        std::memset(Value, 0, sizeof(*Value));
}
inline HRESULT VariantClear(VARIANT* Value)
{
    if (Value == nullptr)
        return E_INVALIDARG;
    if (Value->vt == VT_BSTR)
        SysFreeString(Value->bstrVal);
    else if (Value->vt == VT_UNKNOWN && Value->punkVal != nullptr)
        Value->punkVal->Release();
    std::memset(Value, 0, sizeof(*Value));
    return S_OK;
}
