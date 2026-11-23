// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): GDI on macOS and Linux, the calls src/include/posix/wingdi.h declares. Not part of
// retail; built only off Windows, where every module on the stand-in <windows.h> links it
// (cmake/Freelancer.cmake), as MSVC links gdi32.lib by default.
//
// What the game asks of GDI is text in memory bitmaps: a font selected into a memory DC with a
// compatible bitmap, glyphs drawn with ExtTextOutW and the pixels read back with GetDIBits
// (the client's glyph atlas and rich text, Common's text renderer), plus the metrics. So:
//
// - Objects are C++ objects behind the handles, kept in a registry (a handle is checked before
//   use), one per process. DCs, 32-bit bitmaps (0x00RRGGBB, top row first), solid brushes,
//   rectangular regions, fonts. Stock objects are never freed.
// - A font is a TrueType face (stb_truetype, vendor/) found by the LOGFONT's face name among the
//   font files of the game's folders and the system's (FL_GDI32_FONT_PATH adds folders), with
//   GDI's sizes: a negative lfHeight is the em height, a positive one the cell height
//   (usWinAscent + usWinDescent), and the TEXTMETRIC follows the OS/2 and hhea tables as
//   Windows reads them. A face that is not installed falls back to a lookalike (Agency FB to
//   Arial Narrow, FixedSys to a monospaced face, then Arial or any face); a bold request without
//   a bold face is drawn one pixel wider, as GDI emboldens.
// - ExtTextOutW draws antialiased glyphs in the text colour, with GDI's alignment (TA_TOP /
//   TA_BASELINE / TA_BOTTOM, TA_LEFT / TA_CENTER / TA_RIGHT), background mode, ETO_OPAQUE /
//   ETO_CLIPPED and the DC's clip region, and the caller's advances (Dx) when given.
// - GetDIBits converts to 32, 24 and 16 bits per pixel (5-5-5, or the masks of BI_BITFIELDS),
//   bottom-up or top-down as the header asks.
//
// It uses nothing of the platform layer's shared state, as user32 does not (dacom links both).

#include <windows.h>

// stb_truetype's implementation, private to this file (STBTT_STATIC): its helpers this file does
// not call are unused here. Font files are outside input: their structure is checked before
// stb_truetype reads them (ValidFont, GlyphReadable), and what is left to stb_truetype's asserts
// (a CFF font's indexes) it reads through bounded buffers, so a bad file draws nothing rather than
// stopping the program.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#define STBTT_assert(Condition) ((void)0)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "vendor/stb_truetype.h"
#pragma GCC diagnostic pop

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <vector>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
// ---------------------------------------------------------------------------------------------
// Objects
// ---------------------------------------------------------------------------------------------

enum class EKind
{
    Dc,
    Bitmap,
    Brush,
    Region,
    Font,
};

struct FObject
{
    explicit FObject(EKind InKind) : Kind(InKind) {}
    virtual ~FObject() = default;
    EKind Kind;
    bool bStock = false;
};

struct FBitmap : FObject
{
    FBitmap(int InWidth, int InHeight)
        : FObject(EKind::Bitmap), Width(InWidth), Height(InHeight),
          Pixels(static_cast<std::size_t>(InWidth) * static_cast<std::size_t>(InHeight), 0)
    {
    }
    int Width;
    int Height;
    std::vector<std::uint32_t> Pixels; // 0x00RRGGBB, top row first
};

struct FBrush : FObject
{
    FBrush(COLORREF InColor, bool bInNull) : FObject(EKind::Brush), Color(InColor), bNull(bInNull)
    {
    }
    COLORREF Color;
    bool bNull;
};

struct FRegion : FObject
{
    explicit FRegion(RECT InRect) : FObject(EKind::Region), Rect(InRect) {}
    RECT Rect;
};

// A font file in memory, one per file and face index. Its bytes are a copy the face owns, so what
// was checked stays as it was whatever happens to the file afterwards.
struct FFace
{
    std::vector<unsigned char> Bytes;
    stbtt_fontinfo Info = {};
    int UnitsPerEm = 2048;
    int WinAscent = 0;
    int WinDescent = 0;
    int HheaAscent = 0;
    int HheaDescent = 0;
    int HheaLineGap = 0;
    int AdvanceMax = 0;
    int AverageWidth = 0;
    bool bBold = false;
    bool bItalic = false;
    bool bFixedPitch = false;
    std::uint32_t Vdmx = 0; // the VDMX table's offset, 0 without one
    std::uint32_t VdmxLength = 0;
    std::uint32_t GlyfLength = 0;
    std::string Family;               // nameID 1, as GetTextFace answers it
    std::vector<std::int8_t> Readable; // per glyph: 0 unchecked, 1 readable, -1 not (TrueType)
};

struct FFont : FObject
{
    FFont() : FObject(EKind::Font) {}
    LOGFONTA Log = {};
    std::shared_ptr<FFace> Face;
    float Scale = 1.f;  // design units to pixels, vertically
    float ScaleX = 1.f; // and horizontally (lfWidth)
    bool bEmbolden = false;
    LONG Ascent = 0;
    LONG Descent = 0;
    LONG Height = 0;
    LONG InternalLeading = 0;
    LONG ExternalLeading = 0;
    LONG AveCharWidth = 0;
    LONG MaxCharWidth = 0;
};

struct FDc : FObject
{
    FDc() : FObject(EKind::Dc) {}
    FBitmap* Bitmap = nullptr;
    FFont* Font = nullptr;
    FBrush* Brush = nullptr;
    COLORREF TextColor = 0x000000;
    COLORREF BkColor = 0xFFFFFF;
    int BkMode = OPAQUE;
    UINT Align = TA_LEFT | TA_TOP;
    bool bClip = false;
    RECT Clip = {};
    bool bScreen = false; // GetDC's: no bitmap, and none may be selected
};

struct FState
{
    std::recursive_mutex Lock;
    std::set<FObject*> Objects;
    // The stock objects, and a memory DC's 1x1 bitmap before one is selected.
    FBrush* StockBrushes[6] = {};
    FFont* SystemFont = nullptr;
    FBitmap* DefaultBitmap = nullptr;
    // The font catalogue: lower-case family name to its faces (file, index, style).
    struct FCatalogEntry
    {
        std::string Path;
        int Index;
        bool bBold;
        bool bItalic;
    };
    bool bCatalogued = false;
    std::multimap<std::string, FCatalogEntry> Catalog;
    std::map<std::pair<std::string, int>, std::weak_ptr<FFace>> Faces;
};

FState& State()
{
    // Never destroyed: other modules' static destructors may still delete objects.
    static FState* const Instance = new FState();
    return *Instance;
}

template <class T>
T* Register(T* Object)
{
    State().Objects.insert(Object);
    return Object;
}

// The object behind a handle, of the kind asked for, or nullptr; the caller holds the lock.
template <class T>
T* As(const void* Handle, EKind Kind)
{
    FObject* const Object = static_cast<FObject*>(const_cast<void*>(Handle));
    if (Object == nullptr || State().Objects.count(Object) == 0 || Object->Kind != Kind)
        return nullptr;
    return static_cast<T*>(Object);
}

std::uint32_t PixelOf(COLORREF Color)
{
    return ((Color & 0xFFu) << 16) | (Color & 0xFF00u) | ((Color >> 16) & 0xFFu);
}

// ---------------------------------------------------------------------------------------------
// Fonts
// ---------------------------------------------------------------------------------------------

std::string Lower(std::string Text)
{
    for (char& Character : Text)
        Character = static_cast<char>(std::tolower(static_cast<unsigned char>(Character)));
    return Text;
}

int ReadU16(const unsigned char* Data)
{
    return (Data[0] << 8) | Data[1];
}

int ReadS16(const unsigned char* Data)
{
    return static_cast<std::int16_t>(ReadU16(Data));
}

std::uint32_t ReadU32(const unsigned char* Data)
{
    return (static_cast<std::uint32_t>(Data[0]) << 24) |
           (static_cast<std::uint32_t>(Data[1]) << 16) |
           (static_cast<std::uint32_t>(Data[2]) << 8) | Data[3];
}

// ---------------------------------------------------------------------------------------------
// Font files: what stb_truetype reads, checked against the file's size first. stb_truetype
// takes only a pointer, so a table, record or outline that runs past the file would be read
// past the mapping.
// ---------------------------------------------------------------------------------------------

// A table's place in the file, from the font's directory; Offset 0 when the font has none.
struct FTable
{
    std::uint32_t Offset = 0;
    std::uint32_t Length = 0;
};

// The first entry of a tag, as stbtt__find_table takes it (the directory is checked).
FTable FindTable(const unsigned char* Data, std::uint32_t FontStart, const char* Tag)
{
    int const Count = ReadU16(Data + FontStart + 4);
    for (int Index = 0; Index < Count; ++Index)
    {
        const unsigned char* const Record = Data + FontStart + 12 + Index * 16;
        if (std::memcmp(Record, Tag, 4) == 0)
            return FTable{ReadU32(Record + 8), ReadU32(Record + 12)};
    }
    return FTable{};
}

// The number of faces in a font file (1, or a collection's count), 0 when its header is not
// one stb_truetype reads or runs past the file.
int FacesInFile(const unsigned char* Data, std::size_t Size)
{
    if (Size < 12)
        return 0;
    if (std::memcmp(Data, "ttcf", 4) != 0)
        return stbtt_GetNumberOfFonts(Data) == 1 ? 1 : 0;
    std::uint32_t const Version = ReadU32(Data + 4);
    if (Version != 0x00010000 && Version != 0x00020000)
        return 0;
    auto const Count = static_cast<std::int32_t>(ReadU32(Data + 8));
    if (Count <= 0 || 12 + 4 * static_cast<std::uint64_t>(Count) > Size)
        return 0;
    return Count;
}

// Whether the cmap subtable stb_truetype chose (Map) can be read for any character: its format
// one stb_truetype maps, its declared length inside the cmap table, and every array a lookup
// reads (format 4's binary search and glyph arrays too) inside that length. Format 4's 16-bit
// length wraps for a subtable of 64 KiB or more: it is taken as the length plus the multiple of
// 65536 the arrays need, when that much of the cmap table follows.
bool ValidCharacterMap(const unsigned char* Data, FTable Cmap, std::uint32_t Map)
{
    std::uint64_t const End = static_cast<std::uint64_t>(Cmap.Offset) + Cmap.Length;
    if (Map < Cmap.Offset || static_cast<std::uint64_t>(Map) + 8 > End)
        return false;
    std::uint64_t const Available = End - Map;
    const unsigned char* const Table = Data + Map;
    std::uint64_t const Length = ReadU16(Table + 2);
    switch (ReadU16(Table))
    {
    case 0:
        return Length >= 6 && Length <= Available;
    case 6:
        return Length >= 10 && Length <= Available &&
               10 + 2 * static_cast<std::uint64_t>(ReadU16(Table + 8)) <= Length;
    case 12:
    case 13:
    {
        if (Available < 16)
            return false;
        std::uint64_t const LongLength = ReadU32(Table + 4);
        return LongLength <= Available &&
               16 + 12 * static_cast<std::uint64_t>(ReadU32(Table + 12)) <= LongLength;
    }
    case 4:
    {
        if (Available < 14)
            return false;
        int const Segments = ReadU16(Table + 6) >> 1;
        std::uint64_t Required = 16 + 8 * static_cast<std::uint64_t>(Segments);
        if (Segments == 0 || Required > Available)
            return false;
        // The furthest segment stbtt_FindGlyphIndex's binary search can land on, from the
        // header's searchRange / entrySelector / rangeShift as it uses them.
        std::int64_t Search = 2 * static_cast<std::int64_t>(ReadU16(Table + 12) >> 1) - 2;
        int Range = ReadU16(Table + 8) >> 1;
        for (int Step = ReadU16(Table + 10); Step > 0 && Range > 0; --Step)
        {
            Range >>= 1;
            Search += 2 * Range;
        }
        if ((Search + 2) / 2 >= Segments)
            return false;
        // A segment with an idRangeOffset reads its glyph array, from that offset's own place.
        for (int Segment = 0; Segment < Segments; ++Segment)
        {
            std::uint64_t const RangeOffsetAt =
                16 + 6 * static_cast<std::uint64_t>(Segments) + 2 * Segment;
            int const Last = ReadU16(Table + 14 + 2 * Segment);
            int const First = ReadU16(Table + 16 + 2 * Segments + 2 * Segment);
            int const RangeOffset = ReadU16(Table + RangeOffsetAt);
            if (RangeOffset != 0 && First <= Last)
                Required = std::max(Required, RangeOffsetAt + RangeOffset +
                                                  2 * static_cast<std::uint64_t>(Last - First) + 2);
        }
        std::uint64_t Bound = Length;
        while (Bound < Required && Bound + 0x10000 <= Available)
            Bound += 0x10000;
        return Required <= Bound && Bound <= Available;
    }
    default:
        return false; // format 2 and others: stb_truetype maps nothing with them
    }
}

// Whether stb_truetype may read the face at FontStart: the directory and every table inside the
// file, the tables it and this file read long enough, the name records and cmap records inside
// their tables, hmtx and loca as long as the glyph count needs.
bool ValidFont(const unsigned char* Data, std::size_t Size, std::uint32_t FontStart)
{
    if (static_cast<std::uint64_t>(FontStart) + 12 > Size)
        return false;
    int const Tables = ReadU16(Data + FontStart + 4);
    if (FontStart + 12 + 16 * static_cast<std::uint64_t>(Tables) > Size)
        return false;
    for (int Index = 0; Index < Tables; ++Index)
    {
        const unsigned char* const Record = Data + FontStart + 12 + Index * 16;
        if (static_cast<std::uint64_t>(ReadU32(Record + 8)) + ReadU32(Record + 12) > Size)
            return false;
    }
    FTable const Head = FindTable(Data, FontStart, "head");
    FTable const Hhea = FindTable(Data, FontStart, "hhea");
    FTable const Maxp = FindTable(Data, FontStart, "maxp");
    FTable const Hmtx = FindTable(Data, FontStart, "hmtx");
    FTable const Cmap = FindTable(Data, FontStart, "cmap");
    if (Head.Offset == 0 || Head.Length < 54 || Hhea.Offset == 0 || Hhea.Length < 36 ||
        Maxp.Offset == 0 || Maxp.Length < 6 || Hmtx.Offset == 0 || Cmap.Offset == 0 ||
        Cmap.Length < 4)
        return false;
    int const Glyphs = ReadU16(Data + Maxp.Offset + 4);
    int const LongMetrics = ReadU16(Data + Hhea.Offset + 34);
    if (LongMetrics == 0 ||
        4 * static_cast<std::uint64_t>(LongMetrics) + 2 * std::max(0, Glyphs - LongMetrics) >
            Hmtx.Length)
        return false;
    FTable const Glyf = FindTable(Data, FontStart, "glyf");
    if (Glyf.Offset != 0)
    {
        FTable const Loca = FindTable(Data, FontStart, "loca");
        int const LocaFormat = ReadU16(Data + Head.Offset + 50);
        if (Loca.Offset == 0 || LocaFormat > 1 ||
            (static_cast<std::uint64_t>(Glyphs) + 1) * (LocaFormat == 0 ? 2 : 4) > Loca.Length)
            return false;
    }
    int const Encodings = ReadU16(Data + Cmap.Offset + 2);
    if (4 + 8 * static_cast<std::uint64_t>(Encodings) > Cmap.Length)
        return false;
    for (int Index = 0; Index < Encodings; ++Index)
        if (static_cast<std::uint64_t>(ReadU32(Data + Cmap.Offset + 4 + 8 * Index + 4)) + 2 >
            Cmap.Length)
            return false;
    if (FTable const Name = FindTable(Data, FontStart, "name"); Name.Offset != 0)
    {
        if (Name.Length < 6)
            return false;
        int const Records = ReadU16(Data + Name.Offset + 2);
        std::uint64_t const Strings = ReadU16(Data + Name.Offset + 4);
        if (6 + 12 * static_cast<std::uint64_t>(Records) > Name.Length)
            return false;
        for (int Index = 0; Index < Records; ++Index)
        {
            const unsigned char* const Record = Data + Name.Offset + 6 + 12 * Index;
            if (Strings + ReadU16(Record + 10) + ReadU16(Record + 8) > Name.Length)
                return false;
        }
    }
    return true;
}

// Opens the face at Index of a font file for stb_truetype, its structure checked first; false
// for a face it cannot read safely.
bool InitFace(stbtt_fontinfo& Info, const unsigned char* Data, std::size_t Size, int Index)
{
    if (Index >= FacesInFile(Data, Size))
        return false;
    int const Offset = stbtt_GetFontOffsetForIndex(Data, Index);
    return Offset >= 0 && ValidFont(Data, Size, static_cast<std::uint32_t>(Offset)) &&
           stbtt_InitFont(&Info, Data, Offset) &&
           ValidCharacterMap(Data, FindTable(Data, static_cast<std::uint32_t>(Offset), "cmap"),
                             static_cast<std::uint32_t>(Info.index_map));
}

// A character's glyph: the cmap may name a glyph past the face's count, which reads as glyph 0.
int GlyphOf(const FFace& Face, std::uint32_t Character)
{
    int const Glyph = stbtt_FindGlyphIndex(&Face.Info, static_cast<int>(Character));
    return Glyph > 0 && Glyph < Face.Info.numGlyphs ? Glyph : 0;
}

// What one glyph's outline may cost stb_truetype, its components included: each component is
// read and expanded again wherever it is used, under the GDI lock. Generous for real fonts
// (composites are a few components of a few hundred points).
struct FOutlineBudget
{
    int Components = 256; // glyphs visited
    std::int64_t Vertices = 1 << 16; // vertices stb_truetype allocates for them
};

// Whether a TrueType outline lies inside its loca span, walked as stbtt__GetGlyphShapeTT reads
// it: a simple glyph's end points (rising), instructions, flags and coordinates; a composite's
// records, each with an x/y offset (stb_truetype does not read point matching), and their
// glyphs, to a depth of 8 (which also ends a composite that contains itself) and within Budget.
bool CheckOutline(FFace& Face, int Glyph, int Depth, FOutlineBudget& Budget)
{
    if (--Budget.Components < 0)
        return false;
    const stbtt_fontinfo& Info = Face.Info;
    const unsigned char* const Loca = Info.data + Info.loca;
    std::uint64_t const From =
        Info.indexToLocFormat == 0 ? 2u * ReadU16(Loca + Glyph * 2) : ReadU32(Loca + Glyph * 4);
    std::uint64_t const To = Info.indexToLocFormat == 0 ? 2u * ReadU16(Loca + Glyph * 2 + 2)
                                                        : ReadU32(Loca + Glyph * 4 + 4);
    if (From == To)
        return true; // an empty glyph, which stb_truetype does not read
    if (From > To || To > Face.GlyfLength || To - From < 10)
        return false;
    std::uint64_t const Span = To - From;
    const unsigned char* const Outline = Info.data + Info.glyf + From;
    int const Contours = ReadS16(Outline);
    if (Contours > 0)
    {
        std::uint64_t Position = 12 + 2 * static_cast<std::uint64_t>(Contours);
        if (Position > Span)
            return false;
        int Last = -1;
        for (int Contour = 0; Contour < Contours; ++Contour)
        {
            int const End = ReadU16(Outline + 10 + 2 * Contour);
            if (End <= Last)
                return false;
            Last = End;
        }
        // stbtt__GetGlyphShapeTT allocates points + 2 per contour.
        Budget.Vertices -= Last + 1 + 2 * static_cast<std::int64_t>(Contours);
        if (Budget.Vertices < 0)
            return false;
        Position += ReadU16(Outline + 10 + 2 * Contours);
        std::uint64_t Coordinates = 0;
        int Flags = 0;
        int Repeat = 0;
        for (int Point = 0; Point <= Last; ++Point)
        {
            if (Repeat == 0)
            {
                if (Position >= Span)
                    return false;
                Flags = Outline[Position++];
                if ((Flags & 8) != 0)
                {
                    if (Position >= Span)
                        return false;
                    Repeat = Outline[Position++];
                }
            }
            else
                --Repeat;
            Coordinates += (Flags & 2) != 0 ? 1 : (Flags & 16) != 0 ? 0 : 2;
            Coordinates += (Flags & 4) != 0 ? 1 : (Flags & 32) != 0 ? 0 : 2;
        }
        return Position + Coordinates <= Span;
    }
    if (Contours < 0)
    {
        std::uint64_t Position = 10;
        for (bool bMore = true; bMore;)
        {
            if (Position + 4 > Span)
                return false;
            int const Flags = ReadU16(Outline + Position);
            int const Component = ReadU16(Outline + Position + 2);
            if ((Flags & 2) == 0)
                return false;
            Position += 4 + ((Flags & 1) != 0 ? 4 : 2);
            if ((Flags & 8) != 0)
                Position += 2;
            else if ((Flags & 0x40) != 0)
                Position += 4;
            else if ((Flags & 0x80) != 0)
                Position += 8;
            if (Position > Span)
                return false;
            // stb_truetype draws nothing for a component past the glyph count.
            if (Component < Info.numGlyphs &&
                (Depth >= 8 || !CheckOutline(Face, Component, Depth + 1, Budget)))
                return false;
            bMore = (Flags & 0x20) != 0;
        }
    }
    return true;
}

// Whether stb_truetype may read a glyph's outline: a CFF font's it reads through bounded buffers
// (its subroutines nest at most 10 deep), a TrueType one's is checked once (CheckOutline).
bool GlyphReadable(FFace& Face, int Glyph)
{
    if (Glyph < 0 || Glyph >= Face.Info.numGlyphs)
        return false;
    if (Face.Info.cff.size != 0)
        return true;
    std::int8_t& Known = Face.Readable[static_cast<std::size_t>(Glyph)];
    if (Known == 0)
    {
        FOutlineBudget Budget;
        Known = CheckOutline(Face, Glyph, 0, Budget) ? 1 : -1;
    }
    return Known > 0;
}

// A name-table string (UTF-16BE for the Windows platform, Mac Roman for the Macintosh one) as
// UTF-8 (Latin-1 range only: face names are ASCII).
std::string NameString(const stbtt_fontinfo& Info, int NameId)
{
    int Length = 0;
    const char* Text =
        stbtt_GetFontNameString(&Info, &Length, STBTT_PLATFORM_ID_MICROSOFT,
                                STBTT_MS_EID_UNICODE_BMP, STBTT_MS_LANG_ENGLISH, NameId);
    std::string Out;
    if (Text != nullptr)
    {
        for (int Index = 0; Index + 1 < Length; Index += 2)
            if (Text[Index] == 0)
                Out += Text[Index + 1];
        return Out;
    }
    Text = stbtt_GetFontNameString(&Info, &Length, STBTT_PLATFORM_ID_MAC, STBTT_MAC_EID_ROMAN,
                                   STBTT_MAC_LANG_ENGLISH, NameId);
    if (Text != nullptr)
        Out.assign(Text, static_cast<std::size_t>(Length));
    return Out;
}

struct FMapped
{
    void* Data = nullptr;
    std::size_t Size = 0;
};

// A regular file mapped whole. Opened without blocking (a FIFO named like a font must not hold
// the lock), and checked as a regular file on the descriptor, which is what gets mapped.
FMapped MapFile(const std::string& Path)
{
    FMapped Out;
    int const File = open(Path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (File < 0)
        return Out;
    struct stat Info;
    if (fstat(File, &Info) == 0 && S_ISREG(Info.st_mode) && Info.st_size > 0)
    {
        void* const Data =
            mmap(nullptr, static_cast<std::size_t>(Info.st_size), PROT_READ, MAP_PRIVATE, File, 0);
        if (Data != MAP_FAILED)
        {
            Out.Data = Data;
            Out.Size = static_cast<std::size_t>(Info.st_size);
        }
    }
    close(File);
    return Out;
}

// A regular file read whole into memory, opened as MapFile opens it; false when it cannot be read
// or does not read to the size it had when opened.
bool ReadWhole(const std::string& Path, std::vector<unsigned char>& Out)
{
    int const File = open(Path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (File < 0)
        return false;
    struct stat Info;
    bool bRead = false;
    if (fstat(File, &Info) == 0 && S_ISREG(Info.st_mode) && Info.st_size > 0)
    {
        Out.resize(static_cast<std::size_t>(Info.st_size));
        std::size_t Done = 0;
        while (Done < Out.size())
        {
            ssize_t const Got = read(File, Out.data() + Done, Out.size() - Done);
            if (Got < 0 && errno == EINTR)
                continue;
            if (Got <= 0)
                break;
            Done += static_cast<std::size_t>(Got);
        }
        bRead = Done == Out.size();
    }
    close(File);
    return bRead;
}

bool HasFontExtension(const std::string& Name)
{
    std::string const Lowered = Lower(Name);
    for (const char* Extension : {".ttf", ".otf", ".ttc"})
    {
        std::size_t const Length = std::strlen(Extension);
        if (Lowered.size() > Length &&
            Lowered.compare(Lowered.size() - Length, Length, Extension) == 0)
            return true;
    }
    return false;
}

// Adds a font file's faces to the catalogue, under their family names (the typographic
// family, nameID 16, and the legacy one, 1, which GDI matches: "Arial Narrow").
void CatalogFile(const std::string& Path)
{
    FMapped const File = MapFile(Path);
    if (File.Data == nullptr)
        return;
    const unsigned char* const Data = static_cast<const unsigned char*>(File.Data);
    int const Count = FacesInFile(Data, File.Size);
    for (int Index = 0; Index < Count; ++Index)
    {
        stbtt_fontinfo Info;
        if (!InitFace(Info, Data, File.Size, Index))
            continue;
        std::string const Style = Lower(NameString(Info, 2));
        bool const bBold = Style.find("bold") != std::string::npos ||
                           Style.find("black") != std::string::npos ||
                           Style.find("heavy") != std::string::npos;
        bool const bItalic =
            Style.find("italic") != std::string::npos || Style.find("oblique") != std::string::npos;
        std::set<std::string> Families;
        for (int NameId : {1, 16})
        {
            std::string const Family = Lower(NameString(Info, NameId));
            if (!Family.empty())
                Families.insert(Family);
        }
        for (const std::string& Family : Families)
            State().Catalog.emplace(Family, FState::FCatalogEntry{Path, Index, bBold, bItalic});
    }
    munmap(File.Data, File.Size);
}

void CatalogFolder(const std::string& Folder, int Depth)
{
    DIR* const Directory = opendir(Folder.c_str());
    if (Directory == nullptr)
        return;
    while (dirent* const Entry = readdir(Directory))
    {
        std::string const Name = Entry->d_name;
        if (Name == "." || Name == "..")
            continue;
        std::string const Path = Folder + "/" + Name;
        struct stat Info;
        if (stat(Path.c_str(), &Info) != 0)
            continue;
        if (S_ISDIR(Info.st_mode))
        {
            if (Depth > 0)
                CatalogFolder(Path, Depth - 1);
        }
        else if (S_ISREG(Info.st_mode) && HasFontExtension(Name))
            CatalogFile(Path);
    }
    closedir(Directory);
}

// A folder's child named Name in any case (DATA\FONTS may be Data/Fonts on a case-sensitive
// volume): the exact name when it exists, else the first entry that matches it.
std::string ChildNoCase(const std::string& Folder, const std::string& Name)
{
    std::string const Exact = Folder + "/" + Name;
    struct stat Info;
    if (stat(Exact.c_str(), &Info) == 0)
        return Exact;
    std::string Found = Exact;
    if (DIR* const Directory = opendir(Folder.c_str()))
    {
        std::string const Wanted = Lower(Name);
        while (dirent* const Entry = readdir(Directory))
            if (Lower(Entry->d_name) == Wanted)
            {
                Found = Folder + "/" + Entry->d_name;
                break;
            }
        closedir(Directory);
    }
    return Found;
}

// The folders searched, in order: FL_GDI32_FONT_PATH (':'-separated), the program's folder and
// the game's DATA\FONTS beside it, then the system's and the user's font folders.
void BuildCatalog()
{
    FState& S = State();
    if (S.bCatalogued)
        return;
    S.bCatalogued = true;
    std::vector<std::string> Folders;
    if (const char* const Extra = std::getenv("FL_GDI32_FONT_PATH"))
    {
        std::string const List = Extra;
        std::size_t Start = 0;
        while (Start <= List.size())
        {
            std::size_t End = List.find(':', Start);
            if (End == std::string::npos)
                End = List.size();
            if (End > Start)
                Folders.push_back(List.substr(Start, End - Start));
            Start = End + 1;
        }
    }
    Dl_info Self;
    if (dladdr(reinterpret_cast<const void*>(&BuildCatalog), &Self) != 0 &&
        Self.dli_fname != nullptr)
    {
        std::string Folder = Self.dli_fname;
        Folder.erase(Folder.find_last_of('/'));
        Folders.push_back(Folder);
        Folders.push_back(ChildNoCase(ChildNoCase(Folder + "/..", "DATA"), "FONTS"));
    }
    std::string const Home = std::getenv("HOME") != nullptr ? std::getenv("HOME") : "";
#if defined(__APPLE__)
    for (const char* Folder : {"/System/Library/Fonts", "/Library/Fonts"})
        Folders.push_back(Folder);
    if (!Home.empty())
        Folders.push_back(Home + "/Library/Fonts");
#else
    for (const char* Folder : {"/usr/share/fonts", "/usr/local/share/fonts"})
        Folders.push_back(Folder);
    if (!Home.empty())
    {
        Folders.push_back(Home + "/.local/share/fonts");
        Folders.push_back(Home + "/.fonts");
    }
#endif
    for (const std::string& Folder : Folders)
        CatalogFolder(Folder, 3);
}

std::shared_ptr<FFace> OpenFace(const std::string& Path, int Index)
{
    FState& S = State();
    auto const Key = std::make_pair(Path, Index);
    if (std::shared_ptr<FFace> const Cached = S.Faces[Key].lock())
        return Cached;
    // Read, not mapped: a file truncated or rewritten later cannot change (or fault) what the
    // face reads. The catalogue scan maps files, for the moment it reads their names.
    auto Face = std::make_shared<FFace>();
    if (!ReadWhole(Path, Face->Bytes))
        return nullptr;
    const unsigned char* const Data = Face->Bytes.data();
    if (!InitFace(Face->Info, Data, Face->Bytes.size(), Index))
        return nullptr;
    stbtt_fontinfo& Info = Face->Info;
    auto const Start = static_cast<std::uint32_t>(Info.fontstart);
    // ValidFont saw head and hhea long enough; OS/2 and post are read only when they are.
    Face->UnitsPerEm = std::max(1, ReadU16(Info.data + FindTable(Data, Start, "head").Offset + 18));
    if (std::uint32_t const Hhea = FindTable(Data, Start, "hhea").Offset)
    {
        Face->HheaAscent = ReadS16(Info.data + Hhea + 4);
        Face->HheaDescent = -ReadS16(Info.data + Hhea + 6);
        Face->HheaLineGap = ReadS16(Info.data + Hhea + 8);
        Face->AdvanceMax = ReadU16(Info.data + Hhea + 10);
    }
    Face->WinAscent = Face->HheaAscent;
    Face->WinDescent = Face->HheaDescent;
    if (FTable const Os2Table = FindTable(Data, Start, "OS/2"); Os2Table.Length >= 78)
    {
        std::uint32_t const Os2 = Os2Table.Offset;
        Face->AverageWidth = ReadS16(Info.data + Os2 + 2);
        int const WeightClass = ReadU16(Info.data + Os2 + 4);
        int const Selection = ReadU16(Info.data + Os2 + 62);
        Face->WinAscent = ReadU16(Info.data + Os2 + 74);
        Face->WinDescent = ReadU16(Info.data + Os2 + 76);
        Face->bBold = WeightClass >= 600 || (Selection & 0x20) != 0;
        Face->bItalic = (Selection & 0x01) != 0;
    }
    FTable const Vdmx = FindTable(Data, Start, "VDMX");
    Face->Vdmx = Vdmx.Offset;
    Face->VdmxLength = Vdmx.Length;
    Face->GlyfLength = FindTable(Data, Start, "glyf").Length;
    if (FTable const Post = FindTable(Data, Start, "post"); Post.Length >= 16)
        Face->bFixedPitch = ReadU16(Info.data + Post.Offset + 12) != 0 ||
                            ReadU16(Info.data + Post.Offset + 14) != 0;
    Face->Family = NameString(Info, 1);
    Face->Readable.assign(static_cast<std::size_t>(Info.numGlyphs), 0);
    if (Face->AverageWidth <= 0)
    {
        int Advance = 0;
        int Bearing = 0;
        stbtt_GetGlyphHMetrics(&Info, GlyphOf(*Face, 'x'), &Advance, &Bearing);
        Face->AverageWidth = Advance;
    }
    S.Faces[Key] = Face;
    return Face;
}

// A family's faces, best first for the style asked: the exact style, then the weight asked;
// faces that match as well keep the catalogue's order (the folders' order).
void FindInFamily(const std::string& Family, bool bBold, bool bItalic,
                  std::vector<const FState::FCatalogEntry*>& Out)
{
    auto const Range = State().Catalog.equal_range(Family);
    std::vector<std::pair<int, const FState::FCatalogEntry*>> Ranked;
    for (auto It = Range.first; It != Range.second; ++It)
        Ranked.emplace_back((It->second.bBold == bBold ? 2 : 0) +
                                (It->second.bItalic == bItalic ? 1 : 0),
                            &It->second);
    std::stable_sort(Ranked.begin(), Ranked.end(),
                     [](const auto& A, const auto& B) { return A.first > B.first; });
    for (const auto& Entry : Ranked)
        Out.push_back(Entry.second);
}

// The faces to try for a LOGFONT, best first: the family asked, its lookalikes, the common
// fallbacks, then every face catalogued.
std::vector<const FState::FCatalogEntry*> FindFaces(const char* FaceName, bool bBold, bool bItalic)
{
    BuildCatalog();
    std::vector<std::string> Families;
    std::string const Wanted = Lower(FaceName != nullptr ? FaceName : "");
    if (!Wanted.empty())
        Families.push_back(Wanted);
    // Lookalikes of the faces Freelancer names that a system may not have.
    if (Wanted == "agency fb")
        for (const char* Alike : {"arial narrow", "helvetica neue", "liberation sans narrow"})
            Families.push_back(Alike);
    if (Wanted == "fixedsys" || Wanted == "courier" || Wanted == "terminal")
        for (const char* Alike : {"courier new", "menlo", "dejavu sans mono", "liberation mono"})
            Families.push_back(Alike);
    if (Wanted == "arial unicode ms")
        Families.push_back("arial unicode");
    for (const char* Fallback :
         {"arial", "helvetica", "liberation sans", "dejavu sans", "noto sans", "freesans"})
        Families.push_back(Fallback);
    std::vector<const FState::FCatalogEntry*> Out;
    for (const std::string& Family : Families)
        FindInFamily(Family, bBold, bItalic, Out);
    for (const auto& Entry : State().Catalog)
        Out.push_back(&Entry.second);
    return Out;
}

// The VDMX table's yMax / yMin (pixels) for a pixel height, as GDI takes a face's ascent and
// descent from it when the face has one: by em height (bCell false) or the tallest entry whose
// yMax - yMin fits a cell height, whose em height (Ppem) is then the font's. A cell taller than
// the group's last entry is past what the table covers: false, and the sizes are scaled. Uses the
// 1:1 aspect ratio group (or the "any" one); false for a table that runs past its length.
bool VdmxExtent(const FFace& Face, int Pixels, bool bCell, int* Ascent, int* Descent, int* Ppem)
{
    if (Face.Vdmx == 0 || Face.VdmxLength < 6)
        return false;
    std::uint64_t const Length = Face.VdmxLength;
    const unsigned char* const Table = Face.Info.data + Face.Vdmx;
    int const Ratios = ReadU16(Table + 4);
    if (6 + 6 * static_cast<std::uint64_t>(Ratios) > Length)
        return false;
    for (int Ratio = 0; Ratio < Ratios; ++Ratio)
    {
        const unsigned char* const Range = Table + 6 + Ratio * 4;
        int const XRatio = Range[1];
        int const YStart = Range[2];
        int const YEnd = Range[3];
        if (!(XRatio == 0 || (YStart <= XRatio && XRatio <= YEnd)))
            continue;
        std::uint64_t const GroupAt = ReadU16(Table + 6 + Ratios * 4 + Ratio * 2);
        if (GroupAt + 4 > Length)
            return false;
        const unsigned char* const Group = Table + GroupAt;
        int const Records = ReadU16(Group);
        if (GroupAt + 4 + 6 * static_cast<std::uint64_t>(Records) > Length)
            return false;
        int Found = -1;
        for (int Record = 0; Record < Records; ++Record)
        {
            const unsigned char* const Entry = Group + 4 + Record * 6;
            int const Height = ReadU16(Entry);
            int const YMax = ReadS16(Entry + 2);
            int const YMin = ReadS16(Entry + 4);
            if (bCell ? YMax - YMin <= Pixels : Height == Pixels)
            {
                *Ascent = YMax;
                *Descent = -YMin;
                *Ppem = Height;
                Found = Record;
                if (!bCell)
                    break;
            }
        }
        if (Found < 0 || (bCell && Found == Records - 1 && *Ascent + *Descent < Pixels))
            return false;
        return true;
    }
    return false;
}

// GDI's sizes for a LOGFONT on a face (see the top of the file).
void SizeFont(FFont& Font)
{
    const FFace& Face = *Font.Face;
    int const Cell = std::max(1, Face.WinAscent + Face.WinDescent);
    LONG Height = Font.Log.lfHeight;
    if (Height == 0)
        Height = -16; // the system font's em height at 96 dpi
    if (Height < 0)
        Font.Scale = static_cast<float>(-Height) / static_cast<float>(Face.UnitsPerEm);
    else
        Font.Scale = static_cast<float>(Height) / static_cast<float>(Cell);
    Font.Ascent = static_cast<LONG>(std::lround(Face.WinAscent * Font.Scale));
    Font.Descent = static_cast<LONG>(std::lround(Face.WinDescent * Font.Scale));
    int VdmxAscent = 0;
    int VdmxDescent = 0;
    int VdmxPpem = 0;
    if (VdmxExtent(Face, Height < 0 ? -Height : Height, Height > 0, &VdmxAscent, &VdmxDescent,
                   &VdmxPpem))
    {
        Font.Ascent = VdmxAscent;
        Font.Descent = VdmxDescent;
        // A cell height realizes the entry's em height: glyphs are drawn at that size too.
        if (Height > 0 && VdmxPpem > 0)
            Font.Scale = static_cast<float>(VdmxPpem) / static_cast<float>(Face.UnitsPerEm);
    }
    Font.ScaleX = Font.Scale;
    if (Font.Log.lfWidth > 0 && Face.AverageWidth > 0)
        Font.ScaleX = static_cast<float>(Font.Log.lfWidth) / static_cast<float>(Face.AverageWidth);
    Font.Height = Font.Ascent + Font.Descent;
    Font.InternalLeading = std::max<LONG>(
        0, Font.Height - static_cast<LONG>(std::lround(Face.UnitsPerEm * Font.Scale)));
    int const HheaHeight = Face.HheaAscent + Face.HheaDescent + Face.HheaLineGap;
    Font.ExternalLeading =
        std::max<LONG>(0, static_cast<LONG>(std::lround((HheaHeight - Cell) * Font.Scale)));
    Font.AveCharWidth =
        std::max<LONG>(1, static_cast<LONG>(std::lround(Face.AverageWidth * Font.ScaleX)));
    Font.MaxCharWidth =
        std::max<LONG>(1, static_cast<LONG>(std::lround(Face.AdvanceMax * Font.ScaleX)));
    if (Font.bEmbolden)
    {
        ++Font.AveCharWidth;
        ++Font.MaxCharWidth;
    }
}

FFont* MakeFont(const LOGFONTA& Log)
{
    bool const bBold = Log.lfWeight >= 600;
    bool const bItalic = Log.lfItalic != 0;
    // A catalogued file may since have gone (or not be readable): the next candidate then.
    std::shared_ptr<FFace> Face;
    for (const FState::FCatalogEntry* const Entry : FindFaces(Log.lfFaceName, bBold, bItalic))
        if ((Face = OpenFace(Entry->Path, Entry->Index)) != nullptr)
            break;
    if (Face == nullptr)
        return nullptr;
    FFont* const Font = new FFont();
    Font->Log = Log;
    Font->Face = std::move(Face);
    Font->bEmbolden = bBold && !Font->Face->bBold;
    SizeFont(*Font);
    return Register(Font);
}

// The DC's font, or the system font.
FFont* FontOf(FDc& Dc)
{
    if (Dc.Font != nullptr)
        return Dc.Font;
    FState& S = State();
    if (S.SystemFont == nullptr)
    {
        LOGFONTA Log = {};
        Log.lfHeight = -16;
        Log.lfWeight = FW_NORMAL;
        std::strcpy(Log.lfFaceName, "Arial");
        S.SystemFont = MakeFont(Log);
        if (S.SystemFont != nullptr)
            S.SystemFont->bStock = true;
    }
    return S.SystemFont;
}

// A character's advance in pixels, GDI's whole pixels.
int AdvanceOf(const FFont& Font, std::uint32_t Character)
{
    int Advance = 0;
    int Bearing = 0;
    stbtt_GetGlyphHMetrics(&Font.Face->Info, GlyphOf(*Font.Face, Character), &Advance, &Bearing);
    return static_cast<int>(std::lround(Advance * Font.ScaleX)) + (Font.bEmbolden ? 1 : 0);
}

// A character of a string and the string units it takes: 2 for a UTF-16 surrogate pair. GDI
// counts in units (Count, Dx, the fit count and the extents), so a pair takes two of each.
struct FCharacter
{
    std::uint32_t Code;
    int Units;
};

// The characters of a wide string: wchar_t is UTF-32 here, and a UTF-16 surrogate pair (text
// from the wire or a resource) is one character.
std::vector<FCharacter> Characters(const wchar_t* Text, int Count)
{
    std::vector<FCharacter> Out;
    if (Text == nullptr)
        return Out;
    if (Count < 0)
        Count = static_cast<int>(std::wcslen(Text));
    for (int Index = 0; Index < Count; ++Index)
    {
        std::uint32_t Code = static_cast<std::uint32_t>(Text[Index]);
        int Units = 1;
        if (Code >= 0xD800 && Code <= 0xDBFF && Index + 1 < Count)
        {
            std::uint32_t const Low = static_cast<std::uint32_t>(Text[Index + 1]);
            if (Low >= 0xDC00 && Low <= 0xDFFF)
            {
                Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
                Units = 2;
                ++Index;
            }
        }
        Out.push_back(FCharacter{Code, Units});
    }
    return Out;
}

std::vector<FCharacter> Characters(LPCSTR Text, int Count)
{
    std::vector<FCharacter> Out;
    if (Text == nullptr)
        return Out;
    if (Count < 0)
        Count = static_cast<int>(std::strlen(Text));
    for (int Index = 0; Index < Count; ++Index)
        Out.push_back(FCharacter{static_cast<unsigned char>(Text[Index]), 1});
    return Out;
}

void FillTextMetrics(const FFont& Font, TEXTMETRICW& Out)
{
    Out = TEXTMETRICW{};
    Out.tmHeight = Font.Height;
    Out.tmAscent = Font.Ascent;
    Out.tmDescent = Font.Descent;
    Out.tmInternalLeading = Font.InternalLeading;
    Out.tmExternalLeading = Font.ExternalLeading;
    Out.tmAveCharWidth = Font.AveCharWidth;
    Out.tmMaxCharWidth = Font.MaxCharWidth;
    Out.tmWeight = Font.Log.lfWeight != 0 ? Font.Log.lfWeight : FW_NORMAL;
    Out.tmOverhang = 0;
    Out.tmDigitizedAspectX = 96;
    Out.tmDigitizedAspectY = 96;
    Out.tmFirstChar = 0x20;
    Out.tmLastChar = 0xFFFC;
    Out.tmDefaultChar = 0x1F;
    Out.tmBreakChar = 0x20;
    Out.tmItalic = Font.Log.lfItalic;
    Out.tmUnderlined = Font.Log.lfUnderline;
    Out.tmStruckOut = Font.Log.lfStrikeOut;
    // TMPF_VECTOR | TMPF_TRUETYPE, and TMPF_FIXED_PITCH's bit set for a variable pitch (sic).
    Out.tmPitchAndFamily = static_cast<BYTE>(0x06 | (Font.Face->bFixedPitch ? 0 : 0x01));
    Out.tmCharSet = Font.Log.lfCharSet;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------

RECT Intersect(const RECT& A, const RECT& B)
{
    RECT Out = {std::max(A.left, B.left), std::max(A.top, B.top), std::min(A.right, B.right),
                std::min(A.bottom, B.bottom)};
    if (Out.right < Out.left)
        Out.right = Out.left;
    if (Out.bottom < Out.top)
        Out.bottom = Out.top;
    return Out;
}

// Where a DC may draw: its bitmap, within its clip region.
RECT DrawableOf(const FDc& Dc)
{
    RECT Area = {0, 0, Dc.Bitmap != nullptr ? Dc.Bitmap->Width : 0,
                 Dc.Bitmap != nullptr ? Dc.Bitmap->Height : 0};
    if (Dc.bClip)
        Area = Intersect(Area, Dc.Clip);
    return Area;
}

void FillPixels(FBitmap& Bitmap, const RECT& Area, std::uint32_t Pixel)
{
    if (Area.right <= Area.left || Area.bottom <= Area.top)
        return; // nothing to fill, and no iterator past the pixels formed
    for (LONG Y = Area.top; Y < Area.bottom; ++Y)
        std::fill(Bitmap.Pixels.begin() + Y * Bitmap.Width + Area.left,
                  Bitmap.Pixels.begin() + Y * Bitmap.Width + Area.right, Pixel);
}

std::uint32_t Blend(std::uint32_t Under, std::uint32_t Over, unsigned Alpha)
{
    std::uint32_t Out = 0;
    for (int Shift = 0; Shift <= 16; Shift += 8)
    {
        unsigned const A = (Under >> Shift) & 0xFF;
        unsigned const B = (Over >> Shift) & 0xFF;
        Out |= ((A * (255 - Alpha) + B * Alpha + 127) / 255) << Shift;
    }
    return Out;
}

// One glyph with its pen at (PenX, BaselineY), coverage blended in Pixel, inside Area.
void DrawGlyph(FBitmap& Bitmap, const FFont& Font, std::uint32_t Character, int PenX, int BaselineY,
               std::uint32_t Pixel, const RECT& Area)
{
    const stbtt_fontinfo& Info = Font.Face->Info;
    int const Glyph = GlyphOf(*Font.Face, Character);
    if (!GlyphReadable(*Font.Face, Glyph))
        return; // an outline that runs past its glyph's data: nothing drawn
    int X0 = 0;
    int Y0 = 0;
    int X1 = 0;
    int Y1 = 0;
    stbtt_GetGlyphBitmapBox(&Info, Glyph, Font.ScaleX, Font.Scale, &X0, &Y0, &X1, &Y1);
    int const Width = X1 - X0;
    int const Height = Y1 - Y0;
    if (Width <= 0 || Height <= 0)
        return;
    std::vector<unsigned char> Coverage(static_cast<std::size_t>(Width) *
                                        static_cast<std::size_t>(Height));
    stbtt_MakeGlyphBitmap(&Info, Coverage.data(), Width, Height, Width, Font.ScaleX, Font.Scale,
                          Glyph);
    // An italic request without an italic face: slant the rows (GDI's synthetic italic).
    bool const bSlant = Font.Log.lfItalic != 0 && !Font.Face->bItalic;
    for (int Pass = 0; Pass < (Font.bEmbolden ? 2 : 1); ++Pass)
        for (int Row = 0; Row < Height; ++Row)
        {
            int const Y = BaselineY + Y0 + Row;
            if (Y < Area.top || Y >= Area.bottom)
                continue;
            int const Slant = bSlant ? (BaselineY - Y) / 4 : 0;
            for (int Column = 0; Column < Width; ++Column)
            {
                int const X = PenX + X0 + Column + Pass + Slant;
                if (X < Area.left || X >= Area.right)
                    continue;
                unsigned const Alpha = Coverage[static_cast<std::size_t>(Row) * Width + Column];
                if (Alpha == 0)
                    continue;
                std::uint32_t& Target =
                    Bitmap.Pixels[static_cast<std::size_t>(Y) * Bitmap.Width + X];
                Target = Blend(Target, Pixel, Alpha);
            }
        }
}

BOOL DrawText(FDc& Dc, int X, int Y, UINT Options, const RECT* Rect,
              const std::vector<FCharacter>& Text, const int* Dx)
{
    FFont* const Font = FontOf(Dc);
    if (Font == nullptr || Dc.Bitmap == nullptr)
        return Dc.Bitmap != nullptr ? FALSE : TRUE; // a DC without a bitmap draws nowhere
    FBitmap& Bitmap = *Dc.Bitmap;
    RECT Area = DrawableOf(Dc);
    if (Rect != nullptr && (Options & ETO_OPAQUE) != 0)
        FillPixels(Bitmap, Intersect(Area, *Rect), PixelOf(Dc.BkColor));
    if (Rect != nullptr && (Options & ETO_CLIPPED) != 0)
        Area = Intersect(Area, *Rect);

    // Dx has an advance per string unit: a surrogate pair's character moves by both.
    std::vector<int> Advances(Text.size());
    int Extent = 0;
    std::size_t Unit = 0;
    for (std::size_t Index = 0; Index < Text.size(); ++Index)
    {
        if (Dx != nullptr)
            Advances[Index] = Dx[Unit] + (Text[Index].Units == 2 ? Dx[Unit + 1] : 0);
        else
            Advances[Index] = AdvanceOf(*Font, Text[Index].Code);
        Unit += static_cast<std::size_t>(Text[Index].Units);
        Extent += Advances[Index];
    }
    switch (Dc.Align & TA_CENTER)
    {
    case TA_RIGHT:
        X -= Extent;
        break;
    case TA_CENTER:
        X -= Extent / 2;
        break;
    default:
        break;
    }
    int Baseline = Y + Font->Ascent;
    if ((Dc.Align & TA_BASELINE) == TA_BASELINE)
        Baseline = Y;
    else if ((Dc.Align & TA_BOTTOM) == TA_BOTTOM)
        Baseline = Y - Font->Descent;
    int const Top = Baseline - Font->Ascent;
    if (Dc.BkMode == OPAQUE)
        FillPixels(Bitmap, Intersect(Area, RECT{X, Top, X + Extent, Top + Font->Height}),
                   PixelOf(Dc.BkColor));
    std::uint32_t const Pixel = PixelOf(Dc.TextColor);
    int Pen = X;
    for (std::size_t Index = 0; Index < Text.size(); ++Index)
    {
        DrawGlyph(Bitmap, *Font, Text[Index].Code, Pen, Baseline, Pixel, Area);
        Pen += Advances[Index];
    }
    return TRUE;
}

// The text's size; with Fit, the string units whose characters fit MaxExtent; with Extents, the
// extent after each unit (both units of a surrogate pair have the extent after its character).
BOOL Extent(FDc& Dc, const std::vector<FCharacter>& Text, int MaxExtent, int* Fit, int* Extents,
            LPSIZE Size)
{
    FFont* const Font = FontOf(Dc);
    if (Font == nullptr || Size == nullptr)
        return FALSE;
    int Total = 0;
    int Fitted = 0;
    int Units = 0;
    for (const FCharacter& Character : Text)
    {
        Total += AdvanceOf(*Font, Character.Code);
        for (int Unit = 0; Unit < Character.Units; ++Unit)
            if (Extents != nullptr)
                Extents[Units + Unit] = Total;
        Units += Character.Units;
        if (MaxExtent < 0 || Total <= MaxExtent)
            Fitted = Units;
    }
    if (Fit != nullptr)
        *Fit = Fitted;
    Size->cx = Total;
    Size->cy = Font->Height;
    return TRUE;
}

// The low and high bits of a mask (BI_BITFIELDS), for packing an 8-bit channel into it.
void MaskShape(DWORD Mask, int* Shift, int* Bits)
{
    *Shift = 0;
    *Bits = 0;
    if (Mask == 0)
        return;
    while ((Mask & 1u) == 0)
    {
        Mask >>= 1;
        ++*Shift;
    }
    while ((Mask & 1u) != 0)
    {
        Mask >>= 1;
        ++*Bits;
    }
}

DWORD PackChannel(unsigned Value, int Shift, int Bits)
{
    if (Bits <= 0)
        return 0;
    return static_cast<DWORD>(Value >> (8 - std::min(Bits, 8))) << Shift;
}

FBrush* StockBrush(int Index)
{
    static const COLORREF kColors[] = {0xFFFFFF, 0xC0C0C0, 0x808080, 0x404040, 0x000000, 0};
    FState& S = State();
    if (S.StockBrushes[Index] == nullptr)
    {
        S.StockBrushes[Index] = Register(new FBrush(kColors[Index], Index == NULL_BRUSH));
        S.StockBrushes[Index]->bStock = true;
    }
    return S.StockBrushes[Index];
}

// A memory DC starts with the stock 1x1 bitmap; a screen DC (GetDC) has no surface: it measures
// text, makes compatible DCs and bitmaps, and draws nowhere.
FDc* MakeDc(bool bScreen)
{
    FState& S = State();
    if (bScreen)
    {
        FDc* const Screen = Register(new FDc());
        Screen->bScreen = true;
        Screen->Brush = StockBrush(WHITE_BRUSH);
        return Screen;
    }
    if (S.DefaultBitmap == nullptr)
    {
        S.DefaultBitmap = Register(new FBitmap(1, 1));
        S.DefaultBitmap->bStock = true;
    }
    FDc* const Dc = Register(new FDc());
    Dc->Bitmap = S.DefaultBitmap;
    Dc->Brush = StockBrush(WHITE_BRUSH);
    return Dc;
}

// Whether an object is selected into a DC (GDI does not delete those).
bool IsSelected(const FObject* Object)
{
    for (FObject* Candidate : State().Objects)
    {
        if (Candidate->Kind != EKind::Dc)
            continue;
        const FDc* const Dc = static_cast<const FDc*>(Candidate);
        if (Dc->Bitmap == Object || Dc->Font == Object || Dc->Brush == Object)
            return true;
    }
    return false;
}

BOOL Blit(FDc& Target, int X, int Y, int Width, int Height, FDc& Source, int SourceX, int SourceY,
          int SourceWidth, int SourceHeight)
{
    if (Target.Bitmap == nullptr || Source.Bitmap == nullptr || Width == 0 || Height == 0 ||
        SourceWidth == 0 || SourceHeight == 0)
        return FALSE;
    RECT const Area = DrawableOf(Target);
    const FBitmap& From = *Source.Bitmap;
    FBitmap& To = *Target.Bitmap;
    // A copy within one bitmap reads the pixels as they were before it, however the two
    // rectangles overlap.
    std::vector<std::uint32_t> Snapshot;
    if (&From == &To)
        Snapshot = From.Pixels;
    const std::vector<std::uint32_t>& FromPixels = &From == &To ? Snapshot : From.Pixels;
    // A negative extent runs its rectangle left (or up) from its origin. The destination's n-th
    // column from its origin samples the source's n-th scaled column from the source's origin,
    // inside the source rectangle; signs that differ mirror the image. Extents are widened
    // before their magnitude is taken (INT_MIN's is no int).
    std::int64_t const Columns = std::abs(static_cast<std::int64_t>(Width));
    std::int64_t const Rows = std::abs(static_cast<std::int64_t>(Height));
    std::int64_t const SourceColumns = std::abs(static_cast<std::int64_t>(SourceWidth));
    std::int64_t const SourceRows = std::abs(static_cast<std::int64_t>(SourceHeight));
    // Only the steps whose destination lies in the drawable area are visited: [First, Last).
    auto const Visible = [](std::int64_t Origin, bool bForward, std::int64_t Count, LONG Low,
                            LONG High, std::int64_t* First, std::int64_t* Last)
    {
        *First = std::max<std::int64_t>(0, bForward ? Low - Origin : Origin - High + 1);
        *Last = std::min<std::int64_t>(Count, bForward ? High - Origin : Origin - Low + 1);
    };
    std::int64_t FirstRow = 0;
    std::int64_t LastRow = 0;
    std::int64_t FirstColumn = 0;
    std::int64_t LastColumn = 0;
    Visible(Y, Height > 0, Rows, Area.top, Area.bottom, &FirstRow, &LastRow);
    Visible(X, Width > 0, Columns, Area.left, Area.right, &FirstColumn, &LastColumn);
    for (std::int64_t Row = FirstRow; Row < LastRow; ++Row)
    {
        std::int64_t const ToY = Y + (Height > 0 ? Row : -Row);
        std::int64_t const RowOffset = Row * SourceRows / Rows;
        std::int64_t const FromY = SourceY + (SourceHeight > 0 ? RowOffset : -RowOffset);
        if (FromY < 0 || FromY >= From.Height)
            continue;
        for (std::int64_t Column = FirstColumn; Column < LastColumn; ++Column)
        {
            std::int64_t const ToX = X + (Width > 0 ? Column : -Column);
            std::int64_t const ColumnOffset = Column * SourceColumns / Columns;
            std::int64_t const FromX = SourceX + (SourceWidth > 0 ? ColumnOffset : -ColumnOffset);
            if (FromX < 0 || FromX >= From.Width)
                continue;
            To.Pixels[static_cast<std::size_t>(ToY * To.Width + ToX)] =
                FromPixels[static_cast<std::size_t>(FromY * From.Width + FromX)];
        }
    }
    return TRUE;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// Device contexts and objects
// ---------------------------------------------------------------------------------------------

HDC GetDC(HWND)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return reinterpret_cast<HDC>(MakeDc(true));
}

int ReleaseDC(HWND, HDC Dc)
{
    return DeleteDC(Dc) ? 1 : 0;
}

HDC CreateCompatibleDC(HDC)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return reinterpret_cast<HDC>(MakeDc(false));
}

BOOL DeleteDC(HDC Handle)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr)
        return FALSE;
    State().Objects.erase(Dc);
    delete Dc;
    return TRUE;
}

// The previous object of the same kind; for a region, SIMPLEREGION / NULLREGION.
HGDIOBJ SelectObject(HDC Handle, HGDIOBJ Object)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    FObject* const Selected = static_cast<FObject*>(Object);
    if (Dc == nullptr || Selected == nullptr || State().Objects.count(Selected) == 0)
        return nullptr;
    switch (Selected->Kind)
    {
    case EKind::Bitmap:
    {
        // A bitmap is selected into one DC at a time (the stock 1x1 one into any memory DC), and
        // never into a screen DC.
        if (Dc->bScreen)
            return nullptr;
        if (!Selected->bStock && Selected != Dc->Bitmap && IsSelected(Selected))
            return nullptr;
        FBitmap* const Previous = Dc->Bitmap;
        Dc->Bitmap = static_cast<FBitmap*>(Selected);
        return Previous;
    }
    case EKind::Font:
    {
        FFont* const Previous = FontOf(*Dc);
        Dc->Font = static_cast<FFont*>(Selected);
        return Previous;
    }
    case EKind::Brush:
    {
        FBrush* const Previous = Dc->Brush;
        Dc->Brush = static_cast<FBrush*>(Selected);
        return Previous;
    }
    case EKind::Region:
    {
        int const Type = SelectClipRgn(Handle, static_cast<HRGN>(Object));
        if (Type == ERROR)
            return nullptr;
        return reinterpret_cast<HGDIOBJ>(static_cast<std::uintptr_t>(Type));
    }
    default:
        return nullptr;
    }
}

BOOL DeleteObject(HGDIOBJ Object)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FObject* const Gone = static_cast<FObject*>(Object);
    if (Gone == nullptr || State().Objects.count(Gone) == 0 || Gone->Kind == EKind::Dc)
        return FALSE;
    if (Gone->bStock)
        return TRUE;
    if (IsSelected(Gone))
        return FALSE;
    State().Objects.erase(Gone);
    delete Gone;
    return TRUE;
}

HGDIOBJ GetStockObject(int Index)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    if (Index >= WHITE_BRUSH && Index <= NULL_BRUSH)
        return StockBrush(Index);
    if (Index == SYSTEM_FONT || Index == DEFAULT_GUI_FONT)
    {
        FDc Scratch;
        return FontOf(Scratch);
    }
    return nullptr;
}

// A bitmap's BITMAP (or DIBSECTION, whose bitmap part is the same), or a font's LOGFONTA.
int GetObjectA(HANDLE Object, int Size, LPVOID Out)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    if (FBitmap* const Bitmap = As<FBitmap>(Object, EKind::Bitmap))
    {
        if (Out == nullptr)
            return static_cast<int>(sizeof(BITMAP));
        if (Size < static_cast<int>(sizeof(BITMAP)))
            return 0;
        BITMAP Info = {0, Bitmap->Width, Bitmap->Height, Bitmap->Width * 4, 1, 32, nullptr};
        std::memcpy(Out, &Info, sizeof(Info));
        return static_cast<int>(sizeof(BITMAP));
    }
    if (FFont* const Font = As<FFont>(Object, EKind::Font))
    {
        if (Out == nullptr)
            return static_cast<int>(sizeof(LOGFONTA));
        if (Size < static_cast<int>(sizeof(LOGFONTA)))
            return 0;
        std::memcpy(Out, &Font->Log, sizeof(LOGFONTA));
        return static_cast<int>(sizeof(LOGFONTA));
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Text
// ---------------------------------------------------------------------------------------------

COLORREF SetTextColor(HDC Handle, COLORREF Color)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr)
        return CLR_INVALID;
    COLORREF const Previous = Dc->TextColor;
    Dc->TextColor = Color & 0xFFFFFF;
    return Previous;
}

COLORREF SetBkColor(HDC Handle, COLORREF Color)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr)
        return CLR_INVALID;
    COLORREF const Previous = Dc->BkColor;
    Dc->BkColor = Color & 0xFFFFFF;
    return Previous;
}

int SetBkMode(HDC Handle, int Mode)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr || (Mode != TRANSPARENT && Mode != OPAQUE))
        return 0;
    int const Previous = Dc->BkMode;
    Dc->BkMode = Mode;
    return Previous;
}

UINT SetTextAlign(HDC Handle, UINT Align)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr)
        return GDI_ERROR;
    UINT const Previous = Dc->Align;
    Dc->Align = Align;
    return Previous;
}

HFONT CreateFontIndirectA(const LOGFONTA* Log)
{
    if (Log == nullptr)
        return nullptr;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return reinterpret_cast<HFONT>(MakeFont(*Log));
}

BOOL GetTextMetricsW(HDC Handle, TEXTMETRICW* Metrics)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    FFont* const Font = Dc != nullptr ? FontOf(*Dc) : nullptr;
    if (Font == nullptr || Metrics == nullptr)
        return FALSE;
    FillTextMetrics(*Font, *Metrics);
    return TRUE;
}

// The DC's font's face name, as realized (a fallback's when the face asked is not installed):
// the characters copied, with the null; the length the name needs when Name is null.
int GetTextFaceA(HDC Handle, int Count, LPSTR Name)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    FFont* const Font = Dc != nullptr ? FontOf(*Dc) : nullptr;
    if (Font == nullptr)
        return 0;
    const std::string& Family = Font->Face->Family;
    if (Name == nullptr)
        return static_cast<int>(Family.size()) + 1;
    if (Count <= 0)
        return 0;
    std::size_t const Copied = std::min(Family.size(), static_cast<std::size_t>(Count) - 1);
    std::memcpy(Name, Family.data(), Copied);
    Name[Copied] = '\0';
    return static_cast<int>(Copied) + 1;
}

BOOL GetTextMetricsA(HDC Handle, TEXTMETRICA* Metrics)
{
    TEXTMETRICW Wide;
    if (Metrics == nullptr || !GetTextMetricsW(Handle, &Wide))
        return FALSE;
    std::memcpy(Metrics, &Wide, offsetof(TEXTMETRICA, tmFirstChar));
    Metrics->tmFirstChar = 0x20;
    Metrics->tmLastChar = 0xFF;
    Metrics->tmDefaultChar = 0x1F;
    Metrics->tmBreakChar = 0x20;
    Metrics->tmItalic = Wide.tmItalic;
    Metrics->tmUnderlined = Wide.tmUnderlined;
    Metrics->tmStruckOut = Wide.tmStruckOut;
    Metrics->tmPitchAndFamily = Wide.tmPitchAndFamily;
    Metrics->tmCharSet = Wide.tmCharSet;
    return TRUE;
}

BOOL GetCharWidthW(HDC Handle, UINT First, UINT Last, int* Widths)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    FFont* const Font = Dc != nullptr ? FontOf(*Dc) : nullptr;
    // Characters are Unicode code points; the loop ends on Last, so it never wraps.
    if (Font == nullptr || Widths == nullptr || Last < First || Last > 0x10FFFF)
        return FALSE;
    for (UINT Character = First;; ++Character)
    {
        Widths[Character - First] = AdvanceOf(*Font, Character);
        if (Character == Last)
            break;
    }
    return TRUE;
}

BOOL GetTextExtentPoint32W(HDC Handle, const wchar_t* Text, int Count, LPSIZE Size)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr || Count < 0)
        return FALSE;
    return Extent(*Dc, Characters(Text, Count), -1, nullptr, nullptr, Size);
}

BOOL GetTextExtentPoint32A(HDC Handle, LPCSTR Text, int Count, LPSIZE Size)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr || Count < 0)
        return FALSE;
    return Extent(*Dc, Characters(Text, Count), -1, nullptr, nullptr, Size);
}

BOOL GetTextExtentExPointW(HDC Handle, const wchar_t* Text, int Count, int MaxExtent, int* Fit,
                           int* Extents, LPSIZE Size)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr || Count < 0)
        return FALSE;
    return Extent(*Dc, Characters(Text, Count), Fit != nullptr ? MaxExtent : -1, Fit, Extents,
                  Size);
}

constexpr UINT kMaxTextCount = 8192;

BOOL ExtTextOutW(HDC Handle, int X, int Y, UINT Options, const RECT* Rect, const wchar_t* Text,
                 UINT Count, const int* Dx)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    // GDI draws at most 8192 characters a call; a larger count is refused before it becomes an
    // int (a negative one would read the text to a terminator). A zero count still fills an
    // ETO_OPAQUE rectangle.
    if (Dc == nullptr || Count > kMaxTextCount)
        return FALSE;
    return DrawText(*Dc, X, Y, Options, Rect, Characters(Text, static_cast<int>(Count)), Dx);
}

BOOL ExtTextOutA(HDC Handle, int X, int Y, UINT Options, const RECT* Rect, LPCSTR Text, UINT Count,
                 const int* Dx)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    // GDI draws at most 8192 characters a call; a larger count is refused before it becomes an
    // int (a negative one would read the text to a terminator). A zero count still fills an
    // ETO_OPAQUE rectangle.
    if (Dc == nullptr || Count > kMaxTextCount)
        return FALSE;
    return DrawText(*Dc, X, Y, Options, Rect, Characters(Text, static_cast<int>(Count)), Dx);
}

// ---------------------------------------------------------------------------------------------
// Bitmaps, brushes, regions
// ---------------------------------------------------------------------------------------------

// Always 32 bits per pixel, black, whatever the DC.
HBITMAP CreateCompatibleBitmap(HDC, int Width, int Height)
{
    if (Width <= 0 || Height <= 0 || Width > 0x4000 || Height > 0x4000)
        return nullptr;
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return reinterpret_cast<HBITMAP>(Register(new FBitmap(Width, Height)));
}

// GDI's rules: with Bits null and a zero bit count, the header describes the bitmap; with Bits
// null otherwise, nothing is copied and the answer is nonzero. Otherwise Lines scan lines from
// Start in the header's format (32, 24 or 16 bits; 16 is 5-5-5 for BI_RGB, the bmiColors masks
// for BI_BITFIELDS), bottom-up for a positive biHeight. The lines copied, 0 on failure.
int GetDIBits(HDC, HBITMAP Handle, UINT Start, UINT Lines, LPVOID Bits, BITMAPINFO* Info, UINT)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FBitmap* const Bitmap = As<FBitmap>(Handle, EKind::Bitmap);
    if (Bitmap == nullptr || Info == nullptr)
        return 0;
    BITMAPINFOHEADER& Header = Info->bmiHeader;
    if (Bits == nullptr)
    {
        if (Header.biBitCount == 0)
        {
            Header.biWidth = Bitmap->Width;
            Header.biHeight = Bitmap->Height;
            Header.biPlanes = 1;
            Header.biBitCount = 32;
            Header.biCompression = BI_RGB;
            Header.biSizeImage = static_cast<DWORD>(Bitmap->Width * Bitmap->Height * 4);
        }
        return Bitmap->Height;
    }
    int const BitCount = Header.biBitCount;
    if (BitCount != 32 && BitCount != 24 && BitCount != 16)
        return 0;
    // A DIB's width is positive and its height nonzero (negative for top-down). The height's
    // magnitude is taken widened (LONG is 32 bits: LONG_MIN's is no LONG).
    std::int64_t const DibWidth = Header.biWidth;
    std::int64_t const DibHeight = Header.biHeight;
    if (DibWidth <= 0 || DibHeight == 0)
        return 0;
    int const Width = static_cast<int>(std::min<std::int64_t>(Bitmap->Width, DibWidth));
    std::int64_t const Rows = DibHeight < 0 ? -DibHeight : DibHeight;
    // Scan lines past the DIB's rows are none: Start is checked before it becomes an index.
    if (Start >= static_cast<UINT>(Rows))
        return 0;
    Lines = std::min<UINT>(Lines, static_cast<UINT>(Rows) - Start);
    bool const bTopDown = Header.biHeight < 0;
    std::size_t const Stride = ((static_cast<std::size_t>(DibWidth) * BitCount + 31) / 32) * 4;
    DWORD Masks[3] = {0x7C00, 0x03E0, 0x001F};
    if (BitCount == 16 && Header.biCompression == BI_BITFIELDS)
        std::memcpy(Masks, Info->bmiColors, sizeof(Masks));
    int Shifts[3];
    int Widths[3];
    for (int Channel = 0; Channel < 3; ++Channel)
        MaskShape(Masks[Channel], &Shifts[Channel], &Widths[Channel]);
    unsigned char* const Out = static_cast<unsigned char*>(Bits);
    int Copied = 0;
    for (UINT Line = 0; Line < Lines; ++Line)
    {
        int const DibRow = static_cast<int>(Start + Line);
        // The DIB's row DibRow is the bitmap's row from the top (top-down) or bottom.
        std::int64_t const SourceRow = bTopDown ? DibRow : Rows - 1 - DibRow;
        unsigned char* const Row = Out + static_cast<std::size_t>(Line) * Stride;
        std::memset(Row, 0, Stride);
        if (SourceRow >= 0 && SourceRow < Bitmap->Height)
        {
            const std::uint32_t* const Pixels =
                &Bitmap->Pixels[static_cast<std::size_t>(SourceRow) * Bitmap->Width];
            for (int Column = 0; Column < Width; ++Column)
            {
                std::uint32_t const Pixel = Pixels[Column];
                unsigned const Red = (Pixel >> 16) & 0xFF;
                unsigned const Green = (Pixel >> 8) & 0xFF;
                unsigned const Blue = Pixel & 0xFF;
                if (BitCount == 32)
                {
                    unsigned char* const Target = Row + Column * 4;
                    Target[0] = static_cast<unsigned char>(Blue);
                    Target[1] = static_cast<unsigned char>(Green);
                    Target[2] = static_cast<unsigned char>(Red);
                    Target[3] = 0;
                }
                else if (BitCount == 24)
                {
                    unsigned char* const Target = Row + Column * 3;
                    Target[0] = static_cast<unsigned char>(Blue);
                    Target[1] = static_cast<unsigned char>(Green);
                    Target[2] = static_cast<unsigned char>(Red);
                }
                else
                {
                    DWORD const Packed = PackChannel(Red, Shifts[0], Widths[0]) |
                                         PackChannel(Green, Shifts[1], Widths[1]) |
                                         PackChannel(Blue, Shifts[2], Widths[2]);
                    Row[Column * 2] = static_cast<unsigned char>(Packed & 0xFF);
                    Row[Column * 2 + 1] = static_cast<unsigned char>((Packed >> 8) & 0xFF);
                }
            }
        }
        ++Copied;
    }
    return Copied;
}

HBRUSH CreateSolidBrush(COLORREF Color)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return reinterpret_cast<HBRUSH>(Register(new FBrush(Color & 0xFFFFFF, false)));
}

// A brush, or a system colour index + 1 (COLOR_WINDOW + 1...), as window classes pass it.
int FillRect(HDC Handle, const RECT* Rect, HBRUSH BrushHandle)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr || Rect == nullptr || Dc->Bitmap == nullptr)
        return 0;
    COLORREF Color = 0;
    if (FBrush* const Brush = As<FBrush>(BrushHandle, EKind::Brush))
    {
        if (Brush->bNull)
            return 1;
        Color = Brush->Color;
    }
    else if (reinterpret_cast<std::uintptr_t>(BrushHandle) <= 31)
        Color =
            reinterpret_cast<std::uintptr_t>(BrushHandle) == COLOR_WINDOW + 1 ? 0xFFFFFF : 0xC0C0C0;
    else
        return 0;
    FillPixels(*Dc->Bitmap, Intersect(DrawableOf(*Dc), *Rect), PixelOf(Color));
    return 1;
}

// Only SRCCOPY is drawn (all the game uses); any other raster operation fails and changes nothing.
BOOL BitBlt(HDC Target, int X, int Y, int Width, int Height, HDC Source, int SourceX, int SourceY,
            DWORD Rop)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const To = As<FDc>(Target, EKind::Dc);
    FDc* const From = As<FDc>(Source, EKind::Dc);
    if (To == nullptr || From == nullptr || Rop != SRCCOPY)
        return FALSE;
    return Blit(*To, X, Y, Width, Height, *From, SourceX, SourceY, Width, Height);
}

BOOL StretchBlt(HDC Target, int X, int Y, int Width, int Height, HDC Source, int SourceX,
                int SourceY, int SourceWidth, int SourceHeight, DWORD Rop)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const To = As<FDc>(Target, EKind::Dc);
    FDc* const From = As<FDc>(Source, EKind::Dc);
    if (To == nullptr || From == nullptr || Rop != SRCCOPY)
        return FALSE;
    return Blit(*To, X, Y, Width, Height, *From, SourceX, SourceY, SourceWidth, SourceHeight);
}

// Bitmap files are not read here yet.
HANDLE LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT)
{
    return nullptr;
}

HRGN CreateRectRgn(int Left, int Top, int Right, int Bottom)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    RECT const Rect = {std::min(Left, Right), std::min(Top, Bottom), std::max(Left, Right),
                       std::max(Top, Bottom)};
    return reinterpret_cast<HRGN>(Register(new FRegion(Rect)));
}

HRGN CreateRectRgnIndirect(const RECT* Rect)
{
    if (Rect == nullptr)
        return nullptr;
    return CreateRectRgn(Rect->left, Rect->top, Rect->right, Rect->bottom);
}

// The DC keeps a copy of the region; a null region removes the clip. SIMPLEREGION / NULLREGION.
int SelectClipRgn(HDC Handle, HRGN Region)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr)
        return ERROR;
    if (Region == nullptr)
    {
        Dc->bClip = false;
        return SIMPLEREGION;
    }
    FRegion* const Clip = As<FRegion>(Region, EKind::Region);
    if (Clip == nullptr)
        return ERROR;
    Dc->bClip = true;
    Dc->Clip = Clip->Rect;
    return IsRectEmpty(&Clip->Rect) ? NULLREGION : SIMPLEREGION;
}

// 1 with the clip copied into Region, 0 when the DC has none, -1 on error.
int GetClipRgn(HDC Handle, HRGN Region)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    FRegion* const Out = As<FRegion>(Region, EKind::Region);
    if (Dc == nullptr || Out == nullptr)
        return -1;
    if (!Dc->bClip)
        return 0;
    Out->Rect = Dc->Clip;
    return 1;
}

int GetRgnBox(HRGN Region, LPRECT Rect)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FRegion* const Box = As<FRegion>(Region, EKind::Region);
    if (Box == nullptr || Rect == nullptr)
        return ERROR;
    *Rect = Box->Rect;
    return IsRectEmpty(&Box->Rect) ? NULLREGION : SIMPLEREGION;
}
