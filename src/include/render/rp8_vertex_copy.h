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
//   Rp8FvfJumpIndex. Their entries are the generic converter, a fast path for one format
//   (the generic converter's bytes, with the format's own source strides assumed), or an empty
//   function: every pre-transformed (XYZRHW) format and
//   every format with a specular colour but no diffuse one copies nothing.
// - Otherwise the generic converter, sequential or indexed (each stream's IndexArray, one index
//   per vertex). Case 0 and 4 have fast paths for a few format pairs too, with the same bytes.
//
// The generic converter writes, per vertex at Dst + (StartVertex + n) * Rp8FvfStride(DstFvf):
// 12 position bytes; the normal's 12 when the destination has one (copied when the source has
// one too); the diffuse colour's 4 likewise; then each destination texture set, copied from the
// source set of the same index when there is one (the source set's ByteSize bytes), the cursor
// moving by the destination set's size. A destination specular colour, blend weights and the
// fourth position float of XYZRHW are not accounted for, as in retail. Bytes it does not copy
// are left as they were.
//
// The descriptor has two texture streams; retail reads a source's third and later sets past
// them (the descriptor's tail and beyond). Here a source set past the second copies nothing.

#include "render/IRenderPipeline.h"
#include "render/rp8_semantics.h"

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

// Whether retail's jump-table entry for DstFvf is the empty ConvNoOp (FUN_06d1f5fd). Both
// tables have it at the same entries: an odd index (XYZRHW), the rows whose colour bits are 2
// (specular without diffuse) and 0x20..0x2F, which JumpIndex never gives. Entry 0x40 (three
// or more texture sets, not pre-transformed) is the generic converter.
inline bool JumpTableCopiesNothing(unsigned int DstFvf)
{
    if (((DstFvf & 0xF00) >> 8) >= 3 && (DstFvf & 4) == 0)
        return false;
    const unsigned int Index = JumpIndex(DstFvf);
    return (Index & 1) != 0 || ((Index >> 2) & 3) == 2 || (Index & 0x30) == 0x20;
}

inline const FRpVertexStream* SourceTexSet(const FRpVertexSrcDesc* Src, unsigned int Index)
{
    return Index < 2 ? &Src->Tex[Index] : nullptr;
}

// FUN_06d2230f (Indexed false) and FUN_06d21450 (Indexed true).
inline void CopyGeneric(unsigned char* Dst, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                        unsigned int StartVertex, unsigned int Count, bool bIndexed)
{
    const unsigned int SrcFvf = Src->SrcFvf;
    const bool bSrcNormal = (SrcFvf & 0x10) != 0;
    const bool bSrcDiffuse = (SrcFvf & 0x40) != 0;
    const unsigned int SrcTexCount = (SrcFvf & 0xF00) >> 8;
    const bool bDstNormal = (DstFvf & 0x10) != 0;
    const bool bDstDiffuse = (DstFvf & 0x40) != 0;
    const unsigned int DstTexCount = (DstFvf & 0xF00) >> 8;
    const unsigned int DstStride = Rp8FvfStride(DstFvf);

    // The source vertex of stream S for the n-th vertex copied.
    auto const Element = [&](const FRpVertexStream& Stream, unsigned int N)
    {
        const std::size_t Vertex =
            bIndexed ? static_cast<std::size_t>(Stream.IndexArray[StartVertex + N])
                     : static_cast<std::size_t>(StartVertex) + N;
        return Stream.Base + Vertex * Stream.Stride;
    };

    for (unsigned int N = 0; N < Count; ++N)
    {
        unsigned char* Cursor = Dst + static_cast<std::size_t>(StartVertex + N) * DstStride;
        std::memcpy(Cursor, Element(Src->Position, N), 0xC);
        Cursor += 0xC;
        if (bDstNormal)
        {
            if (bSrcNormal)
                std::memcpy(Cursor, Element(Src->Normal, N), 0xC);
            Cursor += 0xC;
        }
        if (bDstDiffuse)
        {
            if (bSrcDiffuse)
                std::memcpy(Cursor, Element(Src->Diffuse, N), 4);
            Cursor += 4;
        }
        for (unsigned int Index = 0; Index < DstTexCount && Index < 8; ++Index)
        {
            const FRpVertexStream* const Set = SourceTexSet(Src, Index);
            if (Index < SrcTexCount && Set != nullptr)
                std::memcpy(Cursor, Element(*Set, N), Set->ByteSize);
            Cursor += TexelBytes(DstFvf >> (Index * 2 + 0x10));
        }
    }
}
} // namespace Rp8VertexCopyDetail

// FUN_06d1f040: Src's vertices StartVertex .. StartVertex + Count - 1 into Dst, a buffer of
// DstFvf vertices (the same vertex numbers).
inline void Rp8CopyVertexData(void* Destination, unsigned int DstFvf, const FRpVertexSrcDesc* Src,
                              unsigned int StartVertex, unsigned int Count)
{
    using namespace Rp8VertexCopyDetail;
    unsigned char* const Dst = static_cast<unsigned char*>(Destination);
    const unsigned int Selector =
        static_cast<unsigned int>(Src->SrcFvf == DstFvf) | (Src->Flags & 6u);
    switch (Selector)
    {
    case 0:
    case 2:
        CopyGeneric(Dst, DstFvf, Src, StartVertex, Count, false);
        break;
    case 1:
        if (!JumpTableCopiesNothing(DstFvf))
            CopyGeneric(Dst, DstFvf, Src, StartVertex, Count, false);
        break;
    case 3:
    {
        const unsigned int Stride = Rp8FvfStride(DstFvf);
        std::memcpy(Dst + static_cast<std::size_t>(StartVertex) * Stride,
                    Src->Position.Base +
                        static_cast<std::size_t>(StartVertex) * Src->Position.Stride,
                    static_cast<std::size_t>(Stride) * Count);
        break;
    }
    case 4:
    case 7:
        CopyGeneric(Dst, DstFvf, Src, StartVertex, Count, true);
        break;
    case 5:
        if (!JumpTableCopiesNothing(DstFvf))
            CopyGeneric(Dst, DstFvf, Src, StartVertex, Count, true);
        break;
    default: // 6
        break;
    }
}
