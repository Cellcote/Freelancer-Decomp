// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS, Linux): checks of the resource reader (pe_resources.h, Platform::ResourceModuleLoad)
// and of the Win32 resource calls over it (posix/kernel32.h's FindResourceExA, posix/winuser.h's
// LoadStringW, resource_text.h), on the resource DLLs tools/build_resource_dll.py makes: the
// game's (PE32) and resource_test.rc's (PE32+). Built against the stand-in <windows.h>, as the
// client is. The exit code is the number of failures.
//
// Usage: resource_tests <bin dir with resources.dll / InfoCards.dll> <src/Resources>
//                       <resource_test.dll>

#include <windows.h>

#include "resource_text.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include <unistd.h>

namespace
{
int G_Failures = 0;

void Check(bool bOk, const char* What)
{
    if (!bOk)
    {
        std::printf("FAIL: %s\n", What);
        ++G_Failures;
    }
}

std::vector<unsigned char> ReadAll(std::string const& Path)
{
    std::ifstream In(Path, std::ios::binary);
    return std::vector<unsigned char>(std::istreambuf_iterator<char>(In), {});
}

bool WriteAll(std::string const& Path, unsigned char const* Data, std::size_t Size)
{
    std::ofstream Out(Path, std::ios::binary | std::ios::trunc);
    Out.write(reinterpret_cast<char const*>(Data), static_cast<std::streamsize>(Size));
    Out.close();
    return !Out.fail();
}

    // The resource's bytes, or empty.
std::vector<unsigned char> Resource(HMODULE Module, LPCSTR Type, LPCSTR Name, WORD Language = 0)
{
    HRSRC const Info = FindResourceExA(Module, Type, Name, Language);
    if (Info == nullptr)
        return {};
    auto const* Data = static_cast<unsigned char const*>(LockResource(LoadResource(Module, Info)));
    if (Data == nullptr)
        return {};
    return std::vector<unsigned char>(Data, Data + SizeofResource(Module, Info));
}

    // The game's resources.dll: a PE32 image from resources.rc.
void TestGameStrings(std::string const& Bin)
{
    HMODULE const Module = Platform::ResourceModuleLoad((Bin + "/resources.dll").c_str());
    Check(Module != nullptr, "resources.dll loads");
    if (Module == nullptr)
        return;

    wchar_t Text[256];
    Check(LoadStringW(Module, 209, Text, 256) == 22 &&
              std::wcscmp(Text, L"Freelancer Fatal Error") == 0,
          "resources.dll string 209 is \"Freelancer Fatal Error\"");
    Check(LoadStringW(Module, 0, Text, 256) == 14 && std::wcscmp(Text, L"Object Unknown") == 0,
          "resources.dll string 0 is \"Object Unknown\"");
    Check(LoadStringW(Module, 5, Text, 256) == 1 && std::wcscmp(Text, L"\n") == 0,
          "resources.dll string 5 is a line feed");
        // Id 4 is not in the script: its block exists, the entry is empty.
    Text[0] = L'x';
    Check(LoadStringW(Module, 4, Text, 256) == 0 && Text[0] == 0,
          "an empty entry gives 0 and an empty buffer");
        // No block 0xFFF holds anything: the buffer is left as it is, as Windows does.
    Text[0] = L'x';
    Check(LoadStringW(Module, 0xFFF0, Text, 256) == 0 && Text[0] == L'x',
          "a missing string gives 0 and leaves the buffer");

    // The string tables are English (US), found from a neutral request; asked for in German,
    // which the module does not have, they are not.
    Check(FindResourceExA(Module, RT_STRING, MAKEINTRESOURCEA(14), 0) != nullptr,
          "a neutral request finds the English (US) block");
    Check(FindResourceExA(Module, RT_STRING, MAKEINTRESOURCEA(14), 0x0409) != nullptr,
          "an English (US) request finds it");
    Check(FindResourceExA(Module, RT_STRING, MAKEINTRESOURCEA(14), 0x0407) == nullptr,
          "a German request does not");
    Check(FindResourceExA(Module, RT_STRING, "#14", 0) != nullptr, "\"#14\" is id 14");

    // A resource module has no exports, its path is its file's, and it is no dlopen handle.
    Check(GetProcAddress(Module, "DllMain") == nullptr, "GetProcAddress finds nothing in it");
    char Path[4096] = {};
    DWORD const Length = GetModuleFileNameA(Module, Path, sizeof(Path));
    Check(Length > 13 && std::strcmp(Path + Length - 13, "resources.dll") == 0,
          "GetModuleFileNameA names the file");
    Check(FreeLibrary(Module) == TRUE, "FreeLibrary frees it");
    Check(FindResourceExA(Module, RT_STRING, MAKEINTRESOURCEA(14), 0) == nullptr,
          "a freed module finds nothing");
}

// InfoCards.dll: type 23 (HTML) resources, the files' bytes as they are.
void TestInfocards(std::string const& Bin, std::string const& Sources)
{
    HMODULE const Module = Platform::ResourceModuleLoad((Bin + "/InfoCards.dll").c_str());
    Check(Module != nullptr, "InfoCards.dll loads");
    if (Module == nullptr)
        return;
    std::vector<unsigned char> const Card =
        Resource(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(3));
    std::vector<unsigned char> const File = ReadAll(Sources + "/InfoCards/HTML3_1.txt");
    Check(!File.empty() && Card == File, "infocard 3 is HTML3_1.txt");
    Check(FindResourceExA(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(2), 0) == nullptr,
          "infocard 2 is not in InfoCards.dll");

    // The text after the byte order mark, as wchar_t (ResourceWide), which lives as long as
    // the module.
    HRSRC const Info = FindResourceExA(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(3), 0);
    auto const* Units = static_cast<ResourceUnit const*>(LockResource(LoadResource(Module, Info)));
    Check(Units != nullptr && Units[0] == 0xFEFF, "infocard 3 starts with a byte order mark");
    if (Units != nullptr)
    {
        std::size_t const Count = SizeofResource(Module, Info) / 2 - 1;
        wchar_t const* const Wide = ResourceWide(Units + 1, Count);
        Check(Wide != nullptr && std::wcsncmp(Wide, L"<RDL><PUSH/>", 12) == 0,
              "infocard 3 as wchar_t is \"<RDL><PUSH/>...\"");
        Check(ResourceWide(Units + 1, Count) == Wide, "the wchar_t copy is made once");
        Check(ResourceWide(Units + 1, Count + 1) == nullptr,
              "no copy holds more units than the resource");
        Check(Wide != nullptr && Wide[SizeofResource(Module, Info) / 2 - 1] == 0,
              "the wchar_t copy is terminated");
        Check(Platform::ResourceWideText(reinterpret_cast<unsigned char const*>(Units) + 1) ==
                  nullptr,
              "a pointer between two units has no wchar_t copy");
    }
    FreeLibrary(Module);
}

// resource_test.rc: a PE32+ image.
void TestFixture(std::string const& Fixture)
{
    std::vector<unsigned char> const Image = ReadAll(Fixture);
    Check(Image.size() > 0x100 && Image[0x40 + 4] == 0x64 && Image[0x40 + 5] == 0x86,
          "the fixture is an AMD64 image");
    // The optional header's magic, which is what makes the reader take the PE32+ layout.
    Check(Image.size() > 0x100 && Image[0x40 + 24] == 0x0B && Image[0x40 + 25] == 0x02,
          "the fixture has a PE32+ optional header");
    HMODULE const Module = Platform::ResourceModuleLoad(Fixture.c_str());
    Check(Module != nullptr, "a PE32+ image loads");
    if (Module == nullptr)
        return;

    wchar_t Text[64];
    Check(LoadStringW(Module, 1, Text, 64) == 3 && std::wcscmp(Text, L"One") == 0,
          "LoadStringW takes the English (US) table for a neutral request");
    Check(LoadStringW(Module, 2, Text, 64) == 13 && std::wcscmp(Text, L"Two \"quoted\"\n") == 0,
          "\"\" is a quote and \\n a line feed");
    Check(LoadStringW(Module, 17, Text, 64) == 8 && std::wcscmp(Text, L"Café ’ ☺") == 0,
          "UTF-8 text and \\x escapes become UTF-16, one wchar_t a unit");
    Check(LoadStringW(Module, 31, Text, 64) == 11 && std::wcscmp(Text, L"Joined text") == 0,
          "adjacent strings are joined");
    Check(LoadStringW(Module, 18, Text, 64) == 5 && std::wcscmp(Text, L"\u00a92001") == 0,
          "\\x takes two digits in a narrow string");
    Check(LoadStringW(Module, 33, Text, 64) == 3 && std::wcscmp(Text, L"Sum") == 0,
          "an id 32+1 is 33");
    static wchar_t const kSmile[] = {0xD83D, 0xDE00, 0};
    Check(LoadStringW(Module, 48, Text, 64) == 2 && std::wcscmp(Text, kSmile) == 0,
          "an escaped surrogate pair is kept");
    Check(LoadStringW(Module, 49, Text, 64) == 2 && std::wcscmp(Text, kSmile) == 0,
          "a supplementary character is the same surrogate pair");
    Check(LoadStringW(Module, 50, Text, 64) == 11 && std::wcscmp(Text, L"Hello world") == 0,
          "a line continuation joins the lines");
    Check(LoadStringW(Module, 51, Text, 64) == 9 && std::wcscmp(Text, L"Commented") == 0,
          "a comment ends an id");

    // The buffer: at most Size - 1 characters and a terminator.
    std::wmemset(Text, L'x', 64);
    Check(LoadStringW(Module, 1, Text, 3) == 2 && std::wcscmp(Text, L"On") == 0 && Text[3] == L'x',
          "a short buffer takes Size - 1 characters, terminated");
    std::wmemset(Text, L'x', 64);
    Check(LoadStringW(Module, 1, Text, 1) == 0 && Text[0] == 0 && Text[1] == L'x',
          "a one-character buffer takes only the terminator");
    Check(LoadStringW(Module, 1, nullptr, 64) == 0, "no buffer gives 0");
    Check(LoadStringW(Module, 1, Text, -1) == 0, "a negative size gives 0");
    wchar_t const* Pointer = nullptr;
    Check(LoadStringW(Module, 1, reinterpret_cast<LPWSTR>(&Pointer), 0) == 3 &&
              Pointer != nullptr && std::wcsncmp(Pointer, L"One", 3) == 0,
          "size 0 hands out a pointer to the string and its length");
    Pointer = nullptr;
    Check(LoadStringW(Module, 15, reinterpret_cast<LPWSTR>(&Pointer), 0) == 0 &&
              Pointer != nullptr && Pointer[0] == 0,
          "size 0 hands out an empty string for an empty entry");
    Pointer = nullptr;
    Check(LoadStringW(Module, 0xFFF0, reinterpret_cast<LPWSTR>(&Pointer), 0) == 0 &&
              Pointer == nullptr,
          "size 0 leaves the pointer for a missing string");
    Check(LoadStringW(Module, 0x10001, Text, 64) == 3 && std::wcscmp(Text, L"One") == 0,
          "LoadStringW takes the id's low word");

    // Languages: an exact request, a request for a missing sublanguage (falls to the primary
    // language with SUBLANG_NEUTRAL, then LANG_NEUTRAL: neither is here) and a resource that
    // exists only in German (a neutral request ends on the first language there is).
    std::vector<unsigned char> const German =
        Resource(Module, RT_STRING, MAKEINTRESOURCEA(1), 0x0407);
    Check(German.size() > 10 && German[4] == 'E' && German[6] == 'i',
          "a German request finds the German block");
    Check(Resource(Module, RT_STRING, MAKEINTRESOURCEA(1), 0x0807).empty(),
          "Swiss German finds nothing (no German with SUBLANG_NEUTRAL or neutral block)");
    std::vector<unsigned char> const Card =
        Resource(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(7));
    Check(Card.size() == 15 && std::memcmp(Card.data(), "raw card bytes\n", 15) == 0,
          "a neutral request takes the only (German) language");
    Check(Resource(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(7), 0x0409).empty(),
          "an English request does not");
    Check(!Resource(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(5)).empty(), "type 23, id 5");

    // Names and types as strings, without regard to case.
    Check(!Resource(Module, "CARDTYPE", "CARDNAME").empty(), "a named type and name");
    Check(!Resource(Module, "cardtype", "CardName").empty(), "names match without regard to case");
    Check(FindResourceA(Module, "CardName", "CardType") != nullptr, "FindResourceA (name, type)");
    Check(!Resource(Module, "CARDTYPE", "caf\u00e9").empty() &&
              !Resource(Module, "CARDTYPE", "CAF\u00c9").empty(),
          "non-ASCII names match without regard to case");
    Check(!Resource(Module, "CARDTYPE", "stra\u00dfe").empty() &&
              Resource(Module, "CARDTYPE", "STRASSE").empty(),
          "the generator folds names as the reader does ('\u00df' stays one character)");
    // "CARDNAME" with its 'C' in an overlong three-byte form: a decoder that took that form as
    // 'C' would find the resource.
    Check(Resource(Module, "CARDTYPE",
                   "\xe0\x81\x83"
                   "ARDNAME")
              .empty(),
          "an overlong UTF-8 form is no character");
    Check(Resource(Module, "CARDTYPE", "#CARDNAME").empty() &&
              Resource(Module, "CARDTYPE", "#0").empty(),
          "a malformed \"#\" name is not looked up as a string");
    Check(Resource(Module, "CARDTYPE", "CARDNAMEX").empty(), "a longer name is another name");
    Check(Resource(Module, "CARDTYPE", "CARD").empty(), "a shorter name is another name");
    Check(Resource(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(6)).empty(), "a missing id");
    Check(Resource(Module, MAKEINTRESOURCEA(24), MAKEINTRESOURCEA(5)).empty(), "a missing type");
    Check(FindResourceExA(Module, RT_STRING, "#", 0) == nullptr, "\"#\" names nothing");
    Check(FindResourceExA(Module, RT_STRING, "#99999", 0) == nullptr, "\"#99999\" names nothing");
    Check(FindResourceExA(Module, RT_STRING, "#0", 0) == nullptr, "\"#0\" names nothing");

    // The executable and a made-up handle have no PE resources here.
    Check(FindResourceExA(nullptr, RT_STRING, MAKEINTRESOURCEA(1), 0) == nullptr,
          "the executable has no resources");
    Check(FindResourceExA(GetModuleHandleA(nullptr), RT_STRING, MAKEINTRESOURCEA(1), 0) == nullptr,
          "nor has its module handle");
    Check(SizeofResource(Module, reinterpret_cast<HRSRC>(Text)) == 0 &&
              LoadResource(Module, reinterpret_cast<HRSRC>(Text)) == nullptr,
          "an HRSRC that is not one gives nothing");
    FreeLibrary(Module);
}

// Damaged images: every truncation and a run of corrupted bytes must load or fail cleanly,
// and every lookup in them stay inside the file (the sanitizer build checks that).
void TestDamaged(std::string const& Fixture, std::string const& Scratch)
{
    std::vector<unsigned char> const Image = ReadAll(Fixture);
    int WriteFailures = 0;
    auto Probe = [&](std::vector<unsigned char> const& Bytes)
    {
        if (!WriteAll(Scratch, Bytes.data(), Bytes.size()))
        {
            ++WriteFailures;
            return;
        }
        HMODULE const Module = Platform::ResourceModuleLoad(Scratch.c_str());
        if (Module == nullptr)
            return;
        wchar_t Text[64];
        for (unsigned Id : {1u, 2u, 3u, 17u, 31u})
            LoadStringW(Module, Id, Text, 64);
        for (unsigned Id : {5u, 7u})
        {
            std::vector<unsigned char> const Data =
                Resource(Module, MAKEINTRESOURCEA(23), MAKEINTRESOURCEA(Id));
            (void)Data;
        }
        Resource(Module, "CARDTYPE", "CARDNAME");
        HRSRC const Info = FindResourceExA(Module, RT_STRING, MAKEINTRESOURCEA(1), 0);
        if (Info != nullptr && LoadResource(Module, Info) != nullptr)
            ResourceWide(static_cast<ResourceUnit const*>(LoadResource(Module, Info)), 1);
        FreeLibrary(Module);
    };
    for (std::size_t Length = 0; Length <= Image.size(); ++Length)
        Probe(std::vector<unsigned char>(Image.begin(),
                                         Image.begin() + static_cast<std::ptrdiff_t>(Length)));
    for (std::size_t At = 0; At < Image.size(); ++At)
    {
        for (unsigned char Value : {0x00, 0x7F, 0x80, 0xFF})
        {
            std::vector<unsigned char> Bytes = Image;
            Bytes[At] = Value;
            Probe(Bytes);
        }
    }
    Check(WriteFailures == 0, "every damaged image was written for its probe");
    unlink(Scratch.c_str());

    // A string table block whose first length runs past the block: FindResource refuses it,
    // since the game walks blocks without bounds.
    {
        HMODULE const Module = Platform::ResourceModuleLoad(Fixture.c_str());
        HRSRC const Info = FindResourceExA(Module, RT_STRING, MAKEINTRESOURCEA(1), 0);
        auto const* Block = static_cast<unsigned char const*>(LoadResource(Module, Info));
        std::ptrdiff_t const At =
            Block != nullptr ? Block - static_cast<unsigned char const*>(Module) : -1;
        FreeLibrary(Module);
        Check(At > 0, "the fixture's first string block is found");
        if (At > 0)
        {
            std::vector<unsigned char> Bytes = Image;
            Bytes[static_cast<std::size_t>(At)] = 0xFF;
            Bytes[static_cast<std::size_t>(At) + 1] = 0xFF;
            HMODULE const Damaged = WriteAll(Scratch, Bytes.data(), Bytes.size())
                                        ? Platform::ResourceModuleLoad(Scratch.c_str())
                                        : nullptr;
            Check(Damaged != nullptr &&
                      FindResourceExA(Damaged, RT_STRING, MAKEINTRESOURCEA(1), 0) == nullptr,
                  "a string block whose lengths run past it is not found");
            wchar_t Text[8] = L"x";
            Check(Damaged != nullptr && LoadStringW(Damaged, 1, Text, 8) == 0 && Text[0] == L'x',
                  "nor is its string");
            if (Damaged != nullptr)
                FreeLibrary(Damaged);
            unlink(Scratch.c_str());
        }
    }

    Check(Platform::ResourceModuleLoad("/nonexistent/resources.dll") == nullptr,
          "a missing file does not load");
    static unsigned char const kText[] =
        "MZ but not a PE image, only text that goes on for a while......";
    Check(WriteAll(Scratch, kText, sizeof(kText)) &&
              Platform::ResourceModuleLoad(Scratch.c_str()) == nullptr,
          "a file that is not a PE image does not load");
    unlink(Scratch.c_str());
}
}

FL_PLATFORM_DEFINE_SHARED()

int main(int Count, char** Arguments)
{
    if (Count != 4)
    {
        std::printf("usage: resource_tests <bin dir> <src/Resources> <resource_test.dll>\n");
        return 1;
    }
    std::string const Bin = Arguments[1];
    std::string const Sources = Arguments[2];
    std::string const Fixture = Arguments[3];

    TestGameStrings(Bin);
    TestInfocards(Bin, Sources);
    TestFixture(Fixture);
    // The damaged images go into a directory of this run's own (two runs may share a fixture).
    char const* const Temporary = std::getenv("TMPDIR");
    std::string Pattern =
        std::string(Temporary != nullptr && *Temporary != 0 ? Temporary : "/tmp") +
        "/resource_tests.XXXXXX";
    if (mkdtemp(Pattern.data()) == nullptr)
    {
        std::printf("FAIL: no scratch directory\n");
        ++G_Failures;
    }
    else
    {
        TestDamaged(Fixture, Pattern + "/damaged.dll");
        rmdir(Pattern.c_str());
    }

    if (G_Failures == 0)
        std::printf("resource_tests: all passed\n");
    return G_Failures;
}
