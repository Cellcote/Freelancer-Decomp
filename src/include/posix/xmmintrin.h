// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): <xmmintrin.h> on macOS and Linux (src/include/posix, non-Windows only). On x86 it
// is the compiler's own. Elsewhere (Apple Silicon) it emulates the scalar SSE operations the
// client uses (alchemy's retail-exact math), with SSE's results: the lane-0 arithmetic, the
// other lanes kept from the first operand, conversions that truncate or round to nearest even,
// and 0x80000000 for a value out of int range or NaN.

#pragma once

#if defined(__x86_64__) || defined(__i386__)
#include_next <xmmintrin.h>
#else

#include <climits>
#include <cmath>
#include <cstdint>
#include <cstring>

struct alignas(16) __m128
{
    float Lane[4];
};

inline __m128 _mm_setzero_ps()
{
    return __m128{{0.0f, 0.0f, 0.0f, 0.0f}};
}
inline __m128 _mm_set_ss(float Value)
{
    return __m128{{Value, 0.0f, 0.0f, 0.0f}};
}
inline __m128 _mm_load_ss(const float* Value)
{
    return _mm_set_ss(*Value);
}
inline void _mm_store_ss(float* Out, __m128 Value)
{
    *Out = Value.Lane[0];
}

inline __m128 _mm_add_ss(__m128 A, __m128 B)
{
    A.Lane[0] = A.Lane[0] + B.Lane[0];
    return A;
}
inline __m128 _mm_sub_ss(__m128 A, __m128 B)
{
    A.Lane[0] = A.Lane[0] - B.Lane[0];
    return A;
}
inline __m128 _mm_mul_ss(__m128 A, __m128 B)
{
    A.Lane[0] = A.Lane[0] * B.Lane[0];
    return A;
}
inline __m128 _mm_or_ps(__m128 A, __m128 B)
{
    for (int Index = 0; Index < 4; ++Index)
    {
        std::uint32_t Left;
        std::uint32_t Right;
        std::memcpy(&Left, &A.Lane[Index], 4);
        std::memcpy(&Right, &B.Lane[Index], 4);
        Left |= Right;
        std::memcpy(&A.Lane[Index], &Left, 4);
    }
    return A;
}
inline __m128 _mm_cvtsi32_ss(__m128 A, int Value)
{
    A.Lane[0] = (float)Value;
    return A;
}

namespace FlSse
{
    // CVTSS2SI / CVTTSS2SI's "integer indefinite" for NaN and values out of range.
inline bool OutOfRange(float Value)
{
    return std::isnan(Value) || Value >= 2147483648.0f || Value < -2147483648.0f;
}
}
inline int _mm_cvtt_ss2si(__m128 A)
{
    return FlSse::OutOfRange(A.Lane[0]) ? INT_MIN : (int)A.Lane[0];
}
inline int _mm_cvttss_si32(__m128 A)
{
    return _mm_cvtt_ss2si(A);
}
inline int _mm_cvtss_si32(__m128 A)
{
    // MXCSR's default rounding: to nearest, ties to even.
    const float Rounded = std::nearbyint(A.Lane[0]);
    return FlSse::OutOfRange(Rounded) ? INT_MIN : (int)Rounded;
}
inline int _mm_cvt_ss2si(__m128 A)
{
    return _mm_cvtss_si32(A);
}

#endif
