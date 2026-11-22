// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): <comdef.h> on macOS and Linux (src/include/posix, non-Windows only): the OLE types
// and the slice of the compiler COM support the code uses: _bstr_t (owning a BSTR, giving its
// text) and _com_issue_error (which throws, as the default handler does).

#pragma once

#include "posix/oleauto.h"

#include <stdexcept>

#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000E)
#endif

class _com_error : public std::runtime_error
{
public:
    explicit _com_error(HRESULT Result) : std::runtime_error("COM error"), Hr(Result) {}
    HRESULT Error() const { return Hr; }

private:
    HRESULT Hr;
};

[[noreturn]] inline void _com_issue_error(HRESULT Result)
{
    throw _com_error(Result);
}

class _bstr_t
{
public:
    // Copy false takes ownership of Text, as the compiler support's _bstr_t(BSTR, false).
    _bstr_t(BSTR Text, bool Copy)
        : Value(Copy ? SysAllocStringByteLen(reinterpret_cast<LPCSTR>(Text), SysStringByteLen(Text))
                     : Text)
    {
        if (Text != nullptr && Value == nullptr)
            _com_issue_error(E_OUTOFMEMORY);
    }
    _bstr_t(const _bstr_t&) = delete;
    _bstr_t& operator=(const _bstr_t&) = delete;
    ~_bstr_t() { SysFreeString(Value); }
    operator wchar_t*() const { return Value; }
    operator const wchar_t*() const { return Value; }

private:
    BSTR Value;
};
