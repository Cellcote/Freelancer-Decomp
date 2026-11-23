// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): GDI on macOS and Linux (src/include/posix, non-Windows only; posix/windows.h
// includes it), with the SDK's values and layouts. The calls are the gdi32 module's
// (src/PosixGdi32), one per process, which every module on the stand-in <windows.h> links, as
// MSVC links gdi32.lib by default: memory device contexts and 32-bit bitmaps, solid brushes,
// rectangular regions, and TrueType fonts found by face name in the system's font folders (and
// the game's), drawn with GDI's metrics and alignment rules. That is what the client's and
// Common's text renderers ask of GDI: glyphs drawn into a memory bitmap and read back with
// GetDIBits. There is no screen DC that draws on a window (the client draws through its render
// pipeline), and no bitmap files yet (LoadImageA fails).

#pragma once

#if defined(_WIN32)
#error "posix/wingdi.h is for macOS and Linux"
#endif

// What the gdi32 module exports (default visibility, also from a hidden-visibility build).
#define FL_GDI32_API __attribute__((visibility("default")))

typedef void* HGDIOBJ;
typedef struct HFONT__* HFONT;
typedef struct HBITMAP__* HBITMAP;
typedef DWORD COLORREF;

#define LF_FACESIZE 32
#define TRANSPARENT 1
#define OPAQUE 2
#define RGB(r, g, b)                                                                               \
    ((COLORREF)(((BYTE)(r) | ((WORD)((BYTE)(g)) << 8)) | (((DWORD)(BYTE)(b)) << 16)))
#define GDI_ERROR (0xFFFFFFFFL)
#define CLR_INVALID 0xFFFFFFFF
#define FW_NORMAL 400
#define FW_BOLD 700
#define DEFAULT_CHARSET 1
#define ANSI_CHARSET 0
#define OUT_DEFAULT_PRECIS 0
#define CLIP_DEFAULT_PRECIS 0
#define ANTIALIASED_QUALITY 4
#define DEFAULT_QUALITY 0
#define DEFAULT_PITCH 0
#define FF_DONTCARE (0 << 4)

typedef struct tagLOGFONTA
{
    LONG lfHeight;
    LONG lfWidth;
    LONG lfEscapement;
    LONG lfOrientation;
    LONG lfWeight;
    BYTE lfItalic;
    BYTE lfUnderline;
    BYTE lfStrikeOut;
    BYTE lfCharSet;
    BYTE lfOutPrecision;
    BYTE lfClipPrecision;
    BYTE lfQuality;
    BYTE lfPitchAndFamily;
    CHAR lfFaceName[LF_FACESIZE];
} LOGFONTA;
typedef LOGFONTA LOGFONT;

typedef struct tagTEXTMETRICA
{
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    BYTE tmFirstChar;
    BYTE tmLastChar;
    BYTE tmDefaultChar;
    BYTE tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICA;
typedef TEXTMETRICA TEXTMETRIC;

typedef struct tagTEXTMETRICW
{
    LONG tmHeight;
    LONG tmAscent;
    LONG tmDescent;
    LONG tmInternalLeading;
    LONG tmExternalLeading;
    LONG tmAveCharWidth;
    LONG tmMaxCharWidth;
    LONG tmWeight;
    LONG tmOverhang;
    LONG tmDigitizedAspectX;
    LONG tmDigitizedAspectY;
    // 16-bit, as Windows' WCHAR (WCHAR is a 32-bit wchar_t here): Windows' layout.
    WORD tmFirstChar;
    WORD tmLastChar;
    WORD tmDefaultChar;
    WORD tmBreakChar;
    BYTE tmItalic;
    BYTE tmUnderlined;
    BYTE tmStruckOut;
    BYTE tmPitchAndFamily;
    BYTE tmCharSet;
} TEXTMETRICW;

typedef struct HRGN__* HRGN;
#define ETO_OPAQUE 0x0002
#define ETO_CLIPPED 0x0004
#define TA_NOUPDATECP 0
#define TA_UPDATECP 1
#define TA_LEFT 0
#define TA_RIGHT 2
#define TA_CENTER 6
#define TA_TOP 0
#define TA_BOTTOM 8
#define TA_BASELINE 24
#define SRCCOPY 0x00CC0020
#define IMAGE_BITMAP 0
#define LR_DEFAULTCOLOR 0x0000
#define LR_LOADFROMFILE 0x0010
#define LR_CREATEDIBSECTION 0x2000
#define ERROR 0
#define NULLREGION 1
#define SIMPLEREGION 2
#define COMPLEXREGION 3

typedef struct tagBITMAP
{
    LONG bmType;
    LONG bmWidth;
    LONG bmHeight;
    LONG bmWidthBytes;
    WORD bmPlanes;
    WORD bmBitsPixel;
    LPVOID bmBits;
} BITMAP;
typedef struct tagBITMAPINFOHEADER
{
    DWORD biSize;
    LONG biWidth;
    LONG biHeight;
    WORD biPlanes;
    WORD biBitCount;
    DWORD biCompression;
    DWORD biSizeImage;
    LONG biXPelsPerMeter;
    LONG biYPelsPerMeter;
    DWORD biClrUsed;
    DWORD biClrImportant;
} BITMAPINFOHEADER;
typedef struct tagRGBQUAD
{
    BYTE rgbBlue;
    BYTE rgbGreen;
    BYTE rgbRed;
    BYTE rgbReserved;
} RGBQUAD;
typedef struct tagBITMAPINFO
{
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD bmiColors[1];
} BITMAPINFO, *LPBITMAPINFO;
#define BI_RGB 0L
#define BI_BITFIELDS 3L
#define DIB_RGB_COLORS 0
#define DIB_PAL_COLORS 1

typedef struct tagDIBSECTION
{
    BITMAP dsBm;
    BITMAPINFOHEADER dsBmih;
    DWORD dsBitfields[3];
    HANDLE dshSection;
    DWORD dsOffset;
} DIBSECTION;

// The stock objects (GetStockObject).
#define WHITE_BRUSH 0
#define LTGRAY_BRUSH 1
#define GRAY_BRUSH 2
#define DKGRAY_BRUSH 3
#define BLACK_BRUSH 4
#define NULL_BRUSH 5
#define SYSTEM_FONT 13
#define DEFAULT_GUI_FONT 17

// Device contexts. GetDC gives a 32-bit "screen" context, whatever the window, for making
// compatible bitmaps and contexts; it draws nowhere.
FL_GDI32_API HDC GetDC(HWND Window);
FL_GDI32_API int ReleaseDC(HWND Window, HDC Dc);
FL_GDI32_API HDC CreateCompatibleDC(HDC Dc);
FL_GDI32_API BOOL DeleteDC(HDC Dc);
FL_GDI32_API HGDIOBJ SelectObject(HDC Dc, HGDIOBJ Object);
FL_GDI32_API BOOL DeleteObject(HGDIOBJ Object);
FL_GDI32_API HGDIOBJ GetStockObject(int Index);
FL_GDI32_API int GetObjectA(HANDLE Object, int Size, LPVOID Out);

// Text state and drawing.
FL_GDI32_API COLORREF SetTextColor(HDC Dc, COLORREF Color);
FL_GDI32_API COLORREF SetBkColor(HDC Dc, COLORREF Color);
FL_GDI32_API int SetBkMode(HDC Dc, int Mode);
FL_GDI32_API UINT SetTextAlign(HDC Dc, UINT Align);
FL_GDI32_API HFONT CreateFontIndirectA(const LOGFONTA* Font);
FL_GDI32_API BOOL GetTextMetricsA(HDC Dc, TEXTMETRICA* Metrics);
FL_GDI32_API int GetTextFaceA(HDC Dc, int Count, LPSTR Name);
FL_GDI32_API BOOL GetTextMetricsW(HDC Dc, TEXTMETRICW* Metrics);
FL_GDI32_API BOOL GetCharWidthW(HDC Dc, UINT First, UINT Last, int* Widths);
FL_GDI32_API BOOL GetTextExtentPoint32W(HDC Dc, const wchar_t* Text, int Count, LPSIZE Size);
FL_GDI32_API BOOL GetTextExtentPoint32A(HDC Dc, LPCSTR Text, int Count, LPSIZE Size);
FL_GDI32_API BOOL GetTextExtentExPointW(HDC Dc, const wchar_t* Text, int Count, int MaxExtent,
                                        int* Fit, int* Extents, LPSIZE Size);
FL_GDI32_API BOOL ExtTextOutW(HDC Dc, int X, int Y, UINT Options, const RECT* Rect,
                              const wchar_t* Text, UINT Count, const int* Dx);
FL_GDI32_API BOOL ExtTextOutA(HDC Dc, int X, int Y, UINT Options, const RECT* Rect, LPCSTR Text,
                              UINT Count, const int* Dx);
inline BOOL GetTextExtentPointW(HDC Dc, const wchar_t* Text, int Count, LPSIZE Size)
{
    return GetTextExtentPoint32W(Dc, Text, Count, Size);
}

// Bitmaps and brushes.
FL_GDI32_API HBITMAP CreateCompatibleBitmap(HDC Dc, int Width, int Height);
FL_GDI32_API int GetDIBits(HDC Dc, HBITMAP Bitmap, UINT Start, UINT Lines, LPVOID Bits,
                           BITMAPINFO* Info, UINT Usage);
FL_GDI32_API HBRUSH CreateSolidBrush(COLORREF Color);
FL_GDI32_API int FillRect(HDC Dc, const RECT* Rect, HBRUSH Brush);
FL_GDI32_API BOOL BitBlt(HDC Dc, int X, int Y, int Width, int Height, HDC Source, int SourceX,
                         int SourceY, DWORD Rop);
FL_GDI32_API BOOL StretchBlt(HDC Dc, int X, int Y, int Width, int Height, HDC Source, int SourceX,
                             int SourceY, int SourceWidth, int SourceHeight, DWORD Rop);
FL_GDI32_API HANDLE LoadImageA(HINSTANCE Instance, LPCSTR Name, UINT Type, int Width, int Height,
                               UINT Flags);

// Regions: rectangles.
FL_GDI32_API HRGN CreateRectRgn(int Left, int Top, int Right, int Bottom);
FL_GDI32_API HRGN CreateRectRgnIndirect(const RECT* Rect);
FL_GDI32_API int SelectClipRgn(HDC Dc, HRGN Region);
FL_GDI32_API int GetClipRgn(HDC Dc, HRGN Region);
FL_GDI32_API int GetRgnBox(HRGN Region, LPRECT Rect);

inline BOOL OffsetRect(LPRECT Rect, int Dx, int Dy)
{
    if (Rect == nullptr)
        return FALSE;
    Rect->left += Dx;
    Rect->right += Dx;
    Rect->top += Dy;
    Rect->bottom += Dy;
    return TRUE;
}
