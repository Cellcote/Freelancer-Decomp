// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Text in a locked resource (LockResource): string tables and the HTML / RDL infocards are
// UTF-16LE, two bytes a character. On Windows wchar_t is that unit and retail reads the
// resource as wchar_t in place; every helper here is that original access. On macOS and Linux
// wchar_t is four bytes, so the text is converted one unit per wchar_t (as the wire's text,
// rpc_wire_wide.h): lengths and counts in units stay counts in characters.

#include <cstddef>
#include <cstring>
#include <cwchar>

#if !defined(_WIN32)
#include "platform.h" // Platform::ResourceWideText
#endif

// One unit of resource text, as the resource holds it.
#if defined(_WIN32)
using ResourceUnit = wchar_t;
#else
using ResourceUnit = char16_t;
#endif

// Resource text that is kept as wchar_t (a TextPtrNode points into it): Text itself on Windows;
// otherwise the converted copy, which lives as long as the resource module
// (Platform::ResourceWideText), or nullptr when that copy cannot be made.
inline wchar_t const* ResourceWide(ResourceUnit const* Text)
{
#if defined(_WIN32)
    return Text;
#else
    return Platform::ResourceWideText(Text);
#endif
}

// memcpy(Dest, Src, Count * sizeof(wchar_t)) from resource text.
inline void ResourceTextCopy(wchar_t* Dest, ResourceUnit const* Src, std::size_t Count)
{
#if defined(_WIN32)
    std::memcpy(Dest, Src, Count * sizeof(wchar_t));
#else
    for (std::size_t Index = 0; Index < Count; ++Index)
        Dest[Index] = static_cast<wchar_t>(Src[Index]);
#endif
}

// wcsncpy(Dest, Src, Count) from resource text: up to Count characters, stopping after a
// terminator, and the rest of the Count zero-filled.
inline wchar_t* ResourceTextCopyN(wchar_t* Dest, ResourceUnit const* Src, std::size_t Count)
{
#if defined(_WIN32)
    return wcsncpy(Dest, Src, Count);
#else
    std::size_t Index = 0;
    for (; Index < Count && Src[Index] != 0; ++Index)
        Dest[Index] = static_cast<wchar_t>(Src[Index]);
    for (; Index < Count; ++Index)
        Dest[Index] = 0;
    return Dest;
#endif
}
