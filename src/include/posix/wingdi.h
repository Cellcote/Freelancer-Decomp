// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): GDI's text types and calls on macOS and Linux (src/include/posix, non-Windows
// only; posix/windows.h includes it). Common's text renderer measures and draws glyphs through
// GDI; there is no GDI here, so the types are declared with the SDK's layouts and every call
// fails as GDI does without a device (null handles, FALSE, zero sizes). A font backend (Core
// Text or FreeType) replaces this for the client (M3); a dedicated server draws no text.

#pragma once

#if defined(_WIN32)
#error "posix/wingdi.h is for macOS and Linux"
#endif

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

inline HDC GetDC(HWND)
{
    return nullptr;
}
inline int ReleaseDC(HWND, HDC)
{
    return 0;
}
inline HDC CreateCompatibleDC(HDC)
{
    return nullptr;
}
inline BOOL DeleteDC(HDC)
{
    return FALSE;
}
inline HFONT CreateFontIndirectA(const LOGFONTA*)
{
    return nullptr;
}
inline HGDIOBJ SelectObject(HDC, HGDIOBJ)
{
    return nullptr;
}
inline BOOL DeleteObject(HGDIOBJ)
{
    return FALSE;
}
inline COLORREF SetTextColor(HDC, COLORREF)
{
    return CLR_INVALID;
}
inline COLORREF SetBkColor(HDC, COLORREF)
{
    return CLR_INVALID;
}
inline int SetBkMode(HDC, int)
{
    return 0;
}
inline BOOL GetTextMetricsA(HDC, TEXTMETRICA* Metrics)
{
    if (Metrics != nullptr)
        *Metrics = TEXTMETRICA{};
    return FALSE;
}
inline BOOL GetTextExtentPoint32W(HDC, const wchar_t*, int, LPSIZE Size)
{
    if (Size != nullptr)
        *Size = SIZE{0, 0};
    return FALSE;
}
inline BOOL GetTextExtentExPointW(HDC, const wchar_t*, int, int, int* Fit, int*, LPSIZE Size)
{
    if (Fit != nullptr)
        *Fit = 0;
    if (Size != nullptr)
        *Size = SIZE{0, 0};
    return FALSE;
}
inline BOOL GetCharWidthW(HDC, UINT First, UINT Last, int* Widths)
{
    if (Widths != nullptr && Last >= First)
    {
        for (UINT Index = 0; Index <= Last - First; ++Index)
            Widths[Index] = 0;
    }
    return FALSE;
}

typedef struct HRGN__* HRGN;
#define ETO_OPAQUE 0x0002
#define ETO_CLIPPED 0x0004
#define TA_LEFT 0
#define TA_TOP 0
#define TA_BASELINE 24
#define SRCCOPY 0x00CC0020
#define IMAGE_BITMAP 0
#define LR_DEFAULTCOLOR 0x0000
#define LR_LOADFROMFILE 0x0010
#define LR_CREATEDIBSECTION 0x2000

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
typedef struct tagDIBSECTION
{
    BITMAP dsBm;
    BITMAPINFOHEADER dsBmih;
    DWORD dsBitfields[3];
    HANDLE dshSection;
    DWORD dsOffset;
} DIBSECTION;

inline HRGN CreateRectRgn(int, int, int, int)
{
    return nullptr;
}
inline HRGN CreateRectRgnIndirect(const RECT*)
{
    return nullptr;
}
inline int SelectClipRgn(HDC, HRGN)
{
    return 0;
}
inline UINT SetTextAlign(HDC, UINT)
{
    return GDI_ERROR;
}
inline BOOL ExtTextOutW(HDC, int, int, UINT, const RECT*, const wchar_t*, UINT, const int*)
{
    return FALSE;
}
inline BOOL ExtTextOutA(HDC, int, int, UINT, const RECT*, LPCSTR, UINT, const int*)
{
    return FALSE;
}
inline BOOL BitBlt(HDC, int, int, int, int, HDC, int, int, DWORD)
{
    return FALSE;
}
inline HANDLE LoadImageA(HINSTANCE, LPCSTR, UINT, int, int, UINT)
{
    return nullptr;
}
inline int GetObjectA(HANDLE, int, LPVOID)
{
    return 0;
}
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
inline int GetClipRgn(HDC, HRGN)
{
    return -1;
}
inline int GetRgnBox(HRGN, LPRECT Rect)
{
    if (Rect != nullptr)
        *Rect = RECT{0, 0, 0, 0};
    return 0;
}
inline BOOL StretchBlt(HDC, int, int, int, int, HDC, int, int, int, int, DWORD)
{
    return FALSE;
}
