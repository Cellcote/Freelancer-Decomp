// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// Wide text at the packet-wire boundary.
//
// Retail's wire carries wide text as UTF-16LE code units, two bytes per character, and
// every length on the wire counts those units. On Windows wchar_t IS that unit, so the
// codecs copied wchar_t buffers straight onto the wire (`PutBytes(Text, Count * 2u)`).
// On POSIX wchar_t is four bytes, so the port keeps native wchar_t in memory and
// converts here, one wire unit per wchar_t (UCS-2, as retail: lengths stay wcslen-based
// and unchanged; a wchar_t above 0xFFFF becomes U+FFFD). On Windows every helper is the
// original call.
//
// The stream helpers are templates over the stream type so this header needs neither
// FRpcMarshalStream (rpclocal) nor FRpcWireStream (remote codecs). Each keeps the
// wrapped call's failure behaviour exactly: one bounds check over Count * 2u bytes, and
// the destination is untouched when that check fails.

#include <cstddef>
#include <cstring>
#include <vector>

// The byte size of an in-memory buffer of Count wchar_t. On Windows this is the
// original `Count * 2u` in unsigned int arithmetic (same wrap); elsewhere it is
// computed in size_t.
inline std::size_t RpcWideBytes(unsigned int Count)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        return Count * 2u;
    }
    else
    {
        return static_cast<std::size_t>(Count) * sizeof(wchar_t);
    }
}

// Writes Count UTF-16LE units (Count * 2 bytes) to Dest from Count wchar_t.
inline void RpcWideToWire(void* Dest, const wchar_t* Src, unsigned int Count)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        if (Count != 0)
            std::memcpy(Dest, Src, Count * 2u);
    }
    else
    {
        unsigned char* Out = static_cast<unsigned char*>(Dest);
        for (unsigned int i = 0; i < Count; ++i)
        {
            const unsigned long Code = static_cast<unsigned long>(Src[i]);
            const unsigned int Unit = (Code > 0xffffu) ? 0xfffdu : static_cast<unsigned int>(Code);
            Out[i * 2u] = static_cast<unsigned char>(Unit & 0xffu);
            Out[i * 2u + 1u] = static_cast<unsigned char>((Unit >> 8) & 0xffu);
        }
    }
}

// Reads Count UTF-16LE units (Count * 2 bytes) from Src into Count wchar_t at Dest.
// Src may be Dest itself (the wire bytes sitting at the start of the wchar_t buffer):
// the expansion runs backwards, so each unit is read before it is overwritten.
inline void RpcWireToWide(wchar_t* Dest, const void* Src, unsigned int Count)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        if (Count != 0 && static_cast<const void*>(Dest) != Src)
            std::memcpy(Dest, Src, Count * 2u);
    }
    else
    {
        const unsigned char* In = static_cast<const unsigned char*>(Src);
        for (unsigned int i = Count; i-- != 0;)
        {
            const unsigned int Unit = static_cast<unsigned int>(In[i * 2u]) |
                                      (static_cast<unsigned int>(In[i * 2u + 1u]) << 8);
            Dest[i] = static_cast<wchar_t>(Unit);
        }
    }
}

// The wire-unit count a byte run of `Count * 2u` (in unsigned int arithmetic, as the
// codecs compute it) actually covers -- equal to Count unless the product wrapped.
inline unsigned int RpcWireUnits(unsigned int Count)
{
    return (Count * 2u) >> 1;
}

// Stream.PutBytes(Src, Count * 2u) with the wide text converted to wire units.
template <typename TStream>
inline bool RpcPutBytesWide(TStream& Stream, const wchar_t* Src, unsigned int Count)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        return Stream.PutBytes(Src, Count * 2u);
    }
    else
    {
        std::vector<unsigned char> Wire(static_cast<std::size_t>(Count * 2u));
        RpcWideToWire(Wire.data(), Src, RpcWireUnits(Count));
        return Stream.PutBytes(Wire.data(), Count * 2u);
    }
}

// Stream.GetBytes(Dest, Count * 2u) into a wchar_t buffer of at least Count elements.
template <typename TStream>
inline bool RpcGetBytesWide(TStream& Stream, wchar_t* Dest, unsigned int Count)
{
    if (!Stream.GetBytes(Dest, Count * 2u))
        return false;
    if constexpr (sizeof(wchar_t) != 2)
        RpcWireToWide(Dest, Dest, RpcWireUnits(Count));
    return true;
}

// Stream.PutBlock(Src, Count * 2u) with the wide text converted to wire units.
template <typename TStream>
inline bool RpcPutBlockWide(TStream& Stream, const wchar_t* Src, unsigned int Count)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        return Stream.PutBlock(Src, Count * 2u);
    }
    else
    {
        std::vector<unsigned char> Wire(static_cast<std::size_t>(Count * 2u));
        RpcWideToWire(Wire.data(), Src, RpcWireUnits(Count));
        return Stream.PutBlock(Wire.data(), Count * 2u);
    }
}

// Stream.PutReserved(Src, Count * 2u) -- the copy into a span a separate Reserve has
// already checked -- with the wide text converted to wire units.
template <typename TStream>
inline void RpcPutReservedWide(TStream& Stream, const wchar_t* Src, unsigned int Count)
{
    if constexpr (sizeof(wchar_t) == 2)
    {
        Stream.PutReserved(Src, Count * 2u);
    }
    else
    {
        std::vector<unsigned char> Wire(static_cast<std::size_t>(Count * 2u));
        RpcWideToWire(Wire.data(), Src, RpcWireUnits(Count));
        Stream.PutReserved(Wire.data(), Count * 2u);
    }
}

// Stream.Get(Dest, Count * 2u) into a wchar_t buffer of at least Count elements.
template <typename TStream>
inline bool RpcGetWide(TStream& Stream, wchar_t* Dest, unsigned int Count)
{
    if (!Stream.Get(Dest, Count * 2u))
        return false;
    if constexpr (sizeof(wchar_t) != 2)
        RpcWireToWide(Dest, Dest, RpcWireUnits(Count));
    return true;
}
