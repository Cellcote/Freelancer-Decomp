// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): checks of gdi32 on Linux and macOS (src/PosixGdi32): memory DCs and bitmaps,
// fonts found by face name and their GDI metrics, ExtTextOutW's alignment, background and
// clipping, GetDIBits' formats, blits, brushes and regions, the selection rules, and font
// folders holding files that are not fonts. Not part of retail; built only by the non-Windows
// build, with ctest. Font folder checks use fonts the test builds; the checks of Windows' exact
// Arial metrics run on macOS, on a copy of its Arial (a known face, with VDMX); the others hold
// for whichever face the fallbacks find.

#include <windows.h>

#include <algorithm>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

// Windows' TEXTMETRICW: 16-bit characters, so tmItalic at byte 52 and 60 bytes in all.
static_assert(offsetof(TEXTMETRICW, tmFirstChar) == 44 && offsetof(TEXTMETRICW, tmItalic) == 52 &&
                  sizeof(TEXTMETRICW) == 60,
              "TEXTMETRICW has Windows' layout");

namespace
{
int GFailures = 0;

void Check(bool bOk, const char* What)
{
    std::printf("%s: %s\n", bOk ? "ok  " : "FAIL", What);
    if (!bOk)
        ++GFailures;
}

HFONT MakeFont(const char* Face, LONG Height, LONG Weight = FW_NORMAL)
{
    LOGFONTA Log = {};
    Log.lfHeight = Height;
    Log.lfWeight = Weight;
    Log.lfQuality = ANTIALIASED_QUALITY;
    std::strncpy(Log.lfFaceName, Face, LF_FACESIZE - 1);
    return CreateFontIndirectA(&Log);
}

// The 32-bit pixels of a DC's bitmap, top row first.
std::vector<unsigned> Pixels(HDC Dc, HBITMAP Bitmap, int Width, int Height)
{
    BITMAPINFO Info = {};
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = Width;
    Info.bmiHeader.biHeight = -Height;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned> Out(static_cast<std::size_t>(Width) * Height);
    GetDIBits(Dc, Bitmap, 0, static_cast<UINT>(Height), Out.data(), &Info, DIB_RGB_COLORS);
    return Out;
}

// Fills a DC's pixel with a colour.
void Plot(HDC Dc, int X, int Y, COLORREF Color)
{
    HBRUSH const Brush = CreateSolidBrush(Color);
    RECT const Pixel = {X, Y, X + 1, Y + 1};
    FillRect(Dc, &Pixel, Brush);
    DeleteObject(Brush);
}

void Put16(std::vector<char>& Out, unsigned Value)
{
    Out.push_back(static_cast<char>((Value >> 8) & 0xFF));
    Out.push_back(static_cast<char>(Value & 0xFF));
}

void Put32(std::vector<char>& Out, unsigned Value)
{
    Put16(Out, Value >> 16);
    Put16(Out, Value & 0xFFFF);
}

// A minimal TrueType font: 1000 units per em; 'A' maps to glyph 1, U+1D49C (with FullMap) to
// glyph 2, a 500 x 700 square with an advance of 800 units.
struct FFontSpec
{
    const char* Family;
    unsigned Advance = 600; // glyph 1's
    // Glyph 1: 0 the square; -1 a single off-curve point (its contour starts on the glyph's last
    // point); n > 0 a composite of n references to glyph 2.
    int Glyph = 0;
    bool bShortCmap = false; // the format 4 subtable's length 2 bytes short of its arrays
    bool bFullMap = false; // a format 12 subtable recorded before the format 4 one
};

void PutSquare(std::vector<char>& Glyf)
{
    Put16(Glyf, 1), Put16(Glyf, 0), Put16(Glyf, 0), Put16(Glyf, 500), Put16(Glyf, 700);
    Put16(Glyf, 3), Put16(Glyf, 0);
    for (int Point = 0; Point < 4; ++Point)
        Glyf.push_back(1); // on-curve, two-byte x and y
    for (unsigned Delta : {0u, 500u, 0u, 0x10000u - 500u})
        Put16(Glyf, Delta);
    for (unsigned Delta : {0u, 0u, 700u, 0u})
        Put16(Glyf, Delta);
}

std::vector<char> BuildFont(const FFontSpec& Spec)
{
    std::vector<char> Head(54, 0);
    Head[1] = 1; // version 1.0
    Head[12] = 0x5F, Head[13] = 0x0F, Head[14] = 0x3C, Head[15] = 0xF5; // magic
    Head[18] = 1000 >> 8, Head[19] = static_cast<char>(1000 & 0xFF); // unitsPerEm
    std::vector<char> Hhea;
    Put32(Hhea, 0x00010000);
    Put16(Hhea, 800); // ascender
    Put16(Hhea, 0x10000 - 200); // descender
    Put16(Hhea, 0); // lineGap
    Put16(Hhea, std::max(Spec.Advance, 800u)); // advanceWidthMax
    Hhea.resize(34, 0);
    Put16(Hhea, 3); // numberOfHMetrics
    std::vector<char> Maxp;
    Put32(Maxp, 0x00005000);
    Put16(Maxp, 3);
    std::vector<char> Hmtx;
    for (unsigned Advance : {500u, Spec.Advance, 800u})
        Put16(Hmtx, Advance), Put16(Hmtx, 0);
    std::vector<char> Format4;
    for (unsigned Value : {4u, Spec.bShortCmap ? 30u : 32u, 0u, 4u, 4u, 1u, 0u, 0x41u, 0xFFFFu, 0u,
                           0x41u, 0xFFFFu, (1u - 0x41u) & 0xFFFFu, 1u, 0u, 0u})
        Put16(Format4, Value);
    std::vector<char> Cmap;
    Put16(Cmap, 0);
    if (Spec.bFullMap)
    {
        Put16(Cmap, 2);
        Put16(Cmap, 0), Put16(Cmap, 4), Put32(Cmap, 20); // Unicode full: format 12, first
        Put16(Cmap, 3), Put16(Cmap, 1), Put32(Cmap, 60); // Windows BMP: format 4, last
        Put16(Cmap, 12), Put16(Cmap, 0), Put32(Cmap, 40), Put32(Cmap, 0), Put32(Cmap, 2);
        for (unsigned Value : {0x41u, 0x41u, 1u, 0x1D49Cu, 0x1D49Cu, 2u})
            Put32(Cmap, Value);
    }
    else
    {
        Put16(Cmap, 1);
        Put16(Cmap, 3), Put16(Cmap, 1), Put32(Cmap, 12);
    }
    Cmap.insert(Cmap.end(), Format4.begin(), Format4.end());
    std::vector<char> Glyf;
    if (Spec.Glyph < 0)
    {
        Put16(Glyf, 1), Put16(Glyf, 0), Put16(Glyf, 0), Put16(Glyf, 500), Put16(Glyf, 700);
        Put16(Glyf, 0), Put16(Glyf, 0); // one point; no instructions
        Glyf.push_back(0); // off-curve, two-byte x and y
        Put16(Glyf, 250), Put16(Glyf, 350);
        Glyf.push_back(0);
    }
    else if (Spec.Glyph > 0)
    {
        Put16(Glyf, 0xFFFF), Put16(Glyf, 0), Put16(Glyf, 0), Put16(Glyf, 500), Put16(Glyf, 700);
        for (int Component = 0; Component < Spec.Glyph; ++Component)
        {
            Put16(Glyf, 0x0002 | (Component + 1 < Spec.Glyph ? 0x0020 : 0)); // x/y bytes, more
            Put16(Glyf, 2);
            Put16(Glyf, 0);
        }
    }
    else
        PutSquare(Glyf);
    std::size_t const Second = Glyf.size();
    PutSquare(Glyf);
    std::vector<char> Loca;
    for (std::size_t Offset : {std::size_t{0}, std::size_t{0}, Second, Glyf.size()})
        Put16(Loca, static_cast<unsigned>(Offset / 2));
    std::vector<char> Name;
    std::size_t const Length = std::strlen(Spec.Family) * 2;
    Put16(Name, 0), Put16(Name, 1), Put16(Name, 18);
    for (unsigned Value : {3u, 1u, 0x409u, 1u, static_cast<unsigned>(Length), 0u})
        Put16(Name, Value);
    for (const char* Character = Spec.Family; *Character != 0; ++Character)
        Put16(Name, static_cast<unsigned char>(*Character));
    std::vector<std::pair<const char*, std::vector<char>>> const Tables = {
        {"cmap", Cmap}, {"glyf", Glyf}, {"head", Head}, {"hhea", Hhea},
        {"hmtx", Hmtx}, {"loca", Loca}, {"maxp", Maxp}, {"name", Name}};
    std::vector<char> Out;
    Put32(Out, 0x00010000);
    Put16(Out, static_cast<unsigned>(Tables.size()));
    Put16(Out, 0), Put16(Out, 0), Put16(Out, 0);
    std::size_t Offset = 12 + 16 * Tables.size();
    for (const auto& Table : Tables)
    {
        Out.insert(Out.end(), Table.first, Table.first + 4);
        Put32(Out, 0);
        Put32(Out, static_cast<unsigned>(Offset));
        Put32(Out, static_cast<unsigned>(Table.second.size()));
        Offset += (Table.second.size() + 3) & ~std::size_t{3};
    }
    for (const auto& Table : Tables)
    {
        Out.insert(Out.end(), Table.second.begin(), Table.second.end());
        Out.resize((Out.size() + 3) & ~std::size_t{3}, 0);
    }
    return Out;
}

#if defined(__APPLE__)
std::vector<char> ReadFile(const char* Path)
{
    std::ifstream File(Path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(File),
                             std::istreambuf_iterator<char>());
}
#endif

bool WriteFile(const std::string& Path, const std::vector<char>& Data, std::size_t Size)
{
    std::ofstream File(Path, std::ios::binary);
    File.write(Data.data(), static_cast<std::streamsize>(std::min(Size, Data.size())));
    File.close();
    return !Data.empty() && File.good();
}

// The extent of a string in a font.
LONG ExtentIn(HFONT Font, const wchar_t* Text, int Count)
{
    HDC const Probe = CreateCompatibleDC(nullptr);
    HGDIOBJ const Previous = SelectObject(Probe, Font);
    SIZE Size = {};
    GetTextExtentPoint32W(Probe, Text, Count, &Size);
    SelectObject(Probe, Previous);
    DeleteDC(Probe);
    return Size.cx;
}

LONG AdvanceOfA(HFONT Font)
{
    return ExtentIn(Font, L"A", 1);
}

// Whether drawing 'A' in a font lights any pixel.
bool DrawsA(HFONT Font)
{
    HDC const Probe = CreateCompatibleDC(nullptr);
    HBITMAP const Target = CreateCompatibleBitmap(nullptr, 128, 128);
    HGDIOBJ const PreviousBitmap = SelectObject(Probe, Target);
    HGDIOBJ const PreviousFont = SelectObject(Probe, Font);
    SetBkMode(Probe, TRANSPARENT);
    SetTextColor(Probe, RGB(255, 255, 255));
    ExtTextOutW(Probe, 0, 0, 0, nullptr, L"A", 1, nullptr);
    bool bLit = false;
    BITMAPINFO Info = {};
    Info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info.bmiHeader.biWidth = 128;
    Info.bmiHeader.biHeight = -128;
    Info.bmiHeader.biPlanes = 1;
    Info.bmiHeader.biBitCount = 32;
    Info.bmiHeader.biCompression = BI_RGB;
    std::vector<unsigned> Image(128 * 128);
    GetDIBits(Probe, Target, 0, 128, Image.data(), &Info, DIB_RGB_COLORS);
    for (unsigned Pixel : Image)
        bLit = bLit || (Pixel & 0xFFFFFF) != 0;
    SelectObject(Probe, PreviousFont);
    SelectObject(Probe, PreviousBitmap);
    DeleteObject(Target);
    DeleteDC(Probe);
    return bLit;
}

// Whether any pixel inside / outside a rectangle is lit.
bool AnyLit(const std::vector<unsigned>& Image, int Width, const RECT& Area, bool bInside)
{
    for (std::size_t Index = 0; Index < Image.size(); ++Index)
    {
        int const X = static_cast<int>(Index) % Width;
        int const Y = static_cast<int>(Index) / Width;
        bool const bIn = X >= Area.left && X < Area.right && Y >= Area.top && Y < Area.bottom;
        if (bIn == bInside && (Image[Index] & 0xFFFFFF) != 0)
            return true;
    }
    return false;
}
} // namespace

int main()
{
    // Before any font is made (the catalogue is built once), three font folders searched first,
    // in order, holding only fonts this test builds (and, on macOS, a copy of the system's
    // Arial): First has a FIFO named like a font (no writer: opening it blocks), a font cut
    // short, and a face that goes away after the catalogue lists it; Second the same family's
    // other file, and faces made to check the outline, cmap and hmtx rules.
    char FolderTemplate[] = "/tmp/gdi32_tests.XXXXXX";
    std::string const Folder = mkdtemp(FolderTemplate) != nullptr ? FolderTemplate : "";
    if (Folder.empty())
    {
        std::printf("FAIL: no temporary folder\n");
        return 1;
    }
    std::string const First = Folder + "/first";
    std::string const Second = Folder + "/second";
    std::string const Third = Folder + "/third";
    std::vector<std::string> const Files = {
        First + "/Pipe.ttf",  First + "/Broken.ttf", First + "/Gone.ttf", Second + "/Kept.ttf",
        Second + "/Bad.ttf",  Second + "/Many.ttf",  Second + "/Few.ttf", Second + "/Short.ttf",
        Second + "/Wide.ttf", Second + "/Full.ttf",  Third + "/Arial.ttf"};
    // Each step of the setup must hold, or what the checks below find is not what they test.
    bool bSetUp =
        mkdir(First.c_str(), 0700) == 0 && mkdir(Second.c_str(), 0700) == 0 &&
        mkdir(Third.c_str(), 0700) == 0 &&
        setenv("FL_GDI32_FONT_PATH", (First + ":" + Second + ":" + Third).c_str(), 1) == 0 &&
        mkfifo(Files[0].c_str(), 0600) == 0;
    std::vector<char> const Gone = BuildFont({"Gdi Test Face", 600});
    bSetUp = bSetUp && WriteFile(Files[1], Gone, 200) && WriteFile(Files[2], Gone, Gone.size());
    std::vector<std::pair<std::string, FFontSpec>> const Fixtures = {
        {Files[3], {"Gdi Test Face", 700}},
        {Files[4], {"Gdi Test Bad", 500, -1}},
        {Files[5], {"Gdi Test Many", 500, 300}},
        {Files[6], {"Gdi Test Few", 500, 4}},
        {Files[7], {"Gdi Test Short", 500, 0, true}},
        {Files[8], {"Gdi Test Wide", 40000}},
        {Files[9], {"Gdi Test Full", 500, 0, false, true}}};
    for (const auto& Fixture : Fixtures)
    {
        std::vector<char> const Data = BuildFont(Fixture.second);
        bSetUp = bSetUp && WriteFile(Fixture.first, Data, Data.size());
    }
#if defined(__APPLE__)
    // macOS's Arial (Supplemental/Arial.ttf) is a known face with a VDMX table, as Windows' is;
    // this copy is found before any other Arial.
    std::vector<char> const Arial = ReadFile("/System/Library/Fonts/Supplemental/Arial.ttf");
    bSetUp = bSetUp && WriteFile(Files[10], Arial, Arial.size());
#endif
    Check(bSetUp, "the font folders are set up (folders, FL_GDI32_FONT_PATH, FIFO, font files)");

    HFONT const Before = MakeFont("Gdi Test Face", -100);
    Check(Before != nullptr && AdvanceOfA(Before) == 60,
          "the catalogue skips a FIFO and a cut-short font, and takes the first folder's face");
    DeleteObject(Before);
    Check(std::remove(Files[2].c_str()) == 0, "the first folder's face is removed");
    {
        HDC const Probe = CreateCompatibleDC(nullptr);
        HFONT const After = MakeFont("Gdi Test Face", -100);
        char Name[LF_FACESIZE] = {};
        HGDIOBJ const Previous = SelectObject(Probe, After);
        Check(After != nullptr && GetTextFaceA(Probe, LF_FACESIZE, Name) > 0 &&
                  std::strcmp(Name, "Gdi Test Face") == 0 && AdvanceOfA(After) == 70,
              "a catalogued face whose file is gone gives way to the next one of its family");
        SelectObject(Probe, Previous);
        DeleteObject(After);
        DeleteDC(Probe);
    }
    {
        // Drawn, the glyph is read to its last point and no further (ASan checks).
        HFONT const Odd = MakeFont("Gdi Test Bad", -100);
        DrawsA(Odd);
        Check(Odd != nullptr,
              "a contour that starts on the glyph's last, off-curve point is drawn");
        DeleteObject(Odd);
        HFONT const Many = MakeFont("Gdi Test Many", -100);
        HFONT const Few = MakeFont("Gdi Test Few", -100);
        Check(!DrawsA(Many) && DrawsA(Few),
              "a composite past the outline budget draws nothing; a small one draws");
        DeleteObject(Many);
        DeleteObject(Few);
        HDC const Probe = CreateCompatibleDC(nullptr);
        HFONT const Short = MakeFont("Gdi Test Short", -100);
        HGDIOBJ const Previous = SelectObject(Probe, Short);
        char Name[LF_FACESIZE] = {};
        GetTextFaceA(Probe, LF_FACESIZE, Name);
        Check(std::strcmp(Name, "Gdi Test Short") != 0,
              "a face whose cmap arrays run past the subtable's length is not used");
        SelectObject(Probe, Previous);
        DeleteObject(Short);
        DeleteDC(Probe);
        HFONT const Wide = MakeFont("Gdi Test Wide", -100);
        Check(AdvanceOfA(Wide) == 4000, "an advance past 32767 units is unsigned");
        DeleteObject(Wide);
        HFONT const Full = MakeFont("Gdi Test Full", -100);
        wchar_t const Script[] = {0xD835, 0xDC9C};
        Check(ExtentIn(Full, Script, 2) == 80 && AdvanceOfA(Full) == 50,
              "a full Unicode map is used before a BMP one recorded after it");
        DeleteObject(Full);
    }

    HDC const Screen = GetDC(nullptr);
    HDC const Dc = CreateCompatibleDC(Screen);
    HBITMAP const Bitmap = CreateCompatibleBitmap(Screen, 128, 64);
    ReleaseDC(nullptr, Screen);
    Check(Dc != nullptr && Bitmap != nullptr, "a memory DC and a compatible bitmap");
    HGDIOBJ const Original = SelectObject(Dc, Bitmap);
    Check(Original != nullptr, "SelectObject answers the DC's first bitmap");

    BITMAPINFO Header = {};
    Header.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Check(GetDIBits(Dc, Bitmap, 0, 1, nullptr, &Header, DIB_RGB_COLORS) != 0 &&
              Header.bmiHeader.biWidth == 128 && Header.bmiHeader.biHeight == 64 &&
              Header.bmiHeader.biBitCount == 32,
          "GetDIBits with no bits and no bit count describes the bitmap");

    HFONT const Font = MakeFont("Arial", -16);
    Check(Font != nullptr, "CreateFontIndirectA finds a face for Arial (or a fallback)");
    HGDIOBJ const SystemFont = SelectObject(Dc, Font);
    Check(SystemFont != nullptr && SystemFont != Font, "SelectObject answers the system font");
    TEXTMETRICA Metrics = {};
    Check(GetTextMetricsA(Dc, &Metrics) && Metrics.tmHeight > 12 &&
              Metrics.tmHeight == Metrics.tmAscent + Metrics.tmDescent &&
              Metrics.tmAveCharWidth > 0 && Metrics.tmMaxCharWidth >= Metrics.tmAveCharWidth,
          "GetTextMetricsA: the cell is ascent + descent, with widths");
    char FaceName[LF_FACESIZE] = {};
    Check(GetTextFaceA(Dc, LF_FACESIZE, FaceName) > 1, "GetTextFaceA names the realized face");
#if defined(__APPLE__)
    // The copy of macOS's Arial in the third folder.
    Check(std::strcmp(FaceName, "Arial") == 0, "Arial is found by name on macOS");
    Check(Metrics.tmHeight == 18 && Metrics.tmAscent == 15 && Metrics.tmDescent == 3 &&
              Metrics.tmInternalLeading == 2,
          "Arial at -16: Windows' 18 / 15 / 3 (VDMX)");
#endif
    {
        // A cell height past the VDMX entries is scaled, not the last entry's cell.
        HDC const Big = CreateCompatibleDC(nullptr);
        HFONT const Tall = MakeFont("Arial", 600);
        HGDIOBJ const Previous = SelectObject(Big, Tall);
        TEXTMETRICA TallMetrics = {};
        Check(GetTextMetricsA(Big, &TallMetrics) && TallMetrics.tmHeight >= 599 &&
                  TallMetrics.tmHeight <= 601,
              "a positive lfHeight of 600 is a 600-pixel cell");
        HFONT const Small = MakeFont("Arial", 20);
        SelectObject(Big, Small);
        TEXTMETRICA SmallMetrics = {};
        Check(GetTextMetricsA(Big, &SmallMetrics) && SmallMetrics.tmHeight <= 20 &&
                  SmallMetrics.tmHeight > 15,
              "a positive lfHeight of 20 is a cell of at most 20 pixels");
        SelectObject(Big, Previous);
        DeleteObject(Tall);
        DeleteObject(Small);
        DeleteDC(Big);
    }

    int Widths[3] = {};
    Check(GetCharWidthW(Dc, 'A', 'C', Widths) && Widths[0] > 0 && Widths[1] > 0 && Widths[2] > 0,
          "GetCharWidthW gives each character's advance");
    SIZE Size = {};
    Check(GetTextExtentPoint32W(Dc, L"ABC", 3, &Size) &&
              Size.cx == Widths[0] + Widths[1] + Widths[2] && Size.cy == Metrics.tmHeight,
          "GetTextExtentPoint32W is the advances' sum by the cell height");
    int Fit = 0;
    int Extents[3] = {};
    Check(GetTextExtentExPointW(Dc, L"ABC", 3, Widths[0] + Widths[1], &Fit, Extents, &Size) &&
              Fit == 2 && Extents[0] == Widths[0] && Extents[1] == Widths[0] + Widths[1] &&
              Size.cx == Widths[0] + Widths[1] + Widths[2],
          "GetTextExtentExPointW counts the characters that fit, with their extents");
    // A surrogate pair (U+1D49C) is two units of the count, the fit and the extents.
    wchar_t const Pair[] = {0xD835, 0xDC9C, L' ', L'x'};
    int PairExtents[4] = {};
    Check(GetTextExtentExPointW(Dc, Pair, 4, 1 << 20, &Fit, PairExtents, &Size) && Fit == 4 &&
              PairExtents[0] == PairExtents[1] && PairExtents[3] == Size.cx,
          "a surrogate pair's two units share its extent");
    Check(GetTextExtentExPointW(Dc, Pair, 4, PairExtents[2], &Fit, nullptr, &Size) && Fit == 3,
          "the fit count is in UTF-16 units");
    int Guarded[2] = {-7, -7};
    Check(!GetCharWidthW(Dc, 0xFFFFFFFFu, 0xFFFFFFFFu, Guarded) && Guarded[0] == -7 &&
              Guarded[1] == -7,
          "GetCharWidthW refuses a character past Unicode");
    Check(GetCharWidthW(Dc, 'B', 'B', Guarded) && Guarded[0] == Widths[1] && Guarded[1] == -7,
          "GetCharWidthW fills one slot for one character");

    // White text on a transparent background, from the top left.
    SetTextAlign(Dc, TA_LEFT | TA_TOP);
    SetBkMode(Dc, TRANSPARENT);
    SetTextColor(Dc, RGB(255, 255, 255));
    SIZE HiSize = {};
    Check(GetTextExtentPoint32W(Dc, L"Hi", 2, &HiSize) && HiSize.cx > 0, "\"Hi\" is measured");
    Check(ExtTextOutW(Dc, 4, 4, 0, nullptr, L"Hi", 2, nullptr), "ExtTextOutW draws");
    std::vector<unsigned> Image = Pixels(Dc, Bitmap, 128, 64);
    RECT const Cell = {4, 4, 4 + HiSize.cx, 4 + Metrics.tmHeight};
    Check(AnyLit(Image, 128, Cell, true) && !AnyLit(Image, 128, Cell, false),
          "the glyphs are inside the text's cell and nothing outside it");

    // A clip region keeps the drawing inside it; an opaque background fills the cell.
    RECT Black = {0, 0, 128, 64};
    HBRUSH const Brush = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
    FillRect(Dc, &Black, Brush);
    HRGN const Clip = CreateRectRgn(0, 0, 10, 64);
    Check(SelectClipRgn(Dc, Clip) == SIMPLEREGION, "SelectClipRgn takes a rectangle");
    SetBkMode(Dc, OPAQUE);
    SetBkColor(Dc, RGB(0, 0, 255));
    SetTextAlign(Dc, TA_LEFT | TA_BASELINE);
    ExtTextOutW(Dc, 4, 4 + Metrics.tmAscent, 0, nullptr, L"Hi", 2, nullptr);
    Image = Pixels(Dc, Bitmap, 128, 64);
    Check(AnyLit(Image, 128, RECT{4, 4, 10, 4 + Metrics.tmHeight}, true) &&
              !AnyLit(Image, 128, RECT{0, 0, 10, 64}, false),
          "the clip region keeps text and background inside it");
    Check(Image[static_cast<std::size_t>(5) * 128 + 5] != 0,
          "TA_BASELINE puts the cell's top at the baseline less the ascent");
    SelectClipRgn(Dc, nullptr);

    // 16-bit 5-5-5 read back, top-down: white is 0x7FFF.
    RECT const White = {0, 0, 1, 1};
    HBRUSH const WhiteBrush = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(Dc, &White, WhiteBrush);
    std::vector<unsigned short> Short(128 * 64);
    BITMAPINFO Info16 = {};
    Info16.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    Info16.bmiHeader.biWidth = 128;
    Info16.bmiHeader.biHeight = -64;
    Info16.bmiHeader.biPlanes = 1;
    Info16.bmiHeader.biBitCount = 16;
    Info16.bmiHeader.biCompression = BI_RGB;
    Check(GetDIBits(Dc, Bitmap, 0, 64, Short.data(), &Info16, DIB_RGB_COLORS) == 64 &&
              Short[0] == 0x7FFF,
          "GetDIBits writes 16-bit 5-5-5 pixels");
    Info16.bmiHeader.biHeight = 64; // bottom-up: the top-left pixel is the last row's first
    GetDIBits(Dc, Bitmap, 0, 64, Short.data(), &Info16, DIB_RGB_COLORS);
    Check(Short[static_cast<std::size_t>(63) * 128] == 0x7FFF, "a positive height is bottom-up");

    {
        // Blits within one 4x1 bitmap: red, green, blue, white.
        HDC const Strip = CreateCompatibleDC(nullptr);
        HDC const Other = CreateCompatibleDC(nullptr);
        HBITMAP const Four = CreateCompatibleBitmap(nullptr, 4, 1);
        HGDIOBJ const StripOriginal = SelectObject(Strip, Four);
        auto const Paint = [&]
        {
            Plot(Strip, 0, 0, RGB(255, 0, 0));
            Plot(Strip, 1, 0, RGB(0, 255, 0));
            Plot(Strip, 2, 0, RGB(0, 0, 255));
            Plot(Strip, 3, 0, RGB(255, 255, 255));
        };
        unsigned const R = 0xFF0000;
        unsigned const G = 0x00FF00;
        unsigned const B = 0x0000FF;
        unsigned const W = 0xFFFFFF;
        Paint();
        BitBlt(Strip, 1, 0, 2, 1, Strip, 0, 0, SRCCOPY);
        Check(Pixels(Strip, Four, 4, 1) == std::vector<unsigned>{R, R, G, W},
              "an overlapping BitBlt copies the source as it was");
        Paint();
        StretchBlt(Strip, 3, 0, -2, 1, Strip, 1, 0, 2, 1, SRCCOPY);
        Check(Pixels(Strip, Four, 4, 1) == std::vector<unsigned>{R, G, B, G},
              "a negative width mirrors the source rectangle's own columns");
        Paint();
        RECT const Outside = {6, 0, 7, 1};
        Check(FillRect(Strip, &Outside, Brush) == 1 &&
                  Pixels(Strip, Four, 4, 1) == std::vector<unsigned>{R, G, B, W},
              "a fill outside the bitmap changes nothing");
        unsigned Untouched[4] = {1, 2, 3, 4};
        BITMAPINFO Line = {};
        Line.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        Line.bmiHeader.biWidth = 4;
        Line.bmiHeader.biHeight = -1;
        Line.bmiHeader.biPlanes = 1;
        Line.bmiHeader.biBitCount = 32;
        Line.bmiHeader.biCompression = BI_RGB;
        Check(GetDIBits(Strip, Four, 0xFFFFFFFFu, 1, Untouched, &Line, DIB_RGB_COLORS) == 0 &&
                  Untouched[0] == 1,
              "GetDIBits copies no scan line from past the bitmap");

        // Only SRCCOPY is drawn; another raster operation fails and changes nothing.
        Check(!BitBlt(Strip, 0, 0, 4, 1, Strip, 0, 0, BLACKNESS) &&
                  Pixels(Strip, Four, 4, 1) == std::vector<unsigned>{R, G, B, W},
              "BitBlt refuses a raster operation it does not draw");
        // Extents of INT_MIN, and a blit far wider than the bitmap, end at once.
        StretchBlt(Strip, 0, 0, INT_MIN, INT_MIN, Strip, 0, 0, INT_MIN, INT_MIN, SRCCOPY);
        StretchBlt(Strip, 0, 0, INT_MAX, 1, Strip, 0, 0, 1, 1, SRCCOPY);
        Check(Pixels(Strip, Four, 4, 1) == std::vector<unsigned>{R, R, R, R},
              "extreme blit extents are clipped to the bitmap");
        Paint();
        // A screen DC has no surface: it fills nothing and takes no bitmap.
        HDC const ScreenDc = GetDC(nullptr);
        RECT const Everything = {0, 0, 4, 1};
        Check(FillRect(ScreenDc, &Everything, Brush) == 0 &&
                  SelectObject(ScreenDc, Four) == nullptr,
              "a screen DC draws nowhere and takes no bitmap");
        ReleaseDC(nullptr, ScreenDc);
        // ExtTextOut counts: past 8192 refused; zero still fills an ETO_OPAQUE rectangle.
        SetBkColor(Strip, RGB(0, 0, 255));
        Check(!ExtTextOutW(Strip, 0, 0, 0, nullptr, L"A", 0x80000000u, nullptr),
              "ExtTextOutW refuses a count past 8192");
        Check(ExtTextOutW(Strip, 0, 0, ETO_OPAQUE, &Everything, L"", 0, nullptr) &&
                  Pixels(Strip, Four, 4, 1) == std::vector<unsigned>{B, B, B, B},
              "ExtTextOutW with no text fills its ETO_OPAQUE rectangle");
        Paint();
        // DIB sizes: a zero width or height copies nothing; INT_MIN's height is 2^31 rows.
        Line.bmiHeader.biWidth = 0;
        Check(GetDIBits(Strip, Four, 0, 1, Untouched, &Line, DIB_RGB_COLORS) == 0,
              "GetDIBits refuses a zero width");
        Line.bmiHeader.biWidth = 4;
        Line.bmiHeader.biHeight = 0;
        Check(GetDIBits(Strip, Four, 0, 1, Untouched, &Line, DIB_RGB_COLORS) == 0,
              "GetDIBits refuses a zero height");
        Line.bmiHeader.biHeight = static_cast<LONG>(INT_MIN);
        Check(GetDIBits(Strip, Four, 0, 1, Untouched, &Line, DIB_RGB_COLORS) == 1 &&
                  Untouched[0] == R,
              "GetDIBits takes INT_MIN as a top-down height");

        Check(SelectObject(Other, Four) == nullptr,
              "a bitmap selected into one DC is not selected into another");
        Check(SelectObject(Strip, Four) == Four, "it may be selected again into its own DC");
        SelectObject(Strip, StripOriginal);
        HGDIOBJ const OtherOriginal = SelectObject(Other, Four);
        Check(OtherOriginal != nullptr, "and into another once deselected");
        SelectObject(Other, OtherOriginal);

        HRGN const Empty = CreateRectRgn(2, 2, 2, 2);
        Check(SelectObject(Strip, Empty) ==
                  reinterpret_cast<HGDIOBJ>(static_cast<std::uintptr_t>(NULLREGION)),
              "SelectObject of an empty region answers NULLREGION");
        SelectClipRgn(Strip, nullptr);
        DeleteObject(Empty);
        DeleteObject(Four);
        DeleteDC(Strip);
        DeleteDC(Other);
    }

    Check(!DeleteObject(Bitmap), "a selected bitmap is not deleted");
    SelectObject(Dc, Original);
    Check(DeleteObject(Bitmap), "a deselected bitmap is");
    SelectObject(Dc, SystemFont);
    Check(DeleteObject(Font) && DeleteObject(WhiteBrush) && DeleteObject(Clip),
          "fonts, brushes and regions are deleted");
    Check(DeleteObject(Brush), "a stock object's DeleteObject succeeds and keeps it");
    Check(DeleteDC(Dc) && !DeleteDC(Dc), "DeleteDC, once");

    HFONT const Missing = MakeFont("Agency FB", -20, FW_BOLD);
    Check(Missing != nullptr, "a face that is not installed falls back to a lookalike");
    DeleteObject(Missing);

    for (const std::string& File : Files)
        std::remove(File.c_str());
    rmdir(First.c_str());
    rmdir(Second.c_str());
    rmdir(Third.c_str());
    rmdir(Folder.c_str());

    std::printf("%d failure(s)\n", GFailures);
    return GFailures == 0 ? 0 : 1;
}
