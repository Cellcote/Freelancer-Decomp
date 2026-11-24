// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// PORT(macOS, Linux): the resource section of a Windows PE image (PE32 or PE32+), read from the
// image's file bytes: what Platform::ResourceModuleLoad maps (platform_posix.h) and the
// stand-in FindResourceExA / LoadStringW search (posix/kernel32.h, posix/winuser.h). Plain
// functions over a byte range, with no state of their own: every offset and count the image
// holds is checked against the range before it is read, so a damaged or hostile DLL makes a
// lookup fail instead of reading outside it.
//
// The lookups are the Windows loader's (LdrFindResource_U):
// - A type or name is a 16-bit id (MAKEINTRESOURCE), "#<decimal>" for the same id, or a string,
//   compared without regard to case (FindResource upper-cases it; rc.exe stores names upper
//   case).
// - Ids are found by binary search, as the loader does: a directory's id entries are sorted.
// - The language: the one asked for, then its primary language with SUBLANG_NEUTRAL, then
//   LANG_NEUTRAL. A neutral request (PRIMARYLANGID 0, which FindResource and LoadString make)
//   goes on to the user's language (English (US) here, the port's locale), English with
//   SUBLANG_NEUTRAL, English (US), and finally the first language the resource has.
//
// Data entries name their data by RVA; the image is not laid out as the loader lays out
// sections, so an RVA is turned into a file offset through the section table.

#include <cstddef>
#include <cstdint>

namespace PeResources
{
// A PE image's file bytes, with the resource directory found (Root is 0 when it has none).
struct FImage
{
    unsigned char const* Data = nullptr;
    std::size_t Size = 0;
    std::uint32_t Sections = 0;     // the section table's file offset
    std::uint32_t SectionCount = 0; // its entries
    std::uint32_t Root = 0;         // the root resource directory's file offset, or 0
    std::uint32_t RootSize = 0;     // the bytes the directory tree may use from Root
};

// A type or name to look up: an id, or a narrow (UTF-8) string; or nothing (a malformed
// "#" name), which no lookup finds.
struct FName
{
    char const* Text = nullptr; // nullptr: Id
    std::uint16_t Id = 0;
    bool bInvalid = false;
};

// MAKEINTRESOURCEA's pointer, "#123" or a string.
inline FName NameOf(char const* Value)
{
    FName Name;
    std::uintptr_t const Number = reinterpret_cast<std::uintptr_t>(Value);
    if ((Number >> 16) == 0)
    {
        Name.Id = static_cast<std::uint16_t>(Number);
        return Name;
    }
    if (Value[0] == '#')
    {
        // RtlCharToInteger's decimal digits. Anything else after the '#' names nothing (Windows
        // fails the call), and neither does "#0": the prefix is for ids only, so a malformed
        // one is not looked up as a string either.
        std::uint32_t Id = 0;
        char const* Digit = Value + 1;
        for (; *Digit >= '0' && *Digit <= '9' && Id <= 0xFFFF; ++Digit)
            Id = Id * 10 + static_cast<std::uint32_t>(*Digit - '0');
        if (*Digit == 0 && Digit != Value + 1 && Id != 0 && Id <= 0xFFFF)
            Name.Id = static_cast<std::uint16_t>(Id);
        else
            Name.bInvalid = true;
        return Name;
    }
    Name.Text = Value;
    return Name;
}

namespace Detail
{
inline bool Fits(FImage const& Image, std::uint64_t Offset, std::uint64_t Length)
{
    return Offset <= Image.Size && Length <= Image.Size - Offset;
}
inline std::uint16_t Read16(FImage const& Image, std::uint32_t Offset)
{
    return static_cast<std::uint16_t>(Image.Data[Offset] | (Image.Data[Offset + 1] << 8));
}
inline std::uint32_t Read32(FImage const& Image, std::uint32_t Offset)
{
    return static_cast<std::uint32_t>(Image.Data[Offset]) |
           (static_cast<std::uint32_t>(Image.Data[Offset + 1]) << 8) |
           (static_cast<std::uint32_t>(Image.Data[Offset + 2]) << 16) |
           (static_cast<std::uint32_t>(Image.Data[Offset + 3]) << 24);
}

// The file offset of Length bytes at Rva, which must lie in one section's file data; false
// otherwise.
inline bool RvaToOffset(FImage const& Image, std::uint32_t Rva, std::uint32_t Length,
                        std::uint32_t& Offset)
{
    for (std::uint32_t Index = 0; Index < Image.SectionCount; ++Index)
    {
        std::uint32_t const Header = Image.Sections + Index * 40u;
        std::uint32_t const Address = Read32(Image, Header + 12);
        std::uint32_t const RawSize = Read32(Image, Header + 16);
        std::uint32_t const RawOffset = Read32(Image, Header + 20);
        // The raw data's end is inside too: an empty resource may sit there.
        if (Rva < Address || Rva - Address > RawSize)
            continue;
        std::uint32_t const Into = Rva - Address;
        if (Length > RawSize - Into || !Fits(Image, std::uint64_t(RawOffset) + Into, Length))
            return false;
        Offset = RawOffset + Into;
        return true;
    }
    return false;
}

// A directory at Relative (from the root): its entry count and the offset of its first entry.
inline bool Directory(FImage const& Image, std::uint32_t Relative, std::uint32_t& First,
                      std::uint32_t& Named, std::uint32_t& Ids)
{
    if (Relative > Image.RootSize || Image.RootSize - Relative < 16)
        return false;
    std::uint32_t const Offset = Image.Root + Relative;
    Named = Read16(Image, Offset + 12);
    Ids = Read16(Image, Offset + 14);
    if ((Named + Ids) * 8ull > Image.RootSize - Relative - 16)
        return false;
    First = Offset + 16;
    return true;
}

// One UTF-8 code point of Text (advanced past it); U+FFFD for a malformed sequence.
inline std::uint32_t NextCodePoint(unsigned char const*& Text)
{
    std::uint32_t const Lead = *Text++;
    if (Lead < 0x80)
        return Lead;
    int Extra = Lead >= 0xF0 && Lead < 0xF5 ? 3 : Lead >= 0xE0 ? 2 : Lead >= 0xC2 ? 1 : -1;
    if (Lead >= 0xF5 || Extra < 0)
        return 0xFFFD;
    std::uint32_t const Minimum = Extra == 1 ? 0x80 : Extra == 2 ? 0x800 : 0x10000;
    std::uint32_t Code = Lead & (0x3Fu >> Extra);
    for (; Extra > 0; --Extra)
    {
        if ((*Text & 0xC0) != 0x80)
            return 0xFFFD;
        Code = (Code << 6) | (*Text++ & 0x3Fu);
    }
    // Overlong forms, surrogates and values past U+10FFFF are not characters.
    if (Code < Minimum || (Code >= 0xD800 && Code <= 0xDFFF) || Code > 0x10FFFF)
        return 0xFFFD;
    return Code;
}

// The upper case of a UTF-16 unit, as the loader compares names (RtlUpcaseUnicodeChar's simple
// mappings) for ASCII, Latin-1, Latin Extended-A, Greek and Cyrillic; other units are compared
// as they are.
inline std::uint32_t Upper(std::uint32_t Code)
{
    if (Code >= 'a' && Code <= 'z')
        return Code - 32;
    if (Code < 0xE0)
        return Code;
    if (Code <= 0xFE)
        return Code == 0xF7 ? Code : Code - 32; // a-grave..thorn, not the division sign
    if (Code == 0xFF)
        return 0x178;
    if (Code >= 0x100 && Code <= 0x17F)
    {
        // Pairs of upper and lower case, upper first, with the dotless i and the runs whose
        // pairs start at an odd unit (U+0139..U+0148, U+0179..U+017E) as exceptions.
        if (Code == 0x131 || Code == 0x138 || Code == 0x149 || Code == 0x17F)
            return Code;
        bool const bOddPairs = (Code >= 0x139 && Code <= 0x148) || (Code >= 0x179 && Code <= 0x17E);
        bool const bLower = bOddPairs ? (Code % 2 == 0) : (Code % 2 == 1);
        return bLower ? Code - 1 : Code;
    }
    if (Code >= 0x3B1 && Code <= 0x3CB && Code != 0x3C2) // alpha..upsilon with dialytika
        return Code - 32;
    if (Code >= 0x430 && Code <= 0x44F)
        return Code - 32;
    if (Code >= 0x450 && Code <= 0x45F)
        return Code - 80;
    return Code;
}

// Whether the directory string at Relative (from the root) is Text, without regard to case
// (Upper).
inline bool NameIs(FImage const& Image, std::uint32_t Relative, char const* Text)
{
    if (Relative > Image.RootSize || Image.RootSize - Relative < 2)
        return false;
    std::uint32_t const Offset = Image.Root + Relative;
    std::uint32_t const Length = Read16(Image, Offset);
    if (Length * 2ull > Image.RootSize - Relative - 2)
        return false;
    unsigned char const* Cursor = reinterpret_cast<unsigned char const*>(Text);
    for (std::uint32_t Index = 0; Index < Length; ++Index)
    {
        if (*Cursor == 0)
            return false;
        std::uint32_t Code = NextCodePoint(Cursor);
        std::uint32_t Unit = Read16(Image, Offset + 2 + Index * 2);
        if (Code >= 0x10000)
        {
            // A surrogate pair in the directory.
            Code -= 0x10000;
            if (Unit != 0xD800 + (Code >> 10) || Index + 1 == Length)
                return false;
            ++Index;
            Unit = Read16(Image, Offset + 2 + Index * 2);
            Code = 0xDC00 + (Code & 0x3FF);
        }
        if (Upper(Code) != Upper(Unit))
            return false;
    }
    return *Cursor == 0;
}

// The OffsetToData field of the entry for Key in the directory at Relative, or false.
inline bool Child(FImage const& Image, std::uint32_t Relative, FName const& Key,
                  std::uint32_t& Target)
{
    std::uint32_t First = 0, Named = 0, Ids = 0;
    if (Key.bInvalid || !Directory(Image, Relative, First, Named, Ids))
        return false;
    if (Key.Text != nullptr)
    {
        for (std::uint32_t Index = 0; Index < Named; ++Index)
        {
            std::uint32_t const Entry = First + Index * 8;
            std::uint32_t const Name = Read32(Image, Entry);
            if ((Name & 0x80000000u) != 0 && NameIs(Image, Name & 0x7FFFFFFFu, Key.Text))
            {
                Target = Read32(Image, Entry + 4);
                return true;
            }
        }
        return false;
    }
    // The id entries, sorted by id.
    std::uint32_t Low = 0, High = Ids;
    while (Low < High)
    {
        std::uint32_t const Middle = Low + (High - Low) / 2;
        std::uint32_t const Entry = First + (Named + Middle) * 8;
        std::uint32_t const Name = Read32(Image, Entry);
        std::uint32_t const Id = (Name & 0x80000000u) != 0 ? 0x10000u : (Name & 0xFFFFu);
        if (Id == Key.Id)
        {
            Target = Read32(Image, Entry + 4);
            return true;
        }
        if (Id < Key.Id)
            Low = Middle + 1;
        else
            High = Middle;
    }
    return false;
}

inline bool IsDirectory(std::uint32_t Target)
{
    return (Target & 0x80000000u) != 0;
}
} // namespace Detail

// The image at Data: false when it is not a PE32 / PE32+ image. One without resources opens,
// and finds nothing.
inline bool Open(unsigned char const* Data, std::size_t Size, FImage& Image)
{
    Image = FImage{};
    Image.Data = Data;
    Image.Size = Size;
    using Detail::Read16;
    using Detail::Read32;
    // File offsets are 32-bit here, as in the format: a larger file is not one of these images.
    if (Size > 0xFFFFFFFFu)
        return false;
    if (Data == nullptr || !Detail::Fits(Image, 0, 0x40) || Data[0] != 'M' || Data[1] != 'Z')
        return false;
    std::uint32_t const Pe = Read32(Image, 0x3C);
    if (!Detail::Fits(Image, Pe, 24) || Read32(Image, Pe) != 0x00004550u) // "PE\0\0"
        return false;
    std::uint32_t const SectionCount = Read16(Image, Pe + 6);
    std::uint32_t const OptionalSize = Read16(Image, Pe + 20);
    std::uint32_t const Optional = Pe + 24;
    if (!Detail::Fits(Image, Optional, OptionalSize) || OptionalSize < 2)
        return false;
    std::uint32_t Directories = 0; // the data directories' offset in the optional header
    switch (Read16(Image, Optional))
    {
    case 0x10B: // PE32
        Directories = 96;
        break;
    case 0x20B: // PE32+
        Directories = 112;
        break;
    default:
        return false;
    }
    if (OptionalSize < Directories)
        return false;
    std::uint32_t const DirectoryCount = Read32(Image, Optional + Directories - 4);
    // The Windows loader takes at most 96 sections; more would only lengthen every RVA lookup.
    if (SectionCount > 96)
        return false;
    Image.Sections = Optional + OptionalSize;
    if (!Detail::Fits(Image, Image.Sections, SectionCount * 40ull))
        return false;
    Image.SectionCount = SectionCount;

    // IMAGE_DIRECTORY_ENTRY_RESOURCE (2), when the header holds it and it is not empty.
    std::uint32_t const Entry = Directories + 2 * 8;
    if (DirectoryCount <= 2 || OptionalSize < Entry + 8)
        return true;
    std::uint32_t const Rva = Read32(Image, Optional + Entry);
    std::uint32_t Length = Read32(Image, Optional + Entry + 4);
    if (Rva == 0 || Length < 16)
        return true;
    // The directory may claim more than the section holds in the file: only the file's part.
    std::uint32_t Root = 0;
    if (!Detail::RvaToOffset(Image, Rva, 16, Root))
        return true;
    for (std::uint32_t Index = 0; Index < SectionCount; ++Index)
    {
        std::uint32_t const Header = Image.Sections + Index * 40u;
        std::uint32_t const Address = Read32(Image, Header + 12);
        std::uint32_t const RawSize = Read32(Image, Header + 16);
        if (Rva >= Address && Rva - Address < RawSize && Length > RawSize - (Rva - Address))
            Length = RawSize - (Rva - Address);
    }
    // Nor more than the file holds after the root (a truncated image).
    if (Length > Image.Size - Root)
        Length = static_cast<std::uint32_t>(Image.Size - Root);
    Image.Root = Root;
    Image.RootSize = Length;
    return true;
}

namespace Detail
{
// The data entry of a resource, by the loader's lookup (see Find).
inline bool FindEntry(FImage const& Image, FName const& Type, FName const& Name,
                      std::uint16_t Language, std::uint32_t& DataEntry)
{
    if (Image.Root == 0)
        return false;
    std::uint32_t Target = 0;
    if (!Detail::Child(Image, 0, Type, Target) || !Detail::IsDirectory(Target))
        return false;
    if (!Detail::Child(Image, Target & 0x7FFFFFFFu, Name, Target) || !Detail::IsDirectory(Target))
        return false;
    std::uint32_t const Languages = Target & 0x7FFFFFFFu;

    // The languages to try, in order (see the top of the file).
    std::uint16_t constexpr kEnglishUs = 0x0409;
    std::uint16_t Order[8];
    int Count = 0;
    Order[Count++] = Language;
    Order[Count++] = static_cast<std::uint16_t>(Language & 0x3FF); // SUBLANG_NEUTRAL
    Order[Count++] = 0; // LANG_NEUTRAL
    bool const bNeutral = (Language & 0x3FF) == 0;
    if (bNeutral)
    {
        if ((Language >> 10) != 2) // SUBLANG_SYS_DEFAULT skips the user's language
            Order[Count++] = kEnglishUs;
        Order[Count++] = kEnglishUs & 0x3FF;
        Order[Count++] = kEnglishUs; // LANG_ENGLISH, SUBLANG_DEFAULT
    }
    for (int Index = 0; Index < Count; ++Index)
    {
        FName Key;
        Key.Id = Order[Index];
        if (Detail::Child(Image, Languages, Key, Target))
        {
            if (Detail::IsDirectory(Target))
                return false;
            DataEntry = Image.Root + Target;
            return Target <= Image.RootSize && Image.RootSize - Target >= 16;
        }
    }
    if (!bNeutral)
        return false;
    // The first language the resource has.
    std::uint32_t First = 0, Named = 0, Ids = 0;
    if (!Detail::Directory(Image, Languages, First, Named, Ids) || Named + Ids == 0)
        return false;
    Target = Detail::Read32(Image, First + 4);
    if (Detail::IsDirectory(Target) || Target > Image.RootSize || Image.RootSize - Target < 16)
        return false;
    DataEntry = Image.Root + Target;
    return true;
}
} // namespace Detail

inline bool Data(FImage const& Image, std::uint32_t DataEntry, std::uint32_t& Offset,
                 std::uint32_t& Size);

// The data entry (IMAGE_RESOURCE_DATA_ENTRY) of a resource: its file offset, as Windows' HRSRC
// points at it. False when there is no such resource. A string table block (RT_STRING) is found
// only when its 16 length-prefixed strings lie within its data, as every resource compiler
// writes them: the game walks a block in place without bounds (GetIdsStringW, LoadIdsIntoRdl),
// so a damaged one is refused here rather than read past.
inline bool Find(FImage const& Image, FName const& Type, FName const& Name, std::uint16_t Language,
                 std::uint32_t& DataEntry)
{
    if (!Detail::FindEntry(Image, Type, Name, Language, DataEntry))
        return false;
    if (Type.Text != nullptr || Type.Id != 6)
        return true;
    std::uint32_t Offset = 0, Size = 0;
    if (!Data(Image, DataEntry, Offset, Size))
        return false;
    std::uint32_t const Units = Size / 2;
    std::uint32_t Cursor = 0;
    for (int Entry = 0; Entry < 16; ++Entry)
    {
        if (Cursor >= Units)
            return false;
        std::uint32_t const Length = Detail::Read16(Image, Offset + Cursor * 2);
        if (Length > Units - Cursor - 1)
            return false;
        Cursor += Length + 1;
    }
    return true;
}

// A data entry's data: its file offset and size. False when the entry is not one of the
// image's, its data is not all in the file, or it starts at an odd offset: the game reads
// string tables and infocards in place as 16-bit units, which an odd address cannot hold (the
// resource compilers align data to 4 or 8 bytes, so only a damaged image has one).
inline bool Data(FImage const& Image, std::uint32_t DataEntry, std::uint32_t& Offset,
                 std::uint32_t& Size)
{
    if (Image.Root == 0 || DataEntry < Image.Root || DataEntry - Image.Root > Image.RootSize ||
        Image.RootSize - (DataEntry - Image.Root) < 16)
        return false;
    Size = Detail::Read32(Image, DataEntry + 4);
    return Detail::RvaToOffset(Image, Detail::Read32(Image, DataEntry), Size, Offset) &&
           Offset % 2 == 0;
}

// Calls Visit(DataOffset, Size) for every resource's data (type, name and language directories;
// deeper trees are not resources and are skipped). A well-formed tree has at most RootSize / 8
// entries, each visited once; the walk stops after that many, so a damaged tree whose entries
// point back at their own directories cannot make it run longer.
template <class TVisit>
void ForEach(FImage const& Image, TVisit&& Visit)
{
    if (Image.Root == 0)
        return;
    std::uint32_t Budget = Image.RootSize / 8;
    auto Walk = [&](auto& Self, std::uint32_t Relative, int Level) -> void
    {
        std::uint32_t First = 0, Named = 0, Ids = 0;
        if (!Detail::Directory(Image, Relative, First, Named, Ids))
            return;
        for (std::uint32_t Index = 0; Index < Named + Ids; ++Index)
        {
            if (Budget == 0)
                return;
            --Budget;
            std::uint32_t const Target = Detail::Read32(Image, First + Index * 8 + 4);
            if (Detail::IsDirectory(Target))
            {
                if (Level < 2)
                    Self(Self, Target & 0x7FFFFFFFu, Level + 1);
                continue;
            }
            std::uint32_t Offset = 0, Size = 0;
            if (Level == 2 && Data(Image, Image.Root + Target, Offset, Size))
                Visit(Offset, Size);
        }
    };
    Walk(Walk, 0, 0);
}

// String Id of a string table (RT_STRING block (Id >> 4) + 1, entry Id & 15, as LoadString
// finds it): its UTF-16 units' file offset and count. False when the block is missing or the
// entry runs past it; an empty string is found, with Length 0.
inline bool String(FImage const& Image, std::uint32_t Id, std::uint16_t Language,
                   std::uint32_t& Offset, std::uint32_t& Length)
{
    FName Type;
    Type.Id = 6;
    FName Block;
    Block.Id = static_cast<std::uint16_t>(((Id & 0xFFFF) >> 4) + 1);
    std::uint32_t Entry = 0, Data = 0, Size = 0;
    if (!Find(Image, Type, Block, Language, Entry) || !PeResources::Data(Image, Entry, Data, Size))
        return false;
    std::uint32_t Cursor = 0; // in units
    for (std::uint32_t Skip = Id & 15;; --Skip)
    {
        if (Size / 2 <= Cursor)
            return false;
        std::uint32_t const Units = Detail::Read16(Image, Data + Cursor * 2);
        if (Units > Size / 2 - Cursor - 1)
            return false;
        if (Skip == 0)
        {
            Offset = Data + (Cursor + 1) * 2;
            Length = Units;
            return true;
        }
        Cursor += Units + 1;
    }
}
} // namespace PeResources
