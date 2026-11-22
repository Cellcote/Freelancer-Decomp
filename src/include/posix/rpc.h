// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): <rpc.h> on macOS and Linux (src/include/posix, non-Windows only): UuidCreate, a
// random (version 4) UUID from the C++ library's random device.

#pragma once

#include <windows.h>

#include <random>

typedef GUID UUID;
typedef LONG RPC_STATUS;
#define RPC_S_OK 0

inline RPC_STATUS UuidCreate(UUID* Out)
{
    std::random_device Device;
    unsigned char Bytes[16];
    for (unsigned char& Byte : Bytes)
        Byte = (unsigned char)(Device() & 0xFF);
    Bytes[6] = (unsigned char)((Bytes[6] & 0x0F) | 0x40); // version 4
    Bytes[8] = (unsigned char)((Bytes[8] & 0x3F) | 0x80); // RFC 4122 variant
    std::memcpy(Out, Bytes, sizeof(Bytes));
    return RPC_S_OK;
}
