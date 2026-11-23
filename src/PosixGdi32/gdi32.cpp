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
// not call are unused here.
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "vendor/stb_truetype.h"
#pragma GCC diagnostic pop

#include <algorithm>
#include <cmath>
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

// A font file in memory (mapped), one per file and face index.
struct FFace
{
    void* Mapping = nullptr;
    std::size_t Size = 0;
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

FMapped MapFile(const std::string& Path)
{
    FMapped Out;
    int const File = open(Path.c_str(), O_RDONLY | O_CLOEXEC);
    if (File < 0)
        return Out;
    struct stat Info;
    if (fstat(File, &Info) == 0 && Info.st_size > 0)
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
    int Count = stbtt_GetNumberOfFonts(Data);
    if (Count <= 0)
        Count = 0;
    for (int Index = 0; Index < Count; ++Index)
    {
        int const Offset = stbtt_GetFontOffsetForIndex(Data, Index);
        stbtt_fontinfo Info;
        if (Offset < 0 || !stbtt_InitFont(&Info, Data, Offset))
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
        else if (HasFontExtension(Name))
            CatalogFile(Path);
    }
    closedir(Directory);
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
        Folders.push_back(Folder + "/../DATA/FONTS");
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
    FMapped const File = MapFile(Path);
    if (File.Data == nullptr)
        return nullptr;
    std::shared_ptr<FFace> Face(new FFace(),
                                [](FFace* Gone)
                                {
                                    if (Gone->Mapping != nullptr)
                                        munmap(Gone->Mapping, Gone->Size);
                                    delete Gone;
                                });
    Face->Mapping = File.Data;
    Face->Size = File.Size;
    const unsigned char* const Data = static_cast<const unsigned char*>(File.Data);
    int const Offset = stbtt_GetFontOffsetForIndex(Data, Index);
    if (Offset < 0 || !stbtt_InitFont(&Face->Info, Data, Offset))
        return nullptr;
    stbtt_fontinfo& Info = Face->Info;
    if (std::uint32_t const Head = stbtt__find_table(Info.data, Info.fontstart, "head"))
        Face->UnitsPerEm = ReadU16(Info.data + Head + 18);
    if (std::uint32_t const Hhea = stbtt__find_table(Info.data, Info.fontstart, "hhea"))
    {
        Face->HheaAscent = ReadS16(Info.data + Hhea + 4);
        Face->HheaDescent = -ReadS16(Info.data + Hhea + 6);
        Face->HheaLineGap = ReadS16(Info.data + Hhea + 8);
        Face->AdvanceMax = ReadU16(Info.data + Hhea + 10);
    }
    Face->WinAscent = Face->HheaAscent;
    Face->WinDescent = Face->HheaDescent;
    if (std::uint32_t const Os2 = stbtt__find_table(Info.data, Info.fontstart, "OS/2"))
    {
        Face->AverageWidth = ReadS16(Info.data + Os2 + 2);
        int const WeightClass = ReadU16(Info.data + Os2 + 4);
        int const Selection = ReadU16(Info.data + Os2 + 62);
        Face->WinAscent = ReadU16(Info.data + Os2 + 74);
        Face->WinDescent = ReadU16(Info.data + Os2 + 76);
        Face->bBold = WeightClass >= 600 || (Selection & 0x20) != 0;
        Face->bItalic = (Selection & 0x01) != 0;
    }
    Face->Vdmx = stbtt__find_table(Info.data, Info.fontstart, "VDMX");
    if (std::uint32_t const Post = stbtt__find_table(Info.data, Info.fontstart, "post"))
        Face->bFixedPitch =
            ReadU16(Info.data + Post + 12) != 0 || ReadU16(Info.data + Post + 14) != 0;
    if (Face->AverageWidth <= 0)
    {
        int Advance = 0;
        int Bearing = 0;
        stbtt_GetGlyphHMetrics(&Info, stbtt_FindGlyphIndex(&Info, 'x'), &Advance, &Bearing);
        Face->AverageWidth = Advance;
    }
    S.Faces[Key] = Face;
    return Face;
}

// The best face of a family for the style asked: the exact style, then the regular face.
const FState::FCatalogEntry* FindInFamily(const std::string& Family, bool bBold, bool bItalic)
{
    auto const Range = State().Catalog.equal_range(Family);
    const FState::FCatalogEntry* Best = nullptr;
    int BestScore = -1;
    for (auto It = Range.first; It != Range.second; ++It)
    {
        int const Score =
            (It->second.bBold == bBold ? 2 : 0) + (It->second.bItalic == bItalic ? 1 : 0);
        if (Score > BestScore)
        {
            Best = &It->second;
            BestScore = Score;
        }
    }
    return Best;
}

const FState::FCatalogEntry* FindFace(const char* FaceName, bool bBold, bool bItalic)
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
    for (const std::string& Family : Families)
        if (const FState::FCatalogEntry* const Found = FindInFamily(Family, bBold, bItalic))
            return Found;
    return State().Catalog.empty() ? nullptr : &State().Catalog.begin()->second;
}

// The VDMX table's yMax / yMin (pixels) for a pixel height, as GDI takes a face's ascent and
// descent from it when the face has one: by em height (bCell false) or the tallest entry whose
// yMax - yMin fits a cell height. Uses the 1:1 aspect ratio group (or the "any" one).
bool VdmxExtent(const FFace& Face, int Pixels, bool bCell, int* Ascent, int* Descent)
{
    if (Face.Vdmx == 0)
        return false;
    const unsigned char* const Table = Face.Info.data + Face.Vdmx;
    int const Ratios = ReadU16(Table + 4);
    for (int Ratio = 0; Ratio < Ratios; ++Ratio)
    {
        const unsigned char* const Range = Table + 6 + Ratio * 4;
        int const XRatio = Range[1];
        int const YStart = Range[2];
        int const YEnd = Range[3];
        if (!(XRatio == 0 || (YStart <= XRatio && XRatio <= YEnd)))
            continue;
        const unsigned char* const Group = Table + ReadU16(Table + 6 + Ratios * 4 + Ratio * 2);
        int const Records = ReadU16(Group);
        bool bFound = false;
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
                bFound = true;
                if (!bCell)
                    break;
            }
        }
        return bFound;
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
    Font.ScaleX = Font.Scale;
    if (Font.Log.lfWidth > 0 && Face.AverageWidth > 0)
        Font.ScaleX = static_cast<float>(Font.Log.lfWidth) / static_cast<float>(Face.AverageWidth);
    Font.Ascent = static_cast<LONG>(std::lround(Face.WinAscent * Font.Scale));
    Font.Descent = static_cast<LONG>(std::lround(Face.WinDescent * Font.Scale));
    int VdmxAscent = 0;
    int VdmxDescent = 0;
    if (VdmxExtent(Face, Height < 0 ? -Height : Height, Height > 0, &VdmxAscent, &VdmxDescent))
    {
        Font.Ascent = VdmxAscent;
        Font.Descent = VdmxDescent;
    }
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
    const FState::FCatalogEntry* const Entry = FindFace(Log.lfFaceName, bBold, bItalic);
    std::shared_ptr<FFace> Face = Entry != nullptr ? OpenFace(Entry->Path, Entry->Index) : nullptr;
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
    stbtt_GetGlyphHMetrics(&Font.Face->Info,
                           stbtt_FindGlyphIndex(&Font.Face->Info, static_cast<int>(Character)),
                           &Advance, &Bearing);
    return static_cast<int>(std::lround(Advance * Font.ScaleX)) + (Font.bEmbolden ? 1 : 0);
}

// The characters of a wide string: wchar_t is UTF-32 here, and a UTF-16 surrogate pair (text
// from the wire or a resource) is one character.
std::vector<std::uint32_t> Characters(const wchar_t* Text, int Count)
{
    std::vector<std::uint32_t> Out;
    if (Text == nullptr)
        return Out;
    if (Count < 0)
        Count = static_cast<int>(std::wcslen(Text));
    for (int Index = 0; Index < Count; ++Index)
    {
        std::uint32_t Code = static_cast<std::uint32_t>(Text[Index]);
        if (Code >= 0xD800 && Code <= 0xDBFF && Index + 1 < Count)
        {
            std::uint32_t const Low = static_cast<std::uint32_t>(Text[Index + 1]);
            if (Low >= 0xDC00 && Low <= 0xDFFF)
            {
                Code = 0x10000 + ((Code - 0xD800) << 10) + (Low - 0xDC00);
                ++Index;
            }
        }
        Out.push_back(Code);
    }
    return Out;
}

std::vector<std::uint32_t> Characters(LPCSTR Text, int Count)
{
    std::vector<std::uint32_t> Out;
    if (Text == nullptr)
        return Out;
    if (Count < 0)
        Count = static_cast<int>(std::strlen(Text));
    for (int Index = 0; Index < Count; ++Index)
        Out.push_back(static_cast<unsigned char>(Text[Index]));
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
    int const Glyph = stbtt_FindGlyphIndex(&Info, static_cast<int>(Character));
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
              const std::vector<std::uint32_t>& Text, const int* Dx)
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

    std::vector<int> Advances(Text.size());
    int Extent = 0;
    for (std::size_t Index = 0; Index < Text.size(); ++Index)
    {
        Advances[Index] = Dx != nullptr ? Dx[Index] : AdvanceOf(*Font, Text[Index]);
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
        DrawGlyph(Bitmap, *Font, Text[Index], Pen, Baseline, Pixel, Area);
        Pen += Advances[Index];
    }
    return TRUE;
}

BOOL Extent(FDc& Dc, const std::vector<std::uint32_t>& Text, int MaxExtent, int* Fit, int* Extents,
            LPSIZE Size)
{
    FFont* const Font = FontOf(Dc);
    if (Font == nullptr || Size == nullptr)
        return FALSE;
    int Total = 0;
    int Fitted = 0;
    for (std::size_t Index = 0; Index < Text.size(); ++Index)
    {
        Total += AdvanceOf(*Font, Text[Index]);
        if (Extents != nullptr)
            Extents[Index] = Total;
        if (MaxExtent < 0 || Total <= MaxExtent)
            Fitted = static_cast<int>(Index) + 1;
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

FDc* MakeDc()
{
    FState& S = State();
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
    for (int Row = 0; Row < std::abs(Height); ++Row)
    {
        int const ToY = Y + (Height > 0 ? Row : -Row);
        if (ToY < Area.top || ToY >= Area.bottom)
            continue;
        int const FromY = SourceY + Row * SourceHeight / Height;
        if (FromY < 0 || FromY >= From.Height)
            continue;
        for (int Column = 0; Column < std::abs(Width); ++Column)
        {
            int const ToX = X + (Width > 0 ? Column : -Column);
            if (ToX < Area.left || ToX >= Area.right)
                continue;
            int const FromX = SourceX + Column * SourceWidth / Width;
            if (FromX < 0 || FromX >= From.Width)
                continue;
            To.Pixels[static_cast<std::size_t>(ToY) * To.Width + ToX] =
                From.Pixels[static_cast<std::size_t>(FromY) * From.Width + FromX];
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
    return reinterpret_cast<HDC>(MakeDc());
}

int ReleaseDC(HWND, HDC Dc)
{
    return DeleteDC(Dc) ? 1 : 0;
}

HDC CreateCompatibleDC(HDC)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    return reinterpret_cast<HDC>(MakeDc());
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
        SelectClipRgn(Handle, static_cast<HRGN>(Object));
        return reinterpret_cast<HGDIOBJ>(static_cast<std::uintptr_t>(SIMPLEREGION));
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
    if (Font == nullptr || Widths == nullptr || Last < First)
        return FALSE;
    for (UINT Character = First; Character <= Last; ++Character)
        Widths[Character - First] = AdvanceOf(*Font, Character);
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

BOOL ExtTextOutW(HDC Handle, int X, int Y, UINT Options, const RECT* Rect, const wchar_t* Text,
                 UINT Count, const int* Dx)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr)
        return FALSE;
    return DrawText(*Dc, X, Y, Options, Rect, Characters(Text, static_cast<int>(Count)), Dx);
}

BOOL ExtTextOutA(HDC Handle, int X, int Y, UINT Options, const RECT* Rect, LPCSTR Text, UINT Count,
                 const int* Dx)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const Dc = As<FDc>(Handle, EKind::Dc);
    if (Dc == nullptr)
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
    int const Width = std::min<int>(Bitmap->Width, std::abs(Header.biWidth));
    int const Rows = std::abs(Header.biHeight);
    bool const bTopDown = Header.biHeight < 0;
    std::size_t const Stride =
        ((static_cast<std::size_t>(std::abs(Header.biWidth)) * BitCount + 31) / 32) * 4;
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
        if (DibRow >= Rows)
            break;
        // The DIB's row DibRow is the bitmap's row from the top (top-down) or bottom.
        int const SourceRow = bTopDown ? DibRow : Rows - 1 - DibRow;
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

BOOL BitBlt(HDC Target, int X, int Y, int Width, int Height, HDC Source, int SourceX, int SourceY,
            DWORD)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const To = As<FDc>(Target, EKind::Dc);
    FDc* const From = As<FDc>(Source, EKind::Dc);
    if (To == nullptr || From == nullptr)
        return FALSE;
    return Blit(*To, X, Y, Width, Height, *From, SourceX, SourceY, Width, Height);
}

BOOL StretchBlt(HDC Target, int X, int Y, int Width, int Height, HDC Source, int SourceX,
                int SourceY, int SourceWidth, int SourceHeight, DWORD)
{
    std::lock_guard<std::recursive_mutex> Guard(State().Lock);
    FDc* const To = As<FDc>(Target, EKind::Dc);
    FDc* const From = As<FDc>(Source, EKind::Dc);
    if (To == nullptr || From == nullptr)
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
