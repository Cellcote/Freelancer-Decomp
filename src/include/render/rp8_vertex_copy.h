// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.

#pragma once

// RP8's IVertexBufferManager::CopyVertexData (slot 8, FUN_06d114c5 -> FUN_06d1f040) for the
// pipelines that are not RP8 (RPGPU): a source-vertex descriptor's streams written into a
// vertex buffer of a D3D8 flexible vertex format. RP8 (src/RP8/RP8.cpp, the namespace before
// FDirectX8::CopyVertexData) is the reference; this is the same dispatch and the same bytes:
//
// - The selector is (SrcFvf == DstFvf) | (Flags & 6): 2 an interleaved source, 4 an indexed one.
// - 3 (interleaved, same format): the vertices copied as they are.
// - 6 (interleaved and indexed, another format): nothing (retail's switch has no case for it).
// - 1 and 5 (separate streams, same format): retail's two 0x41-entry jump tables, indexed by
//   Rp8FvfJumpIndex. An entry is the generic converter, a fixed-layout converter, or an empty
//   function: every pre-transformed (XYZRHW) format and every format with a specular colour but
//   no diffuse one copies nothing.
// - 0 and 4 (separate streams, another format): a fixed-layout converter for a few format
//   pairs, the generic converter for the rest.
//
// The generic converter writes, per vertex at Dst + (StartVertex + n) * Rp8FvfStride(DstFvf):
// 12 position bytes; the normal's 12 when the destination has one (copied when the source has
// one too); the diffuse colour's 4 likewise; then each destination texture set, copied from the
// source set of the same index when there is one (the source set's ByteSize bytes), the cursor
// moving by the destination set's size. Each source element is read at its stream's Stride. A
// destination specular colour, blend weights and the fourth position float of XYZRHW are not
// accounted for, as in retail. Bytes it does not copy are left as they were.
//
// A fixed-layout converter (FRp8FixedLayout) has its own destination stride and offsets, and
// reads the source at retail's literal strides whatever the descriptor says: 12 bytes a
// position or normal, 4 a diffuse colour, 8 a texture set (8 bytes copied). Retail picks it
// by the jump index or the format pair alone, whatever the texture sets' sizes; so does this.
//
// Where retail writes outside the memory it was given, this does not: a converter that would
// write past a vertex record copies nothing (WritesPastRecord), a generic texture copy stops at
// its vertex's end, and
// Rp8CopyVertexDataAt refuses a copy that would leave the buffer.
//
// The descriptor has two texture streams; retail reads a source's third and later sets past
// them (the descriptor's tail and beyond). Here a source set past the second copies nothing.

#include "render/IRenderPipeline.h"
#include "render/rp8_semantics.h"

#include <cstddef>
#include <cstring>

namespace Rp8VertexCopyDetail
{
// Rp8TexelByteSize (RP8.cpp): a texture set's size code (0 two floats, 1 three, 2 four, 3 one).
inline unsigned int TexelBytes(unsigned int Code)
{
    switch (Code & 3)
    {
    case 0:
        return 8;
    case 1:
        return 0xC;
    case 2:
        return 0x10;
    default:
        return 4;
    }
}

// Rp8FvfJumpIndex (RP8.cpp): the jump tables' index, retail's local_8.
inline unsigned int JumpIndex(unsigned int Fvf)
{
    const unsigned int TexBucket = (Fvf >> 8) & 3;
    unsigned int High = TexBucket << 4;
    if (TexBucket == 2)
        High = 0x30;
    return High | (((Fvf >> 6) & 3) << 2) | (((Fvf >> 4) & 1) << 1) | ((Fvf >> 2) & 1);
}

// Rp8JumpTableIndex (RP8.cpp): entry 0x40 for three or more texture sets, not pre-transformed.
inline unsigned int TableIndex(unsigned int DstFvf)
{
    if (((DstFvf & 0xF00) >> 8) < 3 || (DstFvf & 4) != 0)
        return JumpIndex(DstFvf);
    return 0x40;
}

// Whether a jump-table entry is the empty ConvNoOp (FUN_06d1f5fd). Both tables have it at the
// same entries: an odd index (XYZRHW), the rows whose colour bits are 2 (specular without
// diffuse) and 0x20..0x2F, which JumpIndex never gives. Entry 0x40 is the generic converter.
inline bool TableCopiesNothing(unsigned int Index)
{
    if (Index == 0x40)
        return false;
    return (Index & 1) != 0 || ((Index >> 2) & 3) == 2 || (Index & 0x30) == 0x20;
}

// One fixed-layout converter: the destination stride and where the normal, the diffuse colour
// and texture sets 0 and 1 go (-1: not written). The position is at 0.
struct FRp8FixedLayout
{
    unsigned int Stride;
    int Normal;
    int Diffuse;
    int Tex0;
    int Tex1;
};

// RP8.cpp's converters, by their retail addresses.
inline constexpr FRp8FixedLayout kPosNormTex1_S20 = {
    0x20, 0xC, -1, 0x18, -1};     // ConvSeqPosNormTex1_S20 (table A 0x12), 06d20ad2
inline constexpr FRp8FixedLayout kPosNormTex1_S28 = {0x28, 0xC, -1, 0x18, -1}; // 06d220df, 06d1fe6f
inline constexpr FRp8FixedLayout kPosNormTex1_S2C = {0x2C, 0xC, -1, 0x1C, -1}; // 06d21ea6, 06d20138
inline constexpr FRp8FixedLayout kPosNormTex2_S2C = {0x2C, 0xC, -1, 0x1C,
                                                     0x24}; // 06d21be9, 06d20754
inline constexpr FRp8FixedLayout kPosNormTex2_S28 = {0x28, 0xC, -1, 0x18, 0x20}; // 06d210dd
inline constexpr FRp8FixedLayout kPosNormDiffTex1_S24 = {0x24, 0xC, 0x18, 0x1C, -1}; // 06d20d92
inline constexpr FRp8FixedLayout kPosNormDiffTex1_S2C = {0x2C, 0xC, 0x18, 0x1C, -1}; // 06d20409
inline constexpr FRp8FixedLayout kPosNorm_S18 = {0x18, 0xC, -1, -1, -1}; // 06d1f602
inline constexpr FRp8FixedLayout kPosNorm_S20 = {0x20, 0xC, -1, -1, -1}; // 06d1fc57
inline constexpr FRp8FixedLayout kPosNorm_S28 = {0x28, 0xC, -1, -1, -1}; // 06d1f818
inline constexpr FRp8FixedLayout kPosNorm_S2C = {0x2C, 0xC, -1, -1, -1}; // 06d1fa33

// The fixed-layout converter RP8 picks, or null for the generic one (the empty entries are
// tested before). Sequential: case 0's chain and table A; indexed: case 4's chain and table B.
inline const FRp8FixedLayout* FixedLayout(unsigned int Selector, unsigned int SrcFvf,
                                          unsigned int DstFvf)
{
    switch (Selector)
    {
    case 0:
        if (SrcFvf == 0x112 && DstFvf == 0x252)
            return &kPosNormTex1_S2C;
        if (SrcFvf == 0x112 && DstFvf == 0x212)
            return &kPosNormTex1_S28;
        if (SrcFvf == 0x212 && DstFvf == 0x252)
            return &kPosNormTex2_S2C;
        return nullptr;
    case 1:
        return TableIndex(DstFvf) == 0x12 ? &kPosNormTex1_S20 : nullptr;
    case 4:
        if (SrcFvf == 0x12)
        {
            if (DstFvf == 0x252)
                return &kPosNorm_S2C;
            if (DstFvf == 0x212)
                return &kPosNorm_S28;
            if (DstFvf == 0x112)
                return &kPosNorm_S20;
        }
        else if (SrcFvf == 0x112)
        {
            if (DstFvf == 0x252)
                return &kPosNormTex1_S2C;
            if (DstFvf == 0x212)
                return &kPosNormTex1_S28;
        }
        else if (SrcFvf == 0x152 && DstFvf == 0x252)
            return &kPosNormDiffTex1_S2C;
        else if (SrcFvf == 0x212 && DstFvf == 0x252)
            return &kPosNormTex2_S2C;
        return nullptr;
    case 5:
        switch (TableIndex(DstFvf))
        {
        case 0x02:
            return &kPosNorm_S18;
        case 0x12:
            return &kPosNormTex1_S20;
        case 0x16:
            return &kPosNormDiffTex1_S24;
        case 0x32:
            return &kPosNormTex2_S28;
        default:
            return nullptr;
        }
    default:
        return nullptr;
    }
}

// The source vertex of a stream for the n-th vertex copied, at Stride bytes a vertex.
inline const unsigned char* Element(const FRpVertexStream& Stream, unsigned int Stride,
                                    unsigned int StartVertex, unsigned int N, bool bIndexed)
{
    const std::size_t Vertex = bIndexed
                                   ? static_cast<std::size_t>(Stream.IndexArray[StartVertex + N])
                                   : static_cast<std::size_t>(StartVertex) + N;
    return Stream.Base + Vertex * Stride;
}

// Where vertex V of the destination goes: Origin + V * Stride + Skew, the offset worked out
// before it is added (Skew lets a caller hand in a pointer to its first vertex).
inline unsigned char* Destination(unsigned char* Origin, std::ptrdiff_t Skew, unsigned int Vertex,
                                  unsigned int Stride)
{
    return Origin + (static_cast<std::ptrdiff_t>(static_cast<std::size_t>(Vertex) * Stride) + Skew);
}

inline void CopyFixed(unsigned char* Origin, std::ptrdiff_t Skew, const FRp8FixedLayout& Layout,
                      const FRpVertexSrcDesc* Src, unsigned int StartVertex, unsigned int Count,
                      bool bIndexed)
{
    for (unsigned int N = 0; N < Count; ++N)
    {
        unsigned char* const Out = Destination(Origin, Skew, StartVertex + N, Layout.Stride);
        std::memcpy(Out, Element(Src->Position, 0xC, StartVertex, N, bIndexed), 0xC);
        if (Layout.Normal >= 0)
            std::memcpy(Out + Layout.Normal, Element(Src->Normal, 0xC, StartVertex, N, bIndexed),
                        0xC);
        if (Layout.Diffuse >= 0)
            std::memcpy(Out + Layout.Diffuse, Element(Src->Diffuse, 4, StartVertex, N, bIndexed),
                        4);
        if (Layout.Tex0 >= 0)
            std::memcpy(Out + Layout.Tex0, Element(Src->Tex[0], 8, StartVertex, N, bIndexed), 8);
        if (Layout.Tex1 >= 0)
            std::memcpy(Out + Layout.Tex1, Element(Src->Tex[1], 8, StartVertex, N, bIndexed), 8);
    }
}

// FUN_06d2230f (sequential) and FUN_06d21450 (indexed).
inline void CopyGeneric(unsigned char* Origin, std::ptrdiff_t Skew, unsigned int DstFvf,
                        const FRpVertexSrcDesc* Src, unsigned int StartVertex, unsigned int Count,
                        bool bIndexed)
{
    const unsigned int SrcFvf = Src->SrcFvf;
    const bool bSrcNormal = (SrcFvf & 0x10) != 0;
    const bool bSrcDiffuse = (SrcFvf & 0x40) != 0;
    const unsigned int SrcTexCount = (SrcFvf & 0xF00) >> 8;
    const bool bDstNormal = (DstFvf & 0x10) != 0;
    const bool bDstDiffuse = (DstFvf & 0x40) != 0;
    const unsigned int DstTexCount = (DstFvf & 0xF00) >> 8;
    const unsigned int DstStride = Rp8FvfStride(DstFvf);

    for (unsigned int N = 0; N < Count; ++N)
    {
        unsigned char* const VertexStart = Destination(Origin, Skew, StartVertex + N, DstStride);
        unsigned char* Cursor = VertexStart;
        std::memcpy(Cursor, Element(Src->Position, Src->Position.Stride, StartVertex, N, bIndexed),
                    0xC);
        Cursor += 0xC;
        if (bDstNormal)
        {
            if (bSrcNormal)
                std::memcpy(Cursor,
                            Element(Src->Normal, Src->Normal.Stride, StartVertex, N, bIndexed),
                            0xC);
            Cursor += 0xC;
        }
        if (bDstDiffuse)
        {
            if (bSrcDiffuse)
                std::memcpy(Cursor,
                            Element(Src->Diffuse, Src->Diffuse.Stride, StartVertex, N, bIndexed),
                            4);
            Cursor += 4;
        }
        for (unsigned int Index = 0; Index < DstTexCount && Index < 8; ++Index)
        {
            if (Index < SrcTexCount && Index < 2)
            {
                // Retail copies ByteSize bytes unchecked; a set wider than the rest of the
                // vertex runs into the next one (or past the last), so the copy stops at the
                // vertex's end. Within the vertex the bytes are retail's.
                const FRpVertexStream& Set = Src->Tex[Index];
                const std::size_t Left = static_cast<std::size_t>(DstStride) -
                                         static_cast<std::size_t>(Cursor - VertexStart);
                const std::size_t Bytes = Set.ByteSize < Left ? Set.ByteSize : Left;
                std::memcpy(Cursor, Element(Set, Set.Stride, StartVertex, N, bIndexed), Bytes);
            }
            Cursor += TexelBytes(DstFvf >> (Index * 2 + 0x10));
        }
    }
}

// Whether the converter RP8 picks would write past each destination vertex record: a fixed
// layout wider than the format (a one-float texture set: 0x30112 is 0x1C bytes, its entry
// writes 0x20), or a generic copy whose position, normal and diffuse bytes alone exceed the
// format's stride (a destination without XYZ: 0x10 is 0xC bytes, the copy writes 0x18).
// Retail writes them anyway; here such a copy writes nothing.
inline bool WritesPastRecord(unsigned int DstFvf, const FRpVertexSrcDesc* Src)
{
    const unsigned int Selector =
        static_cast<unsigned int>(Src->SrcFvf == DstFvf) | (Src->Flags & 6u);
    if (Selector == 3 || Selector == 6)
        return false;
    if ((Selector == 1 || Selector == 5) && TableCopiesNothing(TableIndex(DstFvf)))
        return false;
    const unsigned int FormatStride = Rp8FvfStride(DstFvf);
    if (const FRp8FixedLayout* const Layout = FixedLayout(Selector, Src->SrcFvf, DstFvf))
        return Layout->Stride > FormatStride;
    const unsigned int Mandatory =
        0xC + ((DstFvf & 0x10) != 0 ? 0xC : 0) + ((DstFvf & 0x40) != 0 ? 4 : 0);
    return Mandatory > FormatStride;
}

// FUN_06d1f040 with the destination's vertex V at Origin + V * stride + Skew.
inline void Copy(unsigned char* Origin, std::ptrdiff_t Skew, unsigned int DstFvf,
                 const FRpVertexSrcDesc* Src, unsigned int StartVertex, unsigned int Count)
{
    const unsigned int Selector =
        static_cast<unsigned int>(Src->SrcFvf == DstFvf) | (Src->Flags & 6u);
    if (Selector == 3)
    {
        const unsigned int Stride = Rp8FvfStride(DstFvf);
        std::memcpy(Destination(Origin, Skew, StartVertex, Stride),
                    Src->Position.Base +
                        static_cast<std::size_t>(StartVertex) * Src->Position.Stride,
                    static_cast<std::size_t>(Stride) * Count);
        return;
    }
    if (Selector == 6)
        return;
    if ((Selector == 1 || Selector == 5) && TableCopiesNothing(TableIndex(DstFvf)))
        return;
    if (WritesPastRecord(DstFvf, Src))
        return;
    const bool bIndexed = (Selector & 4) != 0;
    if (const FRp8FixedLayout* const Layout = FixedLayout(Selector, Src->SrcFvf, DstFvf))
        CopyFixed(Origin, Skew, *Layout, Src, StartVertex, Count, bIndexed);
    else
        CopyGeneric(Origin, Skew, DstFvf, Src, StartVertex, Count, bIndexed);
}

// The destination stride the converter Copy picks writes with: a fixed layout's own, else
// the format's.
inline unsigned int WriteStride(unsigned int DstFvf, const FRpVertexSrcDesc* Src)
{
    const unsigned int Selector =
        static_cast<unsigned int>(Src->SrcFvf == DstFvf) | (Src->Flags & 6u);
    const FRp8FixedLayout* const Layout =
        Selector == 3 || Selector == 6 ? nullptr : FixedLayout(Selector, Src->SrcFvf, DstFvf);
    return Layout != nullptr ? Layout->Stride : Rp8FvfStride(DstFvf);
}
} // namespace Rp8VertexCopyDetail

// FUN_06d1f040: Src's vertices StartVertex .. StartVertex + Count - 1 into Destination, a
// buffer of DstFvf vertices, at the same vertex numbers.
inline void Rp8CopyVertexData(void* Destination, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                              unsigned int StartVertex, unsigned int Count)
{
    Rp8VertexCopyDetail::Copy(static_cast<unsigned char*>(Destination), 0, DstFvf, Src, StartVertex,
                              Count);
}

// RP8's CopyVertices call (FUN_06d1228c): FUN_06d1f040 on Bits - FvfStride(DstFvf) *
// StartVertex, so the source's vertex StartVertex lands at Bits. The same, with the offset
// applied per vertex instead of to a pointer before Bits. Available is the bytes from Bits to
// the end of the buffer. Where a fixed layout's stride is not the format's (a texture set of
// another size), retail's writes start before Bits or run past the vertices locked; here a
// copy that would write outside Bits .. Bits + Available is refused (false) and writes
// nothing.
inline bool Rp8CopyVertexDataAt(void* Bits, std::size_t Available, unsigned int DstFvf,
                                const FRpVertexSrcDesc* Src, unsigned int StartVertex,
                                unsigned int Count)
{
    if (Rp8VertexCopyDetail::WritesPastRecord(DstFvf, Src))
        return false;
    const unsigned long long FormatStride = Rp8FvfStride(DstFvf);
    const unsigned long long WriteStride = Rp8VertexCopyDetail::WriteStride(DstFvf, Src);
    if (Count != 0)
    {
        // First and one-past-last byte written, relative to Bits.
        const long long First = static_cast<long long>(WriteStride * StartVertex) -
                                static_cast<long long>(FormatStride * StartVertex);
        const long long End = First + static_cast<long long>(WriteStride * Count);
        if (First < 0 || End < First || static_cast<unsigned long long>(End) > Available)
            return false;
    }
    const std::ptrdiff_t Skew = -static_cast<std::ptrdiff_t>(FormatStride * StartVertex);
    Rp8VertexCopyDetail::Copy(static_cast<unsigned char*>(Bits), Skew, DstFvf, Src, StartVertex,
                              Count);
    return true;
}
