// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): checks of gdi32 on Linux and macOS (src/PosixGdi32): memory DCs and bitmaps,
// fonts found by face name and their GDI metrics, ExtTextOutW's alignment, background and
// clipping, GetDIBits' formats, blits, brushes and regions, the selection rules, and font
// folders holding files that are not fonts. Not part of retail; built only by the non-Windows
// build, with ctest. The checks of Windows' exact Arial metrics run on macOS, whose Arial is a
// known face (with VDMX); the others hold for whichever face the fallbacks find.

#include <windows.h>

#include <algorithm>
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

// A font file on this system and its family name, for the font-folder checks; null when none of
// these is installed.
struct FKnownFont
{
    const char* Path;
    const char* Family;
};

const FKnownFont* FindKnownFont()
{
    static const FKnownFont kFonts[] = {
        {"/System/Library/Fonts/Supplemental/Arial.ttf", "Arial"},
        {"/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "DejaVu Sans"},
        {"/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf", "Liberation Sans"},
        {"/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf", "Liberation Sans"},
    };
    for (const FKnownFont& Font : kFonts)
        if (access(Font.Path, R_OK) == 0)
            return &Font;
    return nullptr;
}

std::vector<char> ReadFile(const char* Path)
{
    std::ifstream File(Path, std::ios::binary);
    return std::vector<char>(std::istreambuf_iterator<char>(File),
                             std::istreambuf_iterator<char>());
}

void WriteFile(const std::string& Path, const char* Data, std::size_t Size)
{
    std::ofstream File(Path, std::ios::binary);
    File.write(Data, static_cast<std::streamsize>(Size));
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
    // Before any font is made (the catalogue is built once): a font folder, searched first,
    // with a FIFO named like a font (no writer: opening it blocks), a font cut short, and a copy
    // of an installed font that goes away after the catalogue lists it.
    const FKnownFont* const Known = FindKnownFont();
    char FolderTemplate[] = "/tmp/gdi32_tests.XXXXXX";
    std::string const Folder = mkdtemp(FolderTemplate) != nullptr ? FolderTemplate : "";
    std::string const Copy = Folder + "/Gone.ttf";
    if (!Folder.empty())
    {
        setenv("FL_GDI32_FONT_PATH", Folder.c_str(), 1);
        mkfifo((Folder + "/Pipe.ttf").c_str(), 0600);
        if (Known != nullptr)
        {
            std::vector<char> const Data = ReadFile(Known->Path);
            WriteFile(Folder + "/Broken.ttf", Data.data(),
                      std::min<std::size_t>(Data.size(), 2048));
            WriteFile(Copy, Data.data(), Data.size());
        }
    }
    if (Known != nullptr && !Folder.empty())
    {
        HFONT const First = MakeFont(Known->Family, -16);
        Check(First != nullptr, "the catalogue skips a FIFO and a cut-short font");
        DeleteObject(First);
        std::remove(Copy.c_str());
        HDC const Probe = CreateCompatibleDC(nullptr);
        HFONT const Again = MakeFont(Known->Family, -16);
        char Name[LF_FACESIZE] = {};
        HGDIOBJ const Previous = SelectObject(Probe, Again);
        Check(Again != nullptr && GetTextFaceA(Probe, LF_FACESIZE, Name) > 0 &&
                  std::strcmp(Name, Known->Family) == 0,
              "a catalogued face whose file is gone gives way to the next one of its family");
        SelectObject(Probe, Previous);
        DeleteObject(Again);
        DeleteDC(Probe);
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
    // macOS's Arial (Supplemental/Arial.ttf) has a VDMX table, as Windows' has.
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
    Check(ExtTextOutW(Dc, 4, 4, 0, nullptr, L"Hi", 2, nullptr), "ExtTextOutW draws");
    std::vector<unsigned> Image = Pixels(Dc, Bitmap, 128, 64);
    RECT const Cell = {4, 4, 4 + Size.cx, 4 + Metrics.tmHeight};
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

    if (!Folder.empty())
    {
        std::remove((Folder + "/Pipe.ttf").c_str());
        std::remove((Folder + "/Broken.ttf").c_str());
        rmdir(Folder.c_str());
    }

    std::printf("%d failure(s)\n", GFailures);
    return GFailures == 0 ? 0 : 1;
}
