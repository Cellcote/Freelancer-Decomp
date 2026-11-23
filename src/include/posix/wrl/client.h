// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the Windows SDK's <wrl/client.h> on macOS and Linux
// (src/include/posix, non-Windows only): Microsoft::WRL::ComPtr, with the members the ported
// modules use. It holds one reference and releases it when it goes, as WRL's does.

#pragma once

#if defined(_WIN32)
#error "posix/wrl/client.h is the stand-in for macOS and Linux; Windows uses the Windows SDK's"
#endif

#include <cstddef>

namespace Microsoft
{
namespace WRL
{
template <class T>
class ComPtr
{
public:
    ComPtr() = default;
    ComPtr(std::nullptr_t) {}
    ComPtr(T* Other) : Pointer(Other)
    {
        if (Pointer != nullptr)
            Pointer->AddRef();
    }
    ComPtr(const ComPtr& Other) : ComPtr(Other.Pointer) {}
    ComPtr(ComPtr&& Other) noexcept : Pointer(Other.Pointer) { Other.Pointer = nullptr; }
    ~ComPtr() { Reset(); }
    ComPtr& operator=(ComPtr Other) noexcept
    {
        T* const Old = Pointer;
        Pointer = Other.Pointer;
        Other.Pointer = Old;
        return *this;
    }

    T* Get() const { return Pointer; }
    T* operator->() const { return Pointer; }
    explicit operator bool() const { return Pointer != nullptr; }
    bool operator==(std::nullptr_t) const { return Pointer == nullptr; }
    bool operator!=(std::nullptr_t) const { return Pointer != nullptr; }
    // The address of the pointer, for an out parameter (whatever was held is not released).
    T** GetAddressOf() { return &Pointer; }
    // Releases what was held, then the address, for an out parameter.
    T** ReleaseAndGetAddressOf()
    {
        Reset();
        return &Pointer;
    }
    unsigned long Reset()
    {
        T* const Old = Pointer;
        Pointer = nullptr;
        return Old != nullptr ? static_cast<unsigned long>(Old->Release()) : 0;
    }
    T* Detach()
    {
        T* const Old = Pointer;
        Pointer = nullptr;
        return Old;
    }

private:
    T* Pointer = nullptr;
};
} // namespace WRL
} // namespace Microsoft
