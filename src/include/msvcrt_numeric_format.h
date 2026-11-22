// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.
// This file was reconstructed by reverse engineering the binaries of Freelancer 1.1,
// developed by Digital Anvil.
// Author: Giperion.

#ifndef MSVCRT_NUMERIC_FORMAT_H
#define MSVCRT_NUMERIC_FORMAT_H

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <locale.h>
#include <math.h>

/* Each includer uses some of these helpers. */
#if defined(__GNUC__)
#define MSVCRT_HELPER static __attribute__((unused))
#else
#define MSVCRT_HELPER static
#endif

/* Selected VC6 numeric contracts used by Lua and NumberStringChild.
 * Keep options local: changing the module-wide UCRT policy affects other users. */
#if defined(_MSC_VER)
MSVCRT_HELPER int MsvcrtLegacyVsnprintf(char* Buffer, size_t Count, char const* Format,
                                        va_list Args)
{
    return __stdio_common_vsprintf(
        _CRT_INTERNAL_PRINTF_LEGACY_VSPRINTF_NULL_TERMINATION |
        _CRT_INTERNAL_PRINTF_LEGACY_MSVCRT_COMPATIBILITY |
        _CRT_INTERNAL_PRINTF_LEGACY_THREE_DIGIT_EXPONENTS,
        Buffer, Count, Format, NULL, Args);
}
#else
/* PORT(macOS): the same three legacy options over the C library's vsnprintf.
 * - Null termination: _vsnprintf's (none when the text fills Count, -1 when it overflows).
 * - MSVCRT compatibility: an infinity or NaN prints as 1.#INF, 1.#QNAN or -1.#IND (the
 *   default NaN of 0/0 has its sign set), padded with zeros to the precision for %e and %f.
 *   A precision too small for that text keeps the text whole (VC6 rounds it, as "1.#J").
 * - Three-digit exponents: %e and %g print e+005, not e+05.
 * Each conversion is formatted on its own: flags, width and precision (also '*'), and the
 * h, l, ll, I64, L length modifiers. */
MSVCRT_HELPER void MsvcrtAppend(char** Out, char* End, char const* Text, size_t Length,
                                size_t* Total)
{
    size_t Index;
    for (Index = 0; Index < Length; ++Index)
    {
        if (*Out < End)
            *(*Out)++ = Text[Index];
    }
    *Total += Length;
}

MSVCRT_HELPER int MsvcrtLegacyVsnprintf(char* Buffer, size_t Count, char const* Format,
                                        va_list Args)
{
    char* Out = Buffer;
    char* const End = Buffer + Count;
    size_t Total = 0;
    char const* Cursor = Format;
    va_list Rest;
    va_copy(Rest, Args);
    while (*Cursor != 0)
    {
        char Spec[64];
        char Piece[512];
        size_t SpecLength = 0;
        int Width = 0, Precision = -1, Long = 0, Wide64 = 0, Short = 0;
        int bWidthStar = 0, bPrecisionStar = 0, bLeft = 0;
        char Conversion;
        int Length = 0;
        if (*Cursor != '%')
        {
            MsvcrtAppend(&Out, End, Cursor, 1, &Total);
            ++Cursor;
            continue;
        }
        if (Cursor[1] == '%')
        {
            MsvcrtAppend(&Out, End, "%", 1, &Total);
            Cursor += 2;
            continue;
        }
        Spec[SpecLength++] = *Cursor++;
        while (strchr("-+ #0", *Cursor) != NULL && *Cursor != 0 && SpecLength < 16)
        {
            if (*Cursor == '-')
                bLeft = 1;
            Spec[SpecLength++] = *Cursor++;
        }
        if (*Cursor == '*')
        {
            bWidthStar = 1;
            Spec[SpecLength++] = *Cursor++;
        }
        else
        {
            while (*Cursor >= '0' && *Cursor <= '9' && SpecLength < 32)
            {
                Width = Width * 10 + (*Cursor - '0');
                Spec[SpecLength++] = *Cursor++;
            }
        }
        if (*Cursor == '.')
        {
            Spec[SpecLength++] = *Cursor++;
            Precision = 0;
            if (*Cursor == '*')
            {
                bPrecisionStar = 1;
                Spec[SpecLength++] = *Cursor++;
            }
            else
            {
                while (*Cursor >= '0' && *Cursor <= '9' && SpecLength < 48)
                {
                    Precision = Precision * 10 + (*Cursor - '0');
                    Spec[SpecLength++] = *Cursor++;
                }
            }
        }
        /* Length modifiers: dropped from Spec and put back in the C library's spelling. */
        for (;;)
        {
            if (*Cursor == 'h')
            {
                Short = 1;
                ++Cursor;
            }
            else if (*Cursor == 'l')
            {
                ++Long;
                ++Cursor;
            }
            else if (Cursor[0] == 'I' && Cursor[1] == '6' && Cursor[2] == '4')
            {
                Wide64 = 1;
                Cursor += 3;
            }
            else if (*Cursor == 'L' || *Cursor == 'w')
                ++Cursor;
            else
                break;
        }
        Conversion = *Cursor;
        if (Conversion == 0)
            break;
        ++Cursor;
        if (bWidthStar)
        {
            Width = va_arg(Rest, int);
            if (Width < 0)
            {
                bLeft = 1;
                Width = -Width;
            }
        }
        if (bPrecisionStar)
            Precision = va_arg(Rest, int);
        switch (Conversion)
        {
        case 'd':
        case 'i':
        case 'u':
        case 'x':
        case 'X':
        case 'o':
            if (Wide64 || Long >= 2)
            {
                memcpy(Spec + SpecLength, "ll", 2);
                Spec[SpecLength + 2] = Conversion;
                Spec[SpecLength + 3] = 0;
                {
                    long long const Value = va_arg(Rest, long long);
                    Length =
                        bWidthStar
                            ? (bPrecisionStar
                                   ? snprintf(Piece, sizeof(Piece), Spec, Width, Precision, Value)
                                   : snprintf(Piece, sizeof(Piece), Spec, Width, Value))
                            : (bPrecisionStar
                                   ? snprintf(Piece, sizeof(Piece), Spec, Precision, Value)
                                   : snprintf(Piece, sizeof(Piece), Spec, Value));
                }
            }
            else
            {
                /* int and long are both 32-bit on Windows. */
                int Value = va_arg(Rest, int);
                if (Short)
                    Value = (Conversion == 'd' || Conversion == 'i') ? (int)(short)Value
                                                                     : (int)(unsigned short)Value;
                Spec[SpecLength] = Conversion;
                Spec[SpecLength + 1] = 0;
                Length =
                    bWidthStar
                        ? (bPrecisionStar
                               ? snprintf(Piece, sizeof(Piece), Spec, Width, Precision, Value)
                               : snprintf(Piece, sizeof(Piece), Spec, Width, Value))
                        : (bPrecisionStar ? snprintf(Piece, sizeof(Piece), Spec, Precision, Value)
                                          : snprintf(Piece, sizeof(Piece), Spec, Value));
            }
            break;
        case 'c':
        {
            int const Value = va_arg(Rest, int);
            Spec[SpecLength] = 'c';
            Spec[SpecLength + 1] = 0;
            Length = bWidthStar ? snprintf(Piece, sizeof(Piece), Spec, Width, Value)
                                : snprintf(Piece, sizeof(Piece), Spec, Value);
            break;
        }
        case 's':
        case 'p':
        {
            void* const Value = va_arg(Rest, void*);
            char const* const Text =
                Conversion == 's' ? (Value != NULL ? (char const*)Value : "(null)") : NULL;
            Spec[SpecLength] = Conversion;
            Spec[SpecLength + 1] = 0;
            if (Conversion == 'p')
                Length =
                    snprintf(Piece, sizeof(Piece), "%08llX", (unsigned long long)(uintptr_t)Value);
            else
            {
                /* A string may be longer than Piece: append it directly with its padding. */
                size_t TextLength = strlen(Text);
                size_t Pad;
                if (Precision >= 0 && (size_t)Precision < TextLength)
                    TextLength = (size_t)Precision;
                Pad = (size_t)Width > TextLength ? (size_t)Width - TextLength : 0;
                while (!bLeft && Pad > 0)
                {
                    MsvcrtAppend(&Out, End, " ", 1, &Total);
                    --Pad;
                }
                MsvcrtAppend(&Out, End, Text, TextLength, &Total);
                while (bLeft && Pad > 0)
                {
                    MsvcrtAppend(&Out, End, " ", 1, &Total);
                    --Pad;
                }
                continue;
            }
            break;
        }
        case 'e':
        case 'E':
        case 'f':
        case 'g':
        case 'G':
        {
            double const Value = va_arg(Rest, double);
            uint64_t Bits;
            memcpy(&Bits, &Value, sizeof(Bits));
            if ((Bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000))
            {
                int const bNegative = (Bits >> 63) != 0;
                int const bNaN = (Bits & UINT64_C(0x000fffffffffffff)) != 0;
                int const bQuiet = (Bits & UINT64_C(0x0008000000000000)) != 0;
                char Text[128];
                size_t TextLength;
                char const* const Base =
                    !bNaN ? "1.#INF"
                          : (bNegative && bQuiet && (Bits & UINT64_C(0x0007ffffffffffff)) == 0
                                 ? "1.#IND"
                                 : (bQuiet ? "1.#QNAN" : "1.#SNAN"));
                snprintf(Text, sizeof(Text), "%s%s", bNegative ? "-" : "", Base);
                TextLength = strlen(Text);
                if (Conversion != 'g' && Conversion != 'G')
                {
                    int const Wanted = Precision < 0 ? 6 : Precision;
                    int const Have = (int)(strlen(Base) - 2); /* the characters after "1." */
                    int Pad;
                    for (Pad = Have; Pad < Wanted && TextLength + 1 < sizeof(Text); ++Pad)
                        Text[TextLength++] = '0';
                    Text[TextLength] = 0;
                }
                Length = snprintf(Piece, sizeof(Piece), bLeft ? "%-*s" : "%*s", Width, Text);
                break;
            }
            Spec[SpecLength] = Conversion;
            Spec[SpecLength + 1] = 0;
            Length = bWidthStar
                         ? (bPrecisionStar
                                ? snprintf(Piece, sizeof(Piece), Spec, Width, Precision, Value)
                                : snprintf(Piece, sizeof(Piece), Spec, Width, Value))
                         : (bPrecisionStar ? snprintf(Piece, sizeof(Piece), Spec, Precision, Value)
                                           : snprintf(Piece, sizeof(Piece), Spec, Value));
            if (Length > 0 && (size_t)Length < sizeof(Piece) - 1 && Conversion != 'f')
            {
                /* e+05 -> e+005: one more exponent digit, taken from the padding if any. */
                char* Exponent = strpbrk(Piece, "eE");
                if (Exponent != NULL && (Exponent[1] == '+' || Exponent[1] == '-') &&
                    strlen(Exponent + 2) == 2)
                {
                    char* const Digits = Exponent + 2;
                    char* Start = Piece;
                    memmove(Digits + 1, Digits, strlen(Digits) + 1);
                    Digits[0] = '0';
                    ++Length;
                    /* Keep the width: drop one leading pad space, or one trailing one. */
                    if (Width > 0 && Length > Width)
                    {
                        if (Piece[0] == ' ')
                        {
                            memmove(Start, Start + 1, (size_t)Length);
                            --Length;
                        }
                        else if (Piece[Length - 1] == ' ')
                        {
                            Piece[--Length] = 0;
                        }
                    }
                }
            }
            break;
        }
        case 'n':
            (void)va_arg(Rest, void*); /* not supported: nothing is written */
            continue;
        default:
            MsvcrtAppend(&Out, End, &Conversion, 1, &Total);
            continue;
        }
        if (Length < 0)
        {
            va_end(Rest);
            return -1;
        }
        if ((size_t)Length >= sizeof(Piece))
            Length = (int)sizeof(Piece) - 1;
        MsvcrtAppend(&Out, End, Piece, (size_t)Length, &Total);
    }
    va_end(Rest);
    if (Total < Count)
    {
        *Out = 0;
        return (int)Total;
    }
    return Total == Count ? (int)Total : -1;
}

/* _ecvt_s: Count significant decimal digits of Value, rounded, without a point; Point is
 * where the point goes (0 for zero, as MSVC), Sign whether Value is negative. */
MSVCRT_HELPER int MsvcrtEcvt(char* Buffer, size_t Size, double Value, int Count, int* Point,
                             int* Sign)
{
    char Text[64];
    char* Cursor;
    int Index = 0;
    if (Buffer == NULL || Size <= (size_t)Count || Count <= 0 || Count > 40)
        return 22; /* EINVAL */
    *Sign = signbit(Value) ? 1 : 0;
    snprintf(Text, sizeof(Text), "%.*e", Count - 1, Value < 0 ? -Value : Value);
    for (Cursor = Text; *Cursor != 0 && *Cursor != 'e'; ++Cursor)
    {
        if (*Cursor >= '0' && *Cursor <= '9' && Index < Count)
            Buffer[Index++] = *Cursor;
    }
    Buffer[Index] = 0;
    *Point = Value == 0.0 ? 0 : atoi(Cursor + 1) + 1;
    return 0;
}
#define _ecvt_s MsvcrtEcvt
#endif

MSVCRT_HELPER int MsvcrtLegacySnprintf(char* Buffer, size_t Count, char const* Format, ...)
{
    int Result;
    va_list Args;
    va_start(Args, Format);
    Result = MsvcrtLegacyVsnprintf(Buffer, Count, Format, Args);
    va_end(Args);
    return Result;
}

MSVCRT_HELPER void MsvcrtRoundDigits(char* Digits, int Keep, int* Point)
{
    int Index;
    if (Keep < 0 || Keep >= 17 || Digits[Keep] < '5')
        return;
    Index = Keep - 1;
    while (Index >= 0 && Digits[Index] == '9')
        Digits[Index--] = '0';
    if (Index >= 0)
        ++Digits[Index];
    else
    {
        Digits[0] = '1';
        for (Index = 1; Index < 17; ++Index)
            Digits[Index] = '0';
        ++*Point;
    }
}

MSVCRT_HELPER int MsvcrtIsSpecial(double Value)
{
    uint64_t Bits;
    memcpy(&Bits, &Value, sizeof(Bits));
    return (Bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000);
}

MSVCRT_HELPER void MsvcrtFormatLuaNumber(char* Buffer, double Value)
{
    char Digits[32];
    char* Cursor = Buffer;
    int Point, Sign, Length, Index;
    char Decimal = localeconv()->decimal_point[0];
    if (MsvcrtIsSpecial(Value))
    {
        MsvcrtLegacySnprintf(Buffer, 32, "%.16g", Value);
        return;
    }
    _ecvt_s(Digits, sizeof(Digits), Value, 17, &Point, &Sign);
    if (Value == 0.0)
        Point = 1; // %g emits 0/-0; _ecvt_s reports a decimal point at zero.
    MsvcrtRoundDigits(Digits, 16, &Point);
    Length = 16;
    while (Length > 1 && Digits[Length - 1] == '0')
        --Length;
    if (Sign)
        *Cursor++ = '-';
    if (Point - 1 < -4 || Point - 1 >= 16)
    {
        *Cursor++ = Digits[0];
        if (Length > 1)
        {
            *Cursor++ = Decimal;
            memcpy(Cursor, Digits + 1, Length - 1);
            Cursor += Length - 1;
        }
        MsvcrtLegacySnprintf(Cursor, 32 - (size_t)(Cursor - Buffer),
            "e%c%03d", Point - 1 < 0 ? '-' : '+', abs(Point - 1));
        return;
    }
    if (Point <= 0)
    {
        *Cursor++ = '0';
        *Cursor++ = Decimal;
        for (Index = 0; Index < -Point; ++Index)
            *Cursor++ = '0';
        memcpy(Cursor, Digits, Length);
        Cursor += Length;
    }
    else
    {
        for (Index = 0; Index < Point; ++Index)
            *Cursor++ = Index < Length ? Digits[Index] : '0';
        if (Length > Point)
        {
            *Cursor++ = Decimal;
            memcpy(Cursor, Digits + Point, Length - Point);
            Cursor += Length - Point;
        }
    }
    *Cursor = 0;
}

/* NumberStringChild passes a widened float and a single %.Nf with a suffix.
 * VC6's fixed output pads zeros after its 17 significant decimal digits. */
MSVCRT_HELPER int MsvcrtNumericVsnprintf(char* Buffer, size_t Count, char const* Format,
                                         va_list Args)
{
    char const* Cursor = Format;
    int Precision = 0;
    if (Cursor[0] == '%' && Cursor[1] == '.' && Cursor[2] >= '0' && Cursor[2] <= '9')
    {
        Cursor += 2;
        while (*Cursor >= '0' && *Cursor <= '9' && Precision <= 100)
            Precision = Precision * 10 + (*Cursor++ - '0');
        if (*Cursor == 'f' && Precision <= 100 &&
            (strchr(Cursor + 1, '%') == NULL || strcmp(Cursor + 1, "%") == 0 || strcmp(Cursor + 1, "%%") == 0))
        {
            double Value = va_arg(Args, double);
            char Digits[32], Output[512];
            char* Out = Output;
            char const* Suffix = strcmp(Cursor + 1, "%") == 0 ? "" :
                (strcmp(Cursor + 1, "%%") == 0 ? "%" : Cursor + 1);
            int Point, Sign, Index, Keep, Length;
            if (MsvcrtIsSpecial(Value))
                return MsvcrtLegacySnprintf(Buffer, Count, Format, Value);
            _ecvt_s(Digits, sizeof(Digits), Value, 17, &Point, &Sign);
            Keep = Point + Precision;
            MsvcrtRoundDigits(Digits, Keep, &Point);
            if (Keep >= 0 && Point + Precision > Keep)
                Keep = Point + Precision;
            if (Sign)
                *Out++ = '-';
            for (Index = 0; Index < (Point > 0 ? Point : 1); ++Index)
                *Out++ = Point > 0 && Index < 17 && Index < Keep ? Digits[Index] : '0';
            if (Precision)
                *Out++ = localeconv()->decimal_point[0];
            for (Index = 0; Index < Precision; ++Index)
            {
                int Digit = Point + Index;
                *Out++ = Digit >= 0 && Digit < 17 && Digit < Keep ? Digits[Digit] : '0';
            }
            Length = (int)(Out - Output);
            if (Length + strlen(Suffix) >= sizeof(Output))
                return -1;
            strcpy(Out, Suffix);
            Length += (int)strlen(Suffix);
            if ((size_t)Length >= Count)
            {
                memcpy(Buffer, Output, Count);
                return (size_t)Length == Count ? Length : -1;
            }
            memcpy(Buffer, Output, Length + 1);
            return Length;
        }
    }
    return MsvcrtLegacyVsnprintf(Buffer, Count, Format, Args);
}
#endif
