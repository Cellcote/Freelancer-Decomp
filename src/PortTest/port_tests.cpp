// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): checks of the port's platform headers outside Windows (src/PortTest/CMakeLists.txt).
// Each check prints what failed; the exit code is the number of failures.

#include "handle_value.h"
#include "platform.h"
#include "x64_low_arena.h"

// port_tests uses the arena without dacom, so it holds the process's arena itself.
FL_LOW_ARENA_DEFINE_SHARED()
FL_HANDLE_VALUE_DEFINE_SHARED()
FL_PLATFORM_DEFINE_SHARED()

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
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

#if defined(FL_PTR64)
    struct FHandle
    {
        FL_LOW_ARENA_NEW
        int Value = 0;
        char Payload[40] = {};
    };

    // Retail keeps arena handles in 32-bit fields and reads them back: every block must survive
    // Encode and Decode, as a positive value (retail sign-extends it), and 0 / -1 keep meaning
    // null / invalid.
    void TestLowArenaRoundTrip()
    {
        std::vector<FHandle*> Handles;
        for (int Index = 0; Index < 10000; ++Index)
        {
            FHandle* const Handle = new FHandle();
            Handle->Value = Index;
            Handles.push_back(Handle);
            const FlLong Value = LowArena::Encode(Handle);
            Check(Value > 0, "an arena block encodes to a positive 32-bit value");
            Check(LowArena::Decode<FHandle>(Value) == Handle, "Decode(Encode(block)) is the block");
        }
        for (int Index = 0; Index < 10000; ++Index)
            Check(Handles[Index]->Value == Index, "arena blocks keep their contents");
        Check(LowArena::Encode(nullptr) == 0, "null encodes to 0");
        Check(LowArena::Decode<FHandle>(0) == nullptr, "0 decodes to null");
        Check(LowArena::Decode<FHandle>(-1) == (FHandle*)(std::intptr_t)(-1), "-1 stays the invalid handle");
        Check(LowArena::Encode((const void*)(std::intptr_t)(-1)) == -1, "the invalid handle encodes to -1");
        for (FHandle* Handle : Handles)
            delete Handle;
    }

    // A freed block is handed out again for the same size.
    void TestLowArenaReuse()
    {
        void* const First = LowArena::Allocate(48);
        LowArena::Free(First, 48);
        void* const Second = LowArena::Allocate(48);
        Check(First == Second, "a freed block is reused for the same size");
        LowArena::Free(Second, 48);
    }

    // A block larger than a chunk gets a chunk of its own and stays usable and encodable.
    void TestLowArenaLargeBlock()
    {
        const std::size_t Size = 40u << 20;
        char* const Block = static_cast<char*>(LowArena::Allocate(Size));
        std::memset(Block, 0x5A, Size);
        Check(Block[0] == 0x5A && Block[Size - 1] == 0x5A, "a large block is committed end to end");
        Check(LowArena::Decode<char>(LowArena::Encode(Block)) == Block, "a large block round-trips");
        LowArena::Free(Block, Size);
    }
#endif

    // A new private scratch directory under the temp folder (mkdtemp), removed again by the test
    // that made it. Empty on failure.
    std::string ScratchDirectory()
    {
        char Temp[1024];
        if (Platform::TempDirectory(Temp, sizeof(Temp)) == 0)
            return {};
        std::string Template = std::string(Temp) + "port_tests_XXXXXX";
        if (mkdtemp(Template.data()) == nullptr)
            return {};
        return Template;
    }

    // Retail paths use '\' and spell names in another case than the disk does.
    void TestPlatformFiles()
    {
        std::string const Root = ScratchDirectory();
        Check(!Root.empty() && mkdir((Root + "/Data").c_str(), 0755) == 0 && mkdir((Root + "/Data/Sub").c_str(), 0755) == 0,
              "scratch directory made");
        if (Root.empty())
            return;
        std::string const Written = Root + "/Data/Sub/File.TXT";
        Platform::Handle File = Platform::FileOpen(Written.c_str(), Platform::kAccessWrite, 0, Platform::kCreateAlways);
        Check(File != Platform::InvalidHandle(), "FileOpen creates a file");
        FlULong Count = 0;
        Check(Platform::FileWrite(File, "freelancer", 10, &Count) && Count == 10, "FileWrite writes");
        Platform::CloseHandle(File);

        std::string const Retail = Root + "\\data\\SUB\\file.txt";
        File = Platform::FileOpen(Retail.c_str(), Platform::kAccessRead, Platform::kShareRead, Platform::kOpenExisting);
        Check(File != Platform::InvalidHandle(), "a '\\' path in another case opens the file");
        Check(Platform::FileGetSize(File) == 10, "FileGetSize");
        char Buffer[32] = {};
        Check(Platform::FileRead(File, Buffer, sizeof(Buffer), &Count) && Count == 10 &&
                  std::memcmp(Buffer, "freelancer", 10) == 0,
              "FileRead reads to the end");
        Platform::CloseHandle(File);
        Check(Platform::FileOpen((Root + "/missing").c_str(), Platform::kAccessRead, 0, Platform::kOpenExisting) ==
                  Platform::InvalidHandle(),
              "a missing file fails with InvalidHandle()");
        Check(Platform::PathAttributes((Root + "\\DATA").c_str()) == Platform::kAttributeDirectory, "a directory's attributes");
        Check(Platform::PathAttributes((Root + "/nothing").c_str()) == Platform::kInvalidAttributes, "a missing path's attributes");

        Check(Platform::FileCopy(Retail.c_str(), (Root + "/Data/Copy.txt").c_str(), true), "FileCopy");
        Check(!Platform::FileCopy(Retail.c_str(), (Root + "/Data/Copy.txt").c_str(), true), "FileCopy fails onto an existing file");
        Check(!Platform::FileCopy(Retail.c_str(), (Root + "/DATA/sub/FILE.txt").c_str(), false) &&
                  Platform::FileGetSize(File = Platform::FileOpen(Written.c_str(), Platform::kAccessRead, 0, Platform::kOpenExisting)) == 10,
              "FileCopy onto itself fails and leaves the file whole");
        Platform::CloseHandle(File);
        Check(Platform::FileOpen(Written.c_str(), Platform::kAccessRead, 0, Platform::kTruncateExisting) == Platform::InvalidHandle(),
              "truncation needs write access");
        Check(Platform::FileCopy(Retail.c_str(), (Root + "/Data/Other.txt").c_str(), true), "FileCopy to a second name");
        Check(!Platform::FileMove((Root + "/Data/Copy.txt").c_str(), (Root + "/Data/Other.txt").c_str()),
              "FileMove fails onto an existing file");
        Check(Platform::FileMove((Root + "/Data/Copy.txt").c_str(), (Root + "/Data/Moved.txt").c_str()), "FileMove");
        std::remove((Root + "/Data/Other.txt").c_str());

        Platform::FindData Found;
        int Matches = 0;
        Platform::Handle Search = Platform::FindFirst((Root + "\\data\\*.TXT").c_str(), &Found);
        Check(Search != Platform::InvalidHandle(), "FindFirst finds *.TXT without regard to case");
        while (Search != Platform::InvalidHandle())
        {
            ++Matches;
            Check(std::strcmp(Found.cFileName, "Moved.txt") == 0 && Found.nFileSizeLow == 10, "the found file's name and size");
            if (!Platform::FindNext(Search, &Found))
                break;
        }
        Check(Matches == 1, "FindFirst / FindNext see only the matching files");
        Check(Platform::Detail::Matches("*.*", "README") && Platform::Detail::Matches("A*b?C", "axxBzc") &&
                  !Platform::Detail::Matches("*.ini", "flplayer.in") && Platform::Detail::Matches("**x", "x"),
              "the wildcard rules");
        Check(!Platform::Detail::Matches("*a*a*a*a*a*a*a*a*a*a*b", std::string(4000, 'a').c_str()),
              "a pathological wildcard is rejected quickly");
        if (Search != Platform::InvalidHandle())
            Platform::FindClose(Search);

        std::string const Command = "rm -rf '" + Root + "'";
        Check(std::system(Command.c_str()) == 0, "scratch directory removed");
    }

    FlULong G_ThreadResult = 0;
    FlULong ThreadBody(void* Parameter)
    {
        G_ThreadResult = *static_cast<FlULong*>(Parameter) * 2;
        return 7;
    }

    FlULong TakeAndEnd(void* Mutex)
    {
        return Platform::WaitOne(Mutex, Platform::kWaitForever);
    }

    FlULong SetLater(void* Event)
    {
        Platform::SleepMs(20);
        Platform::EventSet(Event);
        return 0;
    }

    void TestPlatformThreadsAndWaits()
    {
        FlULong Input = 21;
        FlULong Id = 0;
        Platform::Handle Thread = Platform::ThreadCreate(&ThreadBody, &Input, 0, true, &Id);
        Check(Thread != nullptr && Id != 0 && Id != Platform::CurrentThreadId(), "ThreadCreate, suspended, with its own id");
        Check(Platform::WaitOne(Thread, 50) == Platform::kWaitTimeout, "a suspended thread does not run");
        Check(Platform::ThreadResume(Thread) == 1, "ThreadResume returns the previous suspend count");
        Check(Platform::WaitOne(Thread, Platform::kWaitForever) == Platform::kWaitSignaled && G_ThreadResult == 42,
              "a thread is signalled when it ends");
        Platform::CloseHandle(Thread);

        Platform::Handle Events[2] = {Platform::EventCreate(false, false), Platform::EventCreate(true, false)};
        Check(Platform::WaitMany(2, Events, false, 10) == Platform::kWaitTimeout, "WaitMany times out");
        Platform::EventSet(Events[1]);
        Check(Platform::WaitMany(2, Events, false, 0) == Platform::kWaitSignaled + 1, "WaitMany reports which object");
        Check(Platform::WaitOne(Events[1], 0) == Platform::kWaitSignaled, "a manual-reset event stays set");
        Platform::EventSet(Events[0]);
        Check(Platform::WaitOne(Events[0], 0) == Platform::kWaitSignaled, "an auto-reset event signals once");
        Check(Platform::WaitOne(Events[0], 0) == Platform::kWaitTimeout, "an auto-reset event resets itself");
        Platform::EventSet(Events[0]);
        Check(Platform::WaitMany(2, Events, true, 0) == Platform::kWaitSignaled, "WaitMany waits for all");
        Platform::CloseHandle(Events[0]);
        Platform::CloseHandle(Events[1]);

        Platform::Handle const NamedA = Platform::EventCreate(true, false, "PortTestEvent");
        Platform::Handle const NamedB = Platform::EventCreate(false, true, "PortTestEvent");
        Check(NamedA != nullptr && NamedB != nullptr && NamedA != NamedB,
              "a named event opens the existing one, with a handle of its own");
        Check(Platform::MutexCreate("PortTestEvent") == nullptr,
              "a name taken by an event is no mutex");
        Platform::EventSet(NamedB);
        Platform::CloseHandle(NamedB);
        Check(Platform::WaitOne(NamedA, 0) == Platform::kWaitSignaled, "a named event lives until its last handle closes");
        Platform::CloseHandle(NamedA);
        Platform::Handle const Fresh = Platform::EventCreate(false, false, "PortTestEvent");
        Check(Platform::WaitOne(Fresh, 0) == Platform::kWaitTimeout, "closing the last handle frees the name");
        Platform::CloseHandle(Fresh);

        Platform::Handle Mutex = Platform::MutexCreate();
        Check(Platform::WaitOne(Mutex, 0) == Platform::kWaitSignaled && Platform::WaitOne(Mutex, 0) == Platform::kWaitSignaled,
              "a mutex is recursive for its owner");
        Check(Platform::MutexRelease(Mutex) && Platform::MutexRelease(Mutex) && !Platform::MutexRelease(Mutex),
              "MutexRelease fails once the mutex is free");
        Thread = Platform::ThreadCreate(&TakeAndEnd, Mutex);
        Check(Platform::WaitOne(Thread, Platform::kWaitForever) == Platform::kWaitSignaled, "the mutex taker ends");
        Platform::CloseHandle(Thread);
        Check(Platform::WaitOne(Mutex, 1000) == Platform::kWaitAbandoned, "a mutex whose owner ended is abandoned");
        Check(Platform::MutexRelease(Mutex) && Platform::WaitOne(Mutex, 0) == Platform::kWaitSignaled,
              "an abandoned mutex is owned afterwards, and abandoned only once");
        Platform::MutexRelease(Mutex);
        Platform::CloseHandle(Mutex);

        // Handles are small numbers, as Windows kernel handles: they survive a 32-bit field.
        Platform::Handle const Small = Platform::EventCreate(true, true);
        std::uint32_t const Stored =
            static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(Small));
        Platform::Handle const Restored =
            reinterpret_cast<Platform::Handle>(static_cast<std::uintptr_t>(Stored));
        Check(reinterpret_cast<std::uintptr_t>(Small) % 4 == 0 && Restored == Small &&
                  Platform::WaitOne(Restored, 0) == Platform::kWaitSignaled,
              "a handle round-trips through 32 bits");
        Platform::CloseHandle(Small);
        Check(Platform::WaitOne(Small, 0) == Platform::kWaitFailed && !Platform::CloseHandle(Small),
              "a closed handle names nothing");
        Check(Platform::WaitOne(reinterpret_cast<Platform::Handle>(std::uintptr_t{0x7FFF0}), 0) ==
                  Platform::kWaitFailed,
              "a made-up handle names nothing");

        // A thread whose handle closes first still runs to its end.
        Platform::Handle const Done = Platform::EventCreate(true, false);
        Platform::CloseHandle(Platform::ThreadCreate(&SetLater, Done));
        Check(Platform::WaitOne(Done, 5000) == Platform::kWaitSignaled, "a thread outlives its closed handle");
        Platform::CloseHandle(Done);

        std::string const Root = ScratchDirectory();
        Platform::Handle const File = Root.empty() ? Platform::InvalidHandle()
                                                   : Platform::FileOpen((Root + "/f").c_str(), Platform::kAccessWrite, 0,
                                                                        Platform::kCreateAlways);
        Check(File != Platform::InvalidHandle() && Platform::WaitOne(File, 0) == Platform::kWaitFailed,
              "waiting on a file fails");
        Platform::CloseHandle(File);
        if (!Root.empty())
            std::system(("rm -rf '" + Root + "'").c_str());

        Platform::CriticalSection Lock;
        Platform::LockInit(&Lock);
        Platform::LockEnter(&Lock);
        Platform::LockEnter(&Lock);
        Platform::LockLeave(&Lock);
        Platform::LockLeave(&Lock);
        Platform::LockDelete(&Lock);

        FlULong const Slot = Platform::TlsSlotAlloc();
        int Value = 0;
        Check(Slot != Platform::kInvalidTlsSlot && Platform::TlsSlotSet(Slot, &Value) && Platform::TlsSlotGet(Slot) == &Value,
              "TLS slots");

        FlLong volatile Counter = 5;
        Check(Platform::AtomicIncrement(&Counter) == 6 && Platform::AtomicDecrement(&Counter) == 5, "atomic increment / decrement");
        Check(Platform::AtomicExchange(&Counter, 9) == 5 && Counter == 9, "atomic exchange returns the previous value");
        Check(Platform::AtomicCompareExchange(&Counter, 1, 9) == 9 && Counter == 1, "atomic compare-exchange replaces on a match");
        Check(Platform::AtomicCompareExchange(&Counter, 3, 9) == 1 && Counter == 1, "atomic compare-exchange keeps on a miss");
    }

    void TestPlatformTimeAndPaths()
    {
        FlULong const Start = Platform::TimerMs();
        Platform::SleepMs(20);
        Check(Platform::TimerMs() - Start >= 15, "TimerMs advances");
        int64_t Frequency = 0;
        int64_t Ticks = 0;
        Check(Platform::PerfFrequency(&Frequency) && Frequency > 0 && Platform::PerfCounter(&Ticks) && Ticks > 0,
              "performance counter");
        Platform::DateTime Now;
        Platform::LocalTime(&Now);
        Check(Now.wYear >= 2024 && Now.wMonth >= 1 && Now.wMonth <= 12, "LocalTime");

        char Full[512];
        char* Part = nullptr;
        FlULong const Length = Platform::FullPath("/a/b/../c/./d.ini", Full, sizeof(Full), &Part);
        Check(Length == std::strlen("/a/c/d.ini") && std::strcmp(Full, "/a/c/d.ini") == 0 && Part != nullptr &&
                  std::strcmp(Part, "d.ini") == 0,
              "FullPath folds . and ..");
        Check(Platform::FullPath("/x", Full, 2) == 3, "FullPath reports the size needed");
        char Here[512];
        Check(Platform::CurrentDirectory(Here, sizeof(Here)) > 0 && Here[0] == '/', "CurrentDirectory");
        char Executable[1024];
        Check(Platform::ModulePath(nullptr, Executable, sizeof(Executable)) > 0, "ModulePath of the executable");
        Platform::Module Self = Platform::ModuleFind(nullptr);
        Check(Self != nullptr && Platform::ModuleSymbol(Self, "malloc") != nullptr, "ModuleFind / ModuleSymbol");
    }

    // HandleValue names any object (not only arena blocks) by a 32-bit id on 64-bit targets.
    void TestHandleValue()
    {
        int Objects[4] = {};
        FlULong Ids[4] = {};
        for (int Index = 0; Index < 4; ++Index)
        {
            Ids[Index] = HandleValue::Of(&Objects[Index]);
            Check(Ids[Index] != 0 && Ids[Index] != 0xFFFFFFFFu, "an id is never 0 or -1");
            Check(HandleValue::Of(&Objects[Index]) == Ids[Index], "the same object gets the same id");
        }
        for (int Index = 0; Index < 4; ++Index)
            Check(HandleValue::ObjectOf(Ids[Index]) == &Objects[Index], "an id names its object");
        HandleValue::Forget(&Objects[0]);
    }
}

int main()
{
    static_assert(sizeof(FlULong) == 4 && sizeof(FlLong) == 4, "Win32's 32-bit long");
#if defined(FL_PTR64)
    TestLowArenaRoundTrip();
    TestLowArenaReuse();
    TestLowArenaLargeBlock();
#endif
    TestHandleValue();
    TestPlatformFiles();
    TestPlatformThreadsAndWaits();
    TestPlatformTimeAndPaths();
    std::printf(G_Failures == 0 ? "port_tests: all passed\n" : "port_tests: %d failed\n", G_Failures);
    return G_Failures;
}
