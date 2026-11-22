// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): MSVC's <intrin.h> on macOS and Linux (src/include/posix, non-Windows only): the
// intrinsics the client uses, with MSVC's meaning. SSE comes from posix/xmmintrin.h.

#pragma once

#include "posix/xmmintrin.h"

#include <chrono>
#include <cstddef>
#include <cstdint>

#if defined(__x86_64__) || defined(__i386__)
#include <cpuid.h>
#include <x86intrin.h>
// <cpuid.h> spells __cpuid as a five-argument macro (__get_cpuid has it expanded already);
// MSVC's is the function below.
#undef __cpuid
#endif

// _BitScanForward / _BitScanReverse: the index of the lowest / highest set bit; 0 (and Index
// untouched) when Mask is 0.
template <class TIndex>
inline unsigned char _BitScanForward(TIndex* Index, std::uint32_t Mask)
{
    if (Mask == 0)
        return 0;
    *Index = (TIndex)__builtin_ctz(Mask);
    return 1;
}
template <class TIndex>
inline unsigned char _BitScanReverse(TIndex* Index, std::uint32_t Mask)
{
    if (Mask == 0)
        return 0;
    *Index = (TIndex)(31 - __builtin_clz(Mask));
    return 1;
}

// __cpuid: the CPU's answer on x86; zeros elsewhere (no x86 feature is present).
inline void __cpuid(int Registers[4], int Leaf)
{
#if defined(__x86_64__) || defined(__i386__)
    unsigned int A = 0, B = 0, C = 0, D = 0;
    __get_cpuid((unsigned int)Leaf, &A, &B, &C, &D);
    Registers[0] = (int)A;
    Registers[1] = (int)B;
    Registers[2] = (int)C;
    Registers[3] = (int)D;
#else
    (void)Leaf;
    Registers[0] = Registers[1] = Registers[2] = Registers[3] = 0;
#endif
}

#if !(defined(__x86_64__) || defined(__i386__))
// __rdtsc: a monotonic tick count (nanoseconds) where there is no time-stamp counter.
inline unsigned long long __rdtsc()
{
    return (unsigned long long)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
#endif

// __movsd / __movsb: REP MOVSD / MOVSB, a forward copy (overlap behaves as the instruction).
template <class TDword>
inline void __movsd(TDword* Destination, const TDword* Source, std::size_t Count)
{
    static_assert(sizeof(TDword) == 4, "__movsd copies dwords");
    for (std::size_t Index = 0; Index < Count; ++Index)
        Destination[Index] = Source[Index];
}
inline void __movsb(unsigned char* Destination, const unsigned char* Source, std::size_t Count)
{
    for (std::size_t Index = 0; Index < Count; ++Index)
        Destination[Index] = Source[Index];
}
