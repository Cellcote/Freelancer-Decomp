// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#pragma once

#include <bit>

// A float and its 32-bit pattern, one to the other, with std::bit_cast: no aliasing UB, and no
// FLD/FSTP of the value in between, so a raw immediate such as retail's `PUSH 0x3cf5c28f` comes
// back bit for bit.
static_assert(sizeof(float) == sizeof(unsigned int), "float and unsigned int must be 32-bit");

constexpr float FloatFromBits(unsigned int Bits)
{
    return std::bit_cast<float>(Bits);
}

constexpr unsigned int BitsFromFloat(float Value)
{
    return std::bit_cast<unsigned int>(Value);
}

// The bits of a float read where it lies. Taking it by reference keeps the value off the x87
// stack on Win32 (/arch:IA32), so a signaling NaN comes back unchanged; for the RPC codecs,
// whose retail senders copy float fields with plain dword MOVs.
constexpr unsigned int BitsFromFloatRef(const float& Value)
{
    return std::bit_cast<unsigned int>(Value);
}
