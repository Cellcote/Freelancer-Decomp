// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): checks of gdi32 on Linux and macOS (src/PosixGdi32): memory DCs and bitmaps,
// fonts found by face name and their GDI metrics, ExtTextOutW's alignment, background and
// clipping, GetDIBits' formats, brushes and regions. Not part of retail; built only by the
// non-Windows build, with ctest. The checks of exact metrics run when Arial is installed (macOS
// has it); the others hold for whichever face the fallbacks find.

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <vector>

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
    LOGFONTA Named = {};
    GetObjectA(Font, sizeof(Named), &Named);
    if (std::strcmp(Named.lfFaceName, "Arial") == 0 && Metrics.tmInternalLeading == 2)
        Check(Metrics.tmHeight == 18 && Metrics.tmAscent == 15 && Metrics.tmDescent == 3,
              "Arial at -16: Windows' 18 / 15 / 3 (VDMX)");

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
              Fit == 2 && Extents[2] == Size.cx,
          "GetTextExtentExPointW counts the characters that fit");

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

    std::printf("%d failure(s)\n", GFailures);
    return GFailures == 0 ? 0 : 1;
}
