// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a module that uses LowArena, built twice (arena_probe_a, arena_probe_b) for
// module_tests: a block one module allocates and encodes must decode in the other, and a block
// one frees must come back from the other's next allocation (one arena per process, dacom's).

#include "x64_low_arena.h"

#define FL_PROBE_JOIN2(A, B) A##B
#define FL_PROBE_JOIN(A, B) FL_PROBE_JOIN2(A, B)
#define FL_PROBE(Name) FL_PROBE_JOIN(FL_PROBE_PREFIX, Name)

// A block of 48 bytes holding Value, encoded.
extern "C" FlLong FL_PROBE(Make)(int Value)
{
    int* const Block = static_cast<int*>(LowArena::Allocate(48));
    *Block = Value;
    return LowArena::Encode(Block);
}

// The value in an encoded block.
extern "C" int FL_PROBE(Read)(FlLong Encoded) { return *LowArena::Decode<int>(Encoded); }

// Frees an encoded block.
extern "C" void FL_PROBE(Free)(FlLong Encoded) { LowArena::Free(LowArena::Decode<int>(Encoded), 48); }
