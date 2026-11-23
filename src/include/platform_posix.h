// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

#pragma once

// PORT(macOS, Linux): the POSIX backend of the platform layer (platform.h), included from there
// outside Windows. The same functions as the Win32 backend, with Win32's values, results and
// structure layouts (x64-client-plan.md, phase 6, M2):
//
// - Handles are small numbers, as Windows kernel handles are (multiples of 4 from 4 up), naming
//   objects in a table: files, searches, threads, events, mutexes. Retail keeps file handles in
//   32-bit fields, which Windows' handles survive on x64, and so do these. InvalidHandle() is
//   the all-ones value, as INVALID_HANDLE_VALUE; a value the table does not hold names nothing.
// - Paths take '\' as well as '/', and each component is matched without regard to case: retail
//   DATA\ spells names in mixed case that its ini files spell differently.
// - Waits (WaitOne, WaitMany) follow WaitForSingleObject / WaitForMultipleObjects: an auto-reset
//   event is consumed, a mutex is taken (recursively, by its owner), a thread is signalled when
//   it ends. They share one lock and condition variable, which keeps WaitMany simple and exact.
// - FindData keeps WIN32_FIND_DATAA's field names and 320-byte layout: IFileSystem hands it
//   across modules. DateTime keeps SYSTEMTIME's.
// - Handles are counted: CloseHandle releases one reference, a running thread keeps its own,
//   and a named event or mutex is one object per name within the process (CreateEventA /
//   CreateMutexA open the existing one). A mutex whose owning thread ends is abandoned: the
//   next wait takes it and returns kWaitAbandoned.
// - Not available here: ThreadTerminate (POSIX has no safe equivalent; it fails), objects
//   shared between processes.
// - Resource-only DLLs (ResourceModuleLoad) are Windows PE images (pe_resources.h), mapped
//   read-only. Their handles are the mapping's address, which no dlopen handle can be: the
//   module calls tell the two apart by the registry of mapped resource modules.

#include "fl_layout.h" // FlULong / FlLong (also forced into every source)
#include "pe_resources.h" // ResourceModuleLoad's images

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <vector>
#include <new>
#include <string>
#include <thread>

#include <dirent.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <pthread.h>
#include <strings.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <stdio.h> // renamex_np
#else
#include <link.h>        // dl_iterate_phdr (ModulePath)
#include <sys/syscall.h> // renameat2
#endif

// FILETIME's layout and tag (see Platform::FileTime).
struct _FILETIME
{
    FlULong dwLowDateTime;
    FlULong dwHighDateTime;
};

namespace Platform
{
    // ---------------------------------------------------------------------------------------
    // Handles
    // ---------------------------------------------------------------------------------------

    using Handle = void*;

    inline Handle InvalidHandle() { return reinterpret_cast<Handle>(static_cast<std::intptr_t>(-1)); }

    // A handle in a 32-bit field and back, as HandleToULong / LongToHandle: retail keeps file
    // handles in DWORDs. Handle values fit in 32 bits (Windows keeps kernel handles there on
    // x64, and so does the POSIX backend); the way back sign-extends, so 0xFFFFFFFF is
    // InvalidHandle() again.
    inline FlULong HandleToValue(Handle Object)
    {
        return (FlULong)((std::uintptr_t)(Object));
    }
    inline Handle HandleFromValue(FlULong Value)
    {
        return (Handle)((std::intptr_t)((FlLong)(Value)));
    }

    namespace Detail
    {
        // Every handle points at one of these. References counts the open handles (and, for a
        // thread, the running thread itself); Name is set for a named event or mutex.
        // Kind says what an object is (a header's RTTI is not reliably one per process across
        // modules here, so no dynamic_cast).
    enum : unsigned char
    {
        kKindNone = 0,
        kKindFile,
        kKindFind,
        kKindEvent,
        kKindMutex,
        kKindThread,
        kKindMapping, // posix/kernel32.h
    };
        struct FObject
        {
            std::atomic<int> References{1};
            std::string Name;
            unsigned char Kind = kKindNone;
            virtual ~FObject() = default;
        };

        // The handle table: slot N is handle (N + 1) * 4. Free slots are reused, as Windows
        // reuses handle values. Lock order: WaitLock, then this one.
        struct FHandles
        {
            std::mutex Lock;
            std::vector<FObject*> Slots;
            std::vector<std::size_t> Free;
        };

        struct FMutex;
        struct FResourceModule;

        // The layer's process-wide state. A header's statics are per module on macOS and Linux
        // (each .dylib / .so has its own), and handles, waits, thread ids and the last error
        // cross modules, so it is one FShared per process: FL_PLATFORM_DEFINE_SHARED in dacom,
        // which every game module links, found through FlPlatformShared. Per-thread values sit
        // under pthread keys of it (a thread_local is per module too).
        struct FShared
        {
            std::mutex WaitLock; // the one lock behind every wait
            std::condition_variable WaitSignal; // and its condition variable
            std::atomic<FlULong> NextThreadId{1}; // GetCurrentThreadId's values
            pthread_key_t ThreadIdKey{};
            pthread_key_t LastErrorKey{}; // posix/kernel32.h's GetLastError
            FHandles Handles;
            std::map<std::string, FObject*> Names; // named events and mutexes, under WaitLock
            std::set<FMutex*> Mutexes; // every mutex, under WaitLock
            std::mutex RegionsLock; // posix/kernel32.h's views and VirtualAlloc blocks
            std::map<const void*, std::size_t> Regions;
            std::mutex ResourceModulesLock; // ResourceModuleLoad's modules, by address
            std::map<std::uintptr_t, FResourceModule*> ResourceModules;
            std::chrono::steady_clock::time_point Start =
                std::chrono::steady_clock::now(); // timeGetTime's zero

            FShared()
            {
                pthread_key_create(&ThreadIdKey, nullptr);
                pthread_key_create(&LastErrorKey, &std::free);
            }
        };
        }
        }

        // The process's platform state (FL_PLATFORM_DEFINE_SHARED, in dacom).
        extern "C" Platform::Detail::FShared* FlPlatformShared();

        namespace Platform
        {
        namespace Detail
        {
        inline FShared& Shared()
        {
            return *FlPlatformShared();
        }
        inline std::mutex& WaitLock()
        {
            return Shared().WaitLock;
        }
        inline std::condition_variable& WaitSignal()
        {
            return Shared().WaitSignal;
        }

        // A thread's 32-bit id, as GetCurrentThreadId's: handed out once per thread, never 0,
        // the same in every module.
        inline FlULong ThisThreadId()
        {
            FShared& State = Shared();
            if (void* const Stored = pthread_getspecific(State.ThreadIdKey))
                return (FlULong)(std::uintptr_t)Stored;
            const FlULong Id = State.NextThreadId.fetch_add(1);
            pthread_setspecific(State.ThreadIdKey, (void*)(std::uintptr_t)Id);
            return Id;
        }

        // Something a wait can end on. Signalled and Acquire run under WaitLock.
        struct FWaitable : FObject
        {
            virtual bool Signalled() const = 0;
            // Takes the object for a waiter that it released (under WaitLock); true for an
            // abandoned mutex.
            virtual bool Acquire() { return false; }
        };

        inline FHandles& Handles()
        {
            return Shared().Handles;
        }

        // A new handle for Object (which it now holds a reference of), or nullptr when the table
        // cannot grow.
        inline Handle NewHandle(FObject* Object)
        {
            if (Object == nullptr)
                return nullptr;
            FHandles& Table = Handles();
            std::lock_guard<std::mutex> Lock(Table.Lock);
            try
            {
                std::size_t Slot;
                if (!Table.Free.empty())
                {
                    Slot = Table.Free.back();
                    Table.Free.pop_back();
                    Table.Slots[Slot] = Object;
                }
                else
                {
                    if (Table.Slots.size() >= 0x3FFFFFFEu)
                        return nullptr; // (Slot + 1) * 4 must stay below 0xFFFFFFFF
                    Slot = Table.Slots.size();
                    Table.Slots.push_back(Object);
                }
                return reinterpret_cast<Handle>(static_cast<std::uintptr_t>((Slot + 1) * 4));
            }
            catch (std::bad_alloc const&)
            {
                return nullptr;
            }
        }

        // The table slot a handle value names, or -1.
        inline std::ptrdiff_t SlotOf(Handle Value)
        {
            std::uintptr_t const Number = reinterpret_cast<std::uintptr_t>(Value);
            if (Number < 4 || Number % 4 != 0)
                return -1;
            return static_cast<std::ptrdiff_t>(Number / 4 - 1);
        }

        // The object a handle names, or nullptr (a closed or made-up value, nullptr,
        // InvalidHandle()). Not a reference: closing the handle on another thread meanwhile is
        // the caller's race, as on Windows.
        inline FObject* Lookup(Handle Value)
        {
            std::ptrdiff_t const Slot = SlotOf(Value);
            if (Slot < 0)
                return nullptr;
            FHandles& Table = Handles();
            std::lock_guard<std::mutex> Lock(Table.Lock);
            return static_cast<std::size_t>(Slot) < Table.Slots.size() ? Table.Slots[Slot]
                                                                       : nullptr;
        }

        inline bool Valid(Handle Value)
        {
            return Lookup(Value) != nullptr;
        }

        // The object behind a handle as a T (by its Kind), or nullptr when it names nothing or
        // something else.
        template <class T>
        T* As(Handle Value)
        {
            FObject* const Object = Lookup(Value);
            return Object != nullptr && Object->Kind == T::kKind ? static_cast<T*>(Object)
                                                                 : nullptr;
        }

        // The named events and mutexes of this process, under WaitLock.
        inline std::map<std::string, FObject*>& Names()
        {
            return Shared().Names;
        }

        // Drops one reference; the last one deletes the object (outside WaitLock).
        inline void Release(FObject* Object)
        {
            bool bLast = false;
            {
                std::lock_guard<std::mutex> Lock(WaitLock());
                bLast = --Object->References == 0;
                if (bLast && !Object->Name.empty())
                    Names().erase(Object->Name);
            }
            if (bLast)
                delete Object;
        }
        }

    // CloseHandle: frees the handle value and releases its reference.
    inline bool CloseHandle(Handle Value)
    {
        std::ptrdiff_t const Slot = Detail::SlotOf(Value);
        if (Slot < 0)
            return false;
        Detail::FObject* Object = nullptr;
        {
            Detail::FHandles& Table = Detail::Handles();
            std::lock_guard<std::mutex> Lock(Table.Lock);
            if (static_cast<std::size_t>(Slot) >= Table.Slots.size() ||
                Table.Slots[Slot] == nullptr)
                return false;
            Object = Table.Slots[Slot];
            Table.Slots[Slot] = nullptr;
            try
            {
                Table.Free.push_back(static_cast<std::size_t>(Slot));
            }
            catch (std::bad_alloc const&)
            {
                // The value is not reused; nothing else is lost.
            }
        }
        Detail::Release(Object);
        return true;
    }

    // ---------------------------------------------------------------------------------------
    // Paths
    // ---------------------------------------------------------------------------------------

    namespace Detail
    {
        // Path with '\' as '/' and each existing component spelled as it is on disk, matched
        // without regard to case. Components that do not exist (a file about to be created) stay
        // as written. Drive letters do not exist here and are dropped ("C:\x" is "/x").
        inline std::string ResolvePath(char const* Path)
        {
            std::string In = Path != nullptr ? Path : "";
            for (char& Character : In)
            {
                if (Character == '\\')
                    Character = '/';
            }
            if (In.size() >= 2 && In[1] == ':')
                In.erase(0, 2);
            std::string Out;
            std::size_t Position = 0;
            if (!In.empty() && In[0] == '/')
            {
                Out = "/";
                Position = 1;
            }
            bool bMissing = false;
            while (Position <= In.size())
            {
                std::size_t Slash = In.find('/', Position);
                if (Slash == std::string::npos)
                    Slash = In.size();
                std::string Component = In.substr(Position, Slash - Position);
                Position = Slash + 1;
                if (Component.empty())
                    continue;
                std::string Directory = Out.empty() ? "." : Out;
                if (!bMissing && Component != "." && Component != "..")
                {
                    struct stat Info;
                    std::string const Exact = (Out.empty() || Out.back() == '/' ? Out : Out + "/") + Component;
                    if (stat(Exact.c_str(), &Info) != 0)
                    {
                        bool bFound = false;
                        if (DIR* const Entries = opendir(Directory.c_str()))
                        {
                            while (dirent* const Entry = readdir(Entries))
                            {
                                if (strcasecmp(Entry->d_name, Component.c_str()) == 0)
                                {
                                    Component = Entry->d_name;
                                    bFound = true;
                                    break;
                                }
                            }
                            closedir(Entries);
                        }
                        bMissing = !bFound;
                    }
                }
                if (!Out.empty() && Out.back() != '/')
                    Out += '/';
                Out += Component;
            }
            return Out.empty() ? std::string(".") : Out;
        }

        // Windows FILETIME (100 ns since 1601) from a Unix time.
        inline void ToFileTime(std::time_t Seconds, FlULong* Low, FlULong* High)
        {
            std::uint64_t const Ticks = (static_cast<std::uint64_t>(Seconds) + 11644473600ull) * 10000000ull;
            *Low = static_cast<FlULong>(Ticks & 0xFFFFFFFFu);
            *High = static_cast<FlULong>(Ticks >> 32);
        }
    }

    // ---------------------------------------------------------------------------------------
    // Files
    // ---------------------------------------------------------------------------------------

    // Access, sharing and creation values, as GENERIC_*, FILE_SHARE_* and the creation
    // disposition. Sharing is not enforced on POSIX.
    constexpr FlULong kAccessRead = 0x80000000u;
    constexpr FlULong kAccessWrite = 0x40000000u;
    constexpr FlULong kShareRead = 0x1;
    constexpr FlULong kShareWrite = 0x2;
    constexpr FlULong kCreateNew = 1;
    constexpr FlULong kCreateAlways = 2;
    constexpr FlULong kOpenExisting = 3;
    constexpr FlULong kOpenAlways = 4;
    constexpr FlULong kTruncateExisting = 5;

    namespace Detail
    {
        struct FFile : FObject
        {
            static constexpr unsigned char kKind = kKindFile;
            FFile() { Kind = kKind; }
            int Descriptor = -1;
            ~FFile() override
            {
                if (Descriptor >= 0)
                    close(Descriptor);
            }
        };
    }

    // CreateFileA: InvalidHandle() on failure.
    inline Handle FileOpen(char const* Path, FlULong Access, FlULong Share, FlULong Creation,
                           FlULong FlagsAndAttributes = 0)
    {
        (void)Share;
        (void)FlagsAndAttributes;
        if (Creation == kTruncateExisting && (Access & kAccessWrite) == 0)
            return InvalidHandle(); // CreateFileA refuses to truncate without write access
        int Flags = (Access & kAccessWrite) != 0 ? ((Access & kAccessRead) != 0 ? O_RDWR : O_WRONLY) : O_RDONLY;
        switch (Creation)
        {
        case kCreateNew: Flags |= O_CREAT | O_EXCL; break;
        case kCreateAlways: Flags |= O_CREAT | O_TRUNC; break;
        case kOpenAlways: Flags |= O_CREAT; break;
        case kTruncateExisting: Flags |= O_TRUNC; break;
        default: break; // kOpenExisting
        }
        std::string const Resolved = Detail::ResolvePath(Path);
        struct stat Info;
        if (stat(Resolved.c_str(), &Info) == 0 && S_ISDIR(Info.st_mode))
            return InvalidHandle(); // CreateFileA opens no directory without FILE_FLAG_BACKUP_SEMANTICS
        int const Descriptor = open(Resolved.c_str(), Flags | O_CLOEXEC, 0644);
        if (Descriptor < 0)
            return InvalidHandle();
        Detail::FFile* const File = new (std::nothrow) Detail::FFile();
        if (File == nullptr)
        {
            close(Descriptor);
            return InvalidHandle();
        }
        File->Descriptor = Descriptor;
        Handle const Value = Detail::NewHandle(File);
        if (Value == nullptr)
        {
            delete File; // closes Descriptor
            return InvalidHandle();
        }
        return Value;
    }

    // ReadFile, synchronous: reads until Size bytes or the end of the file.
    inline bool FileRead(Handle File, void* Buffer, FlULong Size, FlULong* BytesRead)
    {
        if (BytesRead != nullptr)
            *BytesRead = 0;
        Detail::FFile* const Object = Detail::As<Detail::FFile>(File);
        if (Object == nullptr)
            return false;
        int const Descriptor = Object->Descriptor;
        FlULong Done = 0;
        while (Done < Size)
        {
            ssize_t const Count = read(Descriptor, static_cast<char*>(Buffer) + Done, Size - Done);
            if (Count < 0 && errno == EINTR)
                continue;
            if (Count < 0)
                return false;
            if (Count == 0)
                break;
            Done += static_cast<FlULong>(Count);
        }
        if (BytesRead != nullptr)
            *BytesRead = Done;
        return true;
    }

    // WriteFile, synchronous.
    inline bool FileWrite(Handle File, void const* Buffer, FlULong Size, FlULong* BytesWritten)
    {
        if (BytesWritten != nullptr)
            *BytesWritten = 0;
        Detail::FFile* const Object = Detail::As<Detail::FFile>(File);
        if (Object == nullptr)
            return false;
        int const Descriptor = Object->Descriptor;
        FlULong Done = 0;
        while (Done < Size)
        {
            ssize_t const Count = write(Descriptor, static_cast<char const*>(Buffer) + Done, Size - Done);
            if (Count < 0 && errno == EINTR)
                continue;
            if (Count <= 0)
                return false;
            Done += static_cast<FlULong>(Count);
        }
        if (BytesWritten != nullptr)
            *BytesWritten = Done;
        return true;
    }

    // GetFileSize: the low 32 bits; 0xffffffff (INVALID_FILE_SIZE) on failure.
    inline FlULong FileGetSize(Handle File)
    {
        struct stat Info;
        Detail::FFile* const Object = Detail::As<Detail::FFile>(File);
        if (Object == nullptr || fstat(Object->Descriptor, &Info) != 0)
            return 0xFFFFFFFFu;
        return static_cast<FlULong>(static_cast<std::uint64_t>(Info.st_size) & 0xFFFFFFFFu);
    }

    // File attribute values (FILE_ATTRIBUTE_*).
    constexpr FlULong kInvalidAttributes = 0xFFFFFFFFu;
    constexpr FlULong kAttributeReadOnly = 0x1;
    constexpr FlULong kAttributeDirectory = 0x10;
    constexpr FlULong kAttributeNormal = 0x80;

    // GetFileAttributesA: kInvalidAttributes when the path does not exist.
    inline FlULong PathAttributes(char const* Path)
    {
        struct stat Info;
        if (stat(Detail::ResolvePath(Path).c_str(), &Info) != 0)
            return kInvalidAttributes;
        if (S_ISDIR(Info.st_mode))
            return kAttributeDirectory;
        return (Info.st_mode & S_IWUSR) != 0 ? kAttributeNormal : kAttributeReadOnly;
    }

    // CopyFileA. FailIfExists false overwrites the destination. Copying a file onto itself (the
    // same file under another spelling, or a hard link) fails without touching it.
    inline bool FileCopy(char const* From, char const* To, bool FailIfExists)
    {
        int const Source = open(Detail::ResolvePath(From).c_str(), O_RDONLY | O_CLOEXEC);
        if (Source < 0)
            return false;
        int const Target = open(Detail::ResolvePath(To).c_str(),
                                O_WRONLY | O_CREAT | O_CLOEXEC | (FailIfExists ? O_EXCL : 0), 0644);
        struct stat SourceInfo;
        struct stat TargetInfo;
        bool bOk = Target >= 0 && fstat(Source, &SourceInfo) == 0 && fstat(Target, &TargetInfo) == 0 &&
                   !(SourceInfo.st_dev == TargetInfo.st_dev && SourceInfo.st_ino == TargetInfo.st_ino) &&
                   ftruncate(Target, 0) == 0;
        char Buffer[64 * 1024];
        while (bOk)
        {
            ssize_t const Read = read(Source, Buffer, sizeof(Buffer));
            if (Read < 0 && errno == EINTR)
                continue;
            if (Read <= 0)
            {
                bOk = Read == 0;
                break;
            }
            ssize_t Written = 0;
            while (bOk && Written < Read)
            {
                ssize_t const Count = write(Target, Buffer + Written, static_cast<std::size_t>(Read - Written));
                if (Count < 0 && errno == EINTR)
                    continue;
                bOk = Count > 0;
                Written += Count > 0 ? Count : 0;
            }
        }
        close(Source);
        if (Target >= 0)
            close(Target);
        return bOk;
    }

    // MoveFileA: fails when the destination exists, as on Windows, checked by the rename itself
    // (no window for another process to create it in between).
    inline bool FileMove(char const* From, char const* To)
    {
        std::string const Source = Detail::ResolvePath(From);
        std::string const Target = Detail::ResolvePath(To);
#if defined(__APPLE__)
        return renamex_np(Source.c_str(), Target.c_str(), RENAME_EXCL) == 0;
#else
        constexpr unsigned kRenameNoReplace = 1; // RENAME_NOREPLACE
        if (syscall(SYS_renameat2, AT_FDCWD, Source.c_str(), AT_FDCWD, Target.c_str(), kRenameNoReplace) == 0)
            return true;
        if (errno != ENOSYS && errno != EINVAL)
            return false;
        // No renameat2 here (or the file system lacks it): link() refuses an existing target too.
        if (link(Source.c_str(), Target.c_str()) != 0)
            return false;
        return unlink(Source.c_str()) == 0;
#endif
    }

    // WIN32_FIND_DATAA with its field names and layout (320 bytes).
    // FILETIME: the global _FILETIME, the tag Windows' headers give it (retail headers forward-
    // declare `struct _FILETIME`).
    using FileTime = ::_FILETIME;
    struct FindData
    {
        FlULong dwFileAttributes;
        FileTime ftCreationTime;
        FileTime ftLastAccessTime;
        FileTime ftLastWriteTime;
        FlULong nFileSizeHigh;
        FlULong nFileSizeLow;
        FlULong dwReserved0;
        FlULong dwReserved1;
        char cFileName[260];
        char cAlternateFileName[14];
    };
    static_assert(sizeof(FindData) == 320, "WIN32_FIND_DATAA's layout");

    namespace Detail
    {
        struct FFind : FObject
        {
            static constexpr unsigned char kKind = kKindFind;
            FFind() { Kind = kKind; }
            DIR* Entries = nullptr;
            std::string Directory;
            std::string Pattern;
            ~FFind() override
            {
                if (Entries != nullptr)
                    closedir(Entries);
            }
        };

        // FindFirstFileA's wildcards, without regard to case: '*' any run, '?' one character.
        // "*.*" also matches names without a dot, as on Windows. Linear backtracking to the last
        // '*', so no pattern makes it slow.
        inline bool Matches(char const* Pattern, char const* Name)
        {
            if (std::strcmp(Pattern, "*.*") == 0)
                return true;
            char const* StarPattern = nullptr;
            char const* StarName = nullptr;
            auto Same = [](char Left, char Right) {
                return std::tolower(static_cast<unsigned char>(Left)) == std::tolower(static_cast<unsigned char>(Right));
            };
            while (*Name != 0)
            {
                if (*Pattern == '*')
                {
                    StarPattern = ++Pattern;
                    StarName = Name;
                }
                else if (*Pattern != 0 && (*Pattern == '?' || Same(*Pattern, *Name)))
                {
                    ++Pattern;
                    ++Name;
                }
                else if (StarPattern != nullptr)
                {
                    Pattern = StarPattern;
                    Name = ++StarName;
                }
                else
                {
                    return false;
                }
            }
            while (*Pattern == '*')
                ++Pattern;
            return *Pattern == 0;
        }

        // The next entry of Search that matches, filled into Data.
        inline bool NextMatch(FFind* Search, FindData* Data)
        {
            while (dirent* const Entry = readdir(Search->Entries))
            {
                if (!Matches(Search->Pattern.c_str(), Entry->d_name))
                    continue;
                std::string const Full = Search->Directory + "/" + Entry->d_name;
                struct stat Info;
                if (stat(Full.c_str(), &Info) != 0)
                    continue;
                std::memset(Data, 0, sizeof(*Data));
                Data->dwFileAttributes = S_ISDIR(Info.st_mode) ? kAttributeDirectory : kAttributeNormal;
                std::uint64_t const Size = static_cast<std::uint64_t>(Info.st_size);
                Data->nFileSizeHigh = static_cast<FlULong>(Size >> 32);
                Data->nFileSizeLow = static_cast<FlULong>(Size & 0xFFFFFFFFu);
                ToFileTime(Info.st_mtime, &Data->ftLastWriteTime.dwLowDateTime, &Data->ftLastWriteTime.dwHighDateTime);
                ToFileTime(Info.st_atime, &Data->ftLastAccessTime.dwLowDateTime, &Data->ftLastAccessTime.dwHighDateTime);
                ToFileTime(Info.st_ctime, &Data->ftCreationTime.dwLowDateTime, &Data->ftCreationTime.dwHighDateTime);
                std::snprintf(Data->cFileName, sizeof(Data->cFileName), "%s", Entry->d_name);
                return true;
            }
            return false;
        }
    }

    // FindFirstFileA: InvalidHandle() when nothing matches.
    inline Handle FindFirst(char const* Pattern, FindData* Data)
    {
        std::string Text = Pattern != nullptr ? Pattern : "";
        for (char& Character : Text)
        {
            if (Character == '\\')
                Character = '/';
        }
        std::size_t const Slash = Text.find_last_of('/');
        Detail::FFind* const Search = new (std::nothrow) Detail::FFind();
        if (Search == nullptr)
            return InvalidHandle();
        Search->Directory = Detail::ResolvePath(Slash == std::string::npos ? "." : Text.substr(0, Slash).c_str());
        Search->Pattern = Slash == std::string::npos ? Text : Text.substr(Slash + 1);
        Search->Entries = opendir(Search->Directory.c_str());
        Handle Value = nullptr;
        if (Search->Entries == nullptr || !Detail::NextMatch(Search, Data) ||
            (Value = Detail::NewHandle(Search)) == nullptr)
        {
            delete Search;
            return InvalidHandle();
        }
        return Value;
    }

    // FindNextFileA.
    inline bool FindNext(Handle Search, FindData* Data)
    {
        Detail::FFind* const Object = Detail::As<Detail::FFind>(Search);
        return Object != nullptr && Detail::NextMatch(Object, Data);
    }

    // FindClose.
    inline bool FindClose(Handle Search) { return CloseHandle(Search); }

    // ---------------------------------------------------------------------------------------
    // Synchronisation
    // ---------------------------------------------------------------------------------------

    constexpr FlULong kWaitForever = 0xFFFFFFFFu;
    constexpr FlULong kWaitSignaled = 0;
    constexpr FlULong kWaitAbandoned = 0x80;
    constexpr FlULong kWaitTimeout = 0x102;
    constexpr FlULong kWaitFailed = 0xFFFFFFFFu;

    namespace Detail
    {
        struct FEvent : FWaitable
        {
            static constexpr unsigned char kKind = kKindEvent;
            FEvent() { Kind = kKind; }
            bool bManualReset = false;
            bool bSet = false;
            bool Signalled() const override { return bSet; }
            bool Acquire() override
            {
                if (!bManualReset)
                    bSet = false;
                return false;
            }
        };

        struct FMutex;

        // Every mutex of the process, under WaitLock, so that a thread's end can abandon the
        // ones it still owns.
        inline std::set<FMutex*>& Mutexes()
        {
            return Shared().Mutexes;
        }

        struct FMutex : FWaitable
        {
            static constexpr unsigned char kKind = kKindMutex;
            FlULong Owner = 0;
            FlULong Count = 0;
            bool bAbandoned = false; // its owner ended without releasing it; the next wait says so
            FMutex();
            ~FMutex() override;
            bool Signalled() const override { return Count == 0 || Owner == ThisThreadId(); }
            bool Acquire() override;
        };

        // Abandons the mutexes the current thread still owns when it ends (WAIT_ABANDONED for the
        // next waiter). Constructed by a thread's first mutex acquisition.
        struct FThreadExit
        {
            ~FThreadExit()
            {
                FlULong const Self = ThisThreadId();
                bool bAny = false;
                {
                    std::lock_guard<std::mutex> Lock(WaitLock());
                    for (FMutex* Mutex : Mutexes())
                    {
                        if (Mutex->Count != 0 && Mutex->Owner == Self)
                        {
                            Mutex->Owner = 0;
                            Mutex->Count = 0;
                            Mutex->bAbandoned = true;
                            bAny = true;
                        }
                    }
                }
                if (bAny)
                    WaitSignal().notify_all();
            }
        };

        // Callers hold WaitLock.
        inline FMutex::FMutex()
        {
            Kind = kKind;
            Mutexes().insert(this);
        }
        inline FMutex::~FMutex()
        {
            std::lock_guard<std::mutex> Lock(WaitLock());
            Mutexes().erase(this);
        }
        inline bool FMutex::Acquire()
        {
            thread_local FThreadExit Exit;
            (void)Exit;
            Owner = ThisThreadId();
            ++Count;
            bool const bWasAbandoned = bAbandoned;
            bAbandoned = false;
            return bWasAbandoned;
        }

        // The waitable object behind a handle, or nullptr (a file, a search, a closed value).
        inline FWaitable* Waitable(Handle Object)
        {
            FObject* const Found = Lookup(Object);
            if (Found == nullptr)
                return nullptr;
            const unsigned char Kind = Found->Kind;
            return Kind == kKindEvent || Kind == kKindMutex || Kind == kKindThread
                       ? static_cast<FWaitable*>(Found)
                       : nullptr;
        }

        // Waits for one object (WaitAll false: any, true: all), as WaitForMultipleObjects. An
        // abandoned mutex returns kWaitAbandoned (+ its index when WaitAll is false).
        inline FlULong Wait(FlULong Count, Handle const* Objects, bool WaitAll, FlULong TimeoutMs)
        {
            if (Count == 0 || Objects == nullptr)
                return kWaitFailed;
            std::vector<FWaitable*> Items(Count);
            for (FlULong Index = 0; Index < Count; ++Index)
            {
                Items[Index] = Waitable(Objects[Index]);
                if (Items[Index] == nullptr)
                    return kWaitFailed;
            }
            auto Ready = [&](FlULong* Which) {
                for (FlULong Index = 0; Index < Count; ++Index)
                {
                    bool const bSignalled = Items[Index]->Signalled();
                    if (!WaitAll && bSignalled)
                    {
                        *Which = Index;
                        return true;
                    }
                    if (WaitAll && !bSignalled)
                        return false;
                }
                *Which = 0;
                return WaitAll;
            };
            std::unique_lock<std::mutex> Lock(WaitLock());
            FlULong Which = 0;
            auto const Deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(TimeoutMs);
            while (!Ready(&Which))
            {
                if (TimeoutMs == kWaitForever)
                    WaitSignal().wait(Lock);
                else if (WaitSignal().wait_until(Lock, Deadline) == std::cv_status::timeout && !Ready(&Which))
                    return kWaitTimeout;
            }
            if (WaitAll)
            {
                bool bAbandoned = false;
                for (FlULong Index = 0; Index < Count; ++Index)
                    bAbandoned = Items[Index]->Acquire() || bAbandoned;
                return bAbandoned ? kWaitAbandoned : kWaitSignaled;
            }
            return (Items[Which]->Acquire() ? kWaitAbandoned : kWaitSignaled) + Which;
        }

        // A named event or mutex: the existing one of that name (one more reference), or a new one
        // made by Make and registered. nullptr when the name is taken by another kind of object.
        template <class T, class FMake>
        Handle OpenOrCreate(char const* Name, FMake Make)
        {
            T* Object = nullptr;
            {
                std::lock_guard<std::mutex> Lock(WaitLock());
                if (Name == nullptr || *Name == 0)
                {
                    Object = Make();
                }
                else
                {
                    auto const Found = Names().find(Name);
                    if (Found != Names().end())
                    {
                        if (Found->second->Kind != T::kKind)
                            return nullptr;
                        Object = static_cast<T*>(Found->second);
                        ++Object->References;
                    }
                    else if ((Object = Make()) != nullptr)
                    {
                        Object->Name = Name;
                        Names().emplace(Name, Object);
                    }
                }
            }
            if (Object == nullptr)
                return nullptr;
            // A handle of its own, also for an object that already existed (as on Windows).
            Handle const Value = NewHandle(Object);
            if (Value == nullptr)
                Release(Object);
            return Value;
        }
    }

    // CreateEventA. A named event is shared within this process (an existing one is opened, its
    // settings unchanged), not between processes. nullptr on failure.
    inline Handle EventCreate(bool ManualReset, bool InitiallySet, char const* Name = nullptr)
    {
        return Detail::OpenOrCreate<Detail::FEvent>(Name, [&]() -> Detail::FEvent* {
            Detail::FEvent* const Event = new (std::nothrow) Detail::FEvent();
            if (Event != nullptr)
            {
                Event->bManualReset = ManualReset;
                Event->bSet = InitiallySet;
            }
            return Event;
        });
    }

    // SetEvent / ResetEvent.
    inline bool EventSet(Handle Event)
    {
        Detail::FEvent* const Object = Detail::As<Detail::FEvent>(Event);
        if (Object == nullptr)
            return false;
        {
            std::lock_guard<std::mutex> Lock(Detail::WaitLock());
            Object->bSet = true;
        }
        Detail::WaitSignal().notify_all();
        return true;
    }
    inline bool EventReset(Handle Event)
    {
        Detail::FEvent* const Object = Detail::As<Detail::FEvent>(Event);
        if (Object == nullptr)
            return false;
        std::lock_guard<std::mutex> Lock(Detail::WaitLock());
        Object->bSet = false;
        return true;
    }

    // CreateMutexA, not initially owned. A named mutex is shared within this process, as a named
    // event. nullptr on failure.
    inline Handle MutexCreate(char const* Name = nullptr)
    {
        return Detail::OpenOrCreate<Detail::FMutex>(Name, [] { return new (std::nothrow) Detail::FMutex(); });
    }

    // ReleaseMutex: fails for a thread that does not own it.
    inline bool MutexRelease(Handle Mutex)
    {
        Detail::FMutex* const Object = Detail::As<Detail::FMutex>(Mutex);
        if (Object == nullptr)
            return false;
        {
            std::lock_guard<std::mutex> Lock(Detail::WaitLock());
            if (Object->Count == 0 || Object->Owner != Detail::ThisThreadId())
                return false;
            if (--Object->Count == 0)
                Object->Owner = 0;
        }
        Detail::WaitSignal().notify_all();
        return true;
    }

    // WaitForSingleObject.
    inline FlULong WaitOne(Handle Object, FlULong TimeoutMs) { return Detail::Wait(1, &Object, false, TimeoutMs); }

    // WaitForMultipleObjects. WaitAll false returns kWaitSignaled + the index of the object that
    // ended the wait.
    inline FlULong WaitMany(FlULong Count, Handle const* Objects, bool WaitAll, FlULong TimeoutMs)
    {
        return Detail::Wait(Count, Objects, WaitAll, TimeoutMs);
    }

    // An in-process lock, recursive for its owner, as CRITICAL_SECTION (its size only matters
    // on Win32).
    struct CriticalSection
    {
        alignas(std::recursive_mutex) unsigned char Storage[sizeof(std::recursive_mutex)];
    };

    inline void LockInit(CriticalSection* Lock) { new (Lock->Storage) std::recursive_mutex(); }
    inline void LockEnter(CriticalSection* Lock) { reinterpret_cast<std::recursive_mutex*>(Lock->Storage)->lock(); }
    inline void LockLeave(CriticalSection* Lock) { reinterpret_cast<std::recursive_mutex*>(Lock->Storage)->unlock(); }
    inline void LockDelete(CriticalSection* Lock)
    {
        reinterpret_cast<std::recursive_mutex*>(Lock->Storage)->~recursive_mutex();
    }

    // Interlocked*: full-barrier atomics on a 32-bit value. Increment and Decrement return the
    // new value, Exchange and CompareExchange the previous one.
    inline FlLong AtomicIncrement(FlLong volatile* Value) { return __atomic_add_fetch(Value, 1, __ATOMIC_SEQ_CST); }
    inline FlLong AtomicDecrement(FlLong volatile* Value) { return __atomic_sub_fetch(Value, 1, __ATOMIC_SEQ_CST); }
    inline FlLong AtomicExchange(FlLong volatile* Value, FlLong NewValue)
    {
        return __atomic_exchange_n(Value, NewValue, __ATOMIC_SEQ_CST);
    }
    inline FlLong AtomicCompareExchange(FlLong volatile* Value, FlLong NewValue, FlLong Expected)
    {
        __atomic_compare_exchange_n(Value, &Expected, NewValue, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
        return Expected; // the previous value, whether or not it was replaced
    }

    // ---------------------------------------------------------------------------------------
    // Threads
    // ---------------------------------------------------------------------------------------

    // A thread entry point (LPTHREAD_START_ROUTINE).
    using ThreadProc = FlULong (*)(void* Parameter);

    // Thread priorities (THREAD_PRIORITY_*). Accepted and not applied: SCHED_OTHER has no
    // per-thread priorities without privileges.
    constexpr int kPriorityNormal = 0;
    constexpr int kPriorityAboveNormal = 1;
    constexpr int kPriorityHighest = 2;

    namespace Detail
    {
        struct FThread : FWaitable
        {
            static constexpr unsigned char kKind = kKindThread;
            FThread() { Kind = kKind; }
            ThreadProc Proc = nullptr;
            void* Parameter = nullptr;
            FlULong Suspended = 0;
            FlULong Id = 0;
            FlULong ExitCode = 0;
            bool bDone = false;
            bool Signalled() const override { return bDone; }
        };

        // The thread detaches itself (its end is seen through bDone, never joined) and holds
        // its own reference to the FThread until it is done, so CloseHandle may come first.
        inline void* RunThread(void* Argument)
        {
            FThread* const Thread = static_cast<FThread*>(Argument);
            pthread_detach(pthread_self());
            {
                std::unique_lock<std::mutex> Lock(WaitLock());
                Thread->Id = ThisThreadId();
                WaitSignal().notify_all();
                WaitSignal().wait(Lock, [Thread] { return Thread->Suspended == 0; });
            }
            FlULong const Result = Thread->Proc(Thread->Parameter);
            {
                std::lock_guard<std::mutex> Lock(WaitLock());
                Thread->ExitCode = Result;
                Thread->bDone = true;
            }
            WaitSignal().notify_all();
            Release(Thread);
            return nullptr;
        }
    }

    // CreateThread. StackSize 0 takes the default. Suspended starts it held until ThreadResume.
    // ThreadId may be nullptr. nullptr on failure. Closing the handle does not stop the thread.
    inline Handle ThreadCreate(ThreadProc Proc, void* Parameter, FlULong StackSize = 0, bool Suspended = false,
                               FlULong* ThreadId = nullptr)
    {
        Detail::FThread* const Thread = new (std::nothrow) Detail::FThread();
        if (Thread == nullptr)
            return nullptr;
        Thread->Proc = Proc;
        Thread->Parameter = Parameter;
        Thread->Suspended = Suspended ? 1 : 0;
        Thread->References = 2; // the handle's and the running thread's
        pthread_t Native;
        pthread_attr_t Attributes;
        pthread_attr_init(&Attributes);
        if (StackSize != 0)
            pthread_attr_setstacksize(&Attributes, StackSize < PTHREAD_STACK_MIN ? PTHREAD_STACK_MIN : StackSize);
        int const Result = pthread_create(&Native, &Attributes, &Detail::RunThread, Thread);
        pthread_attr_destroy(&Attributes);
        if (Result != 0)
        {
            delete Thread;
            return nullptr;
        }
        FlULong Id = 0;
        {
            std::unique_lock<std::mutex> Lock(Detail::WaitLock());
            Detail::WaitSignal().wait(Lock, [Thread] { return Thread->Id != 0; });
            Id = Thread->Id;
        }
        Handle const Value = Detail::NewHandle(Thread);
        if (Value == nullptr)
        {
            // No handle: the thread runs on, unseen, as one whose handle was closed.
            Detail::Release(Thread);
            return nullptr;
        }
        if (ThreadId != nullptr)
            *ThreadId = Id;
        return Value;
    }

    // ResumeThread: the previous suspend count, or 0xffffffff on failure.
    inline FlULong ThreadResume(Handle Thread)
    {
        Detail::FThread* const Object = Detail::As<Detail::FThread>(Thread);
        if (Object == nullptr)
            return 0xFFFFFFFFu;
        FlULong Previous = 0;
        {
            std::lock_guard<std::mutex> Lock(Detail::WaitLock());
            Previous = Object->Suspended;
            if (Object->Suspended > 0)
                --Object->Suspended;
        }
        Detail::WaitSignal().notify_all();
        return Previous;
    }

    // SetThreadPriority: accepted, not applied (see kPriorityNormal).
    inline bool ThreadSetPriority(Handle Thread, int Priority)
    {
        (void)Priority;
        return Detail::As<Detail::FThread>(Thread) != nullptr;
    }

    // GetExitCodeThread: the thread's result, or kStillActive while it runs. False for a handle
    // that is not a thread.
    constexpr FlULong kStillActive = 0x103;
    inline bool ThreadExitCode(Handle Thread, FlULong* ExitCode)
    {
        Detail::FThread* const Object = Detail::As<Detail::FThread>(Thread);
        if (Object == nullptr || ExitCode == nullptr)
            return false;
        std::lock_guard<std::mutex> Lock(Detail::WaitLock());
        *ExitCode = Object->bDone ? Object->ExitCode : kStillActive;
        return true;
    }

    // TerminateThread: POSIX has no safe equivalent, so it fails.
    inline bool ThreadTerminate(Handle Thread, FlULong ExitCode)
    {
        (void)Thread;
        (void)ExitCode;
        return false;
    }

    // GetCurrentThreadId.
    inline FlULong CurrentThreadId() { return Detail::ThisThreadId(); }

    // Sleep. 0 gives up the rest of the time slice.
    inline void SleepMs(FlULong Milliseconds)
    {
        if (Milliseconds == 0)
            std::this_thread::yield();
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(Milliseconds));
    }

    // Thread-local slots. TlsSlotAlloc returns kInvalidTlsSlot (TLS_OUT_OF_INDEXES) on failure.
    constexpr FlULong kInvalidTlsSlot = 0xFFFFFFFFu;
    inline FlULong TlsSlotAlloc()
    {
        pthread_key_t Key;
        if (pthread_key_create(&Key, nullptr) != 0 || static_cast<std::uint64_t>(Key) >= kInvalidTlsSlot)
            return kInvalidTlsSlot;
        return static_cast<FlULong>(Key);
    }
    inline void* TlsSlotGet(FlULong Slot) { return pthread_getspecific(static_cast<pthread_key_t>(Slot)); }
    inline bool TlsSlotSet(FlULong Slot, void* Value)
    {
        return pthread_setspecific(static_cast<pthread_key_t>(Slot), Value) == 0;
    }

    // ---------------------------------------------------------------------------------------
    // Time
    // ---------------------------------------------------------------------------------------

    namespace Detail
    {
        inline std::uint64_t MonotonicNs()
        {
            timespec Now;
            clock_gettime(CLOCK_MONOTONIC, &Now);
            return static_cast<std::uint64_t>(Now.tv_sec) * 1000000000ull + static_cast<std::uint64_t>(Now.tv_nsec);
        }
    }

    // timeGetTime and GetTickCount: milliseconds from a monotonic clock, wrapping every 49.7 days.
    inline FlULong TimerMs() { return static_cast<FlULong>(Detail::MonotonicNs() / 1000000ull); }
    inline FlULong TickCountMs() { return static_cast<FlULong>(Detail::MonotonicNs() / 1000000ull); }

    // QueryPerformanceCounter / QueryPerformanceFrequency: nanoseconds.
    inline bool PerfCounter(int64_t* Ticks)
    {
        *Ticks = static_cast<int64_t>(Detail::MonotonicNs());
        return true;
    }
    inline bool PerfFrequency(int64_t* TicksPerSecond)
    {
        *TicksPerSecond = 1000000000;
        return true;
    }

    // SYSTEMTIME with its field names: eight 16-bit words.
    struct DateTime
    {
        std::uint16_t wYear;
        std::uint16_t wMonth;
        std::uint16_t wDayOfWeek;
        std::uint16_t wDay;
        std::uint16_t wHour;
        std::uint16_t wMinute;
        std::uint16_t wSecond;
        std::uint16_t wMilliseconds;
    };
    static_assert(sizeof(DateTime) == 16, "SYSTEMTIME's layout");

    // GetLocalTime.
    inline void LocalTime(DateTime* Now)
    {
        timespec Clock;
        clock_gettime(CLOCK_REALTIME, &Clock);
        std::tm Fields{};
        localtime_r(&Clock.tv_sec, &Fields);
        Now->wYear = static_cast<std::uint16_t>(Fields.tm_year + 1900);
        Now->wMonth = static_cast<std::uint16_t>(Fields.tm_mon + 1);
        Now->wDayOfWeek = static_cast<std::uint16_t>(Fields.tm_wday);
        Now->wDay = static_cast<std::uint16_t>(Fields.tm_mday);
        Now->wHour = static_cast<std::uint16_t>(Fields.tm_hour);
        Now->wMinute = static_cast<std::uint16_t>(Fields.tm_min);
        Now->wSecond = static_cast<std::uint16_t>(Fields.tm_sec);
        Now->wMilliseconds = static_cast<std::uint16_t>(Clock.tv_nsec / 1000000);
    }

    // ---------------------------------------------------------------------------------------
    // Modules
    // ---------------------------------------------------------------------------------------

    using Module = void*;
    using Symbol = void*;

    namespace Detail
    {
    // A resource-only DLL mapped by ResourceModuleLoad: its file bytes, read-only. Its
    // handle is Base. The text of a resource is converted to wchar_t on first use
    // (ResourceWideText) and kept until the module is freed, as long as the mapped UTF-16
    // it stands for.
    struct FResourceModule
    {
        unsigned char const* Base = nullptr;
        std::size_t Size = 0;
        std::string Path;
        PeResources::FImage Image;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> Resources; // (offset, size)
        bool bIndexed = false;
        std::map<std::pair<std::uint32_t, std::uint32_t>, std::unique_ptr<wchar_t[]>>
            Wide; // by (offset, size)
        ~FResourceModule()
        {
            if (Base != nullptr)
                munmap(const_cast<unsigned char*>(Base), Size);
        }
    };

    // The resource module Library is, or nullptr. Under ResourceModulesLock.
    inline FResourceModule* ResourceModuleOf(void const* Library)
    {
        auto& Modules = Shared().ResourceModules;
        auto const Found = Modules.find(reinterpret_cast<std::uintptr_t>(Library));
        return Found != Modules.end() ? Found->second : nullptr;
    }

    // The resource module whose mapping holds Address, or nullptr. Under
    // ResourceModulesLock.
    inline FResourceModule* ResourceModuleHolding(void const* Address)
    {
        auto& Modules = Shared().ResourceModules;
        std::uintptr_t const Value = reinterpret_cast<std::uintptr_t>(Address);
        auto Found = Modules.upper_bound(Value);
        if (Found == Modules.begin())
            return nullptr;
        --Found;
        return Value - Found->first < Found->second->Size ? Found->second : nullptr;
    }
    }

    // LoadLibraryA: dlopen. A name ending in .dll is also tried as .dylib (macOS) or .so.
    // nullptr on failure.
    //
    // A loaded module stays mapped until the process ends (RTLD_NODELETE), even after
    // ModuleFree drops the last reference. Objects it made outlive it: handles in the shared
    // table (their vtables are the module's) and the factories and callbacks it registered with
    // dacom. FreeLibrary returns as on Windows, but the module's static destructors and
    // DLL_PROCESS_DETACH run at process exit.
    inline constexpr int kModuleLoadFlags = RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE;
    inline Module ModuleLoad(char const* Path)
    {
        std::string Resolved = Detail::ResolvePath(Path);
        if (Module const Loaded = dlopen(Resolved.c_str(), kModuleLoadFlags))
            return Loaded;
        std::size_t const Length = std::strlen(Path);
        if (Length > 4 && strcasecmp(Path + Length - 4, ".dll") == 0)
        {
#if defined(__APPLE__)
            std::string Native(Path);
            Native.replace(Native.size() - 4, 4, ".dylib");
#else
            std::string Native(Path);
            Native.replace(Native.size() - 4, 4, ".so");
#endif
            // Resolved again: the .dylib/.so may be spelled with another case than the .dll.
            return dlopen(Detail::ResolvePath(Native.c_str()).c_str(), kModuleLoadFlags);
        }
        return nullptr;
    }

    // FreeLibrary: drops a reference; the module stays mapped (see ModuleLoad). A resource
    // module (ResourceModuleLoad) is unmapped.
    inline bool ModuleFree(Module Library)
    {
        if (Library == nullptr)
            return false;
        Detail::FShared& State = Detail::Shared();
        std::unique_ptr<Detail::FResourceModule> Resources;
        {
            std::lock_guard<std::mutex> Lock(State.ResourceModulesLock);
            auto const Found =
                State.ResourceModules.find(reinterpret_cast<std::uintptr_t>(Library));
            if (Found != State.ResourceModules.end())
            {
                Resources.reset(Found->second);
                State.ResourceModules.erase(Found);
            }
        }
        if (Resources != nullptr)
            return true;
        return dlclose(Library) == 0;
    }

    // GetModuleHandleA: an already loaded module, nullptr naming the executable. Borrowed, as
    // on Windows: the caller does not free it (the reference dlopen took is dropped here, and the
    // module stays loaded by whoever loaded it).
    inline Module ModuleFind(char const* Name)
    {
        Module const Found = Name == nullptr ? dlopen(nullptr, RTLD_NOW) : dlopen(Name, RTLD_NOW | RTLD_NOLOAD);
        if (Found != nullptr)
            dlclose(Found);
        return Found;
    }

    // GetProcAddress: nullptr when the module has no such export.
    // A resource module has none (Windows' data-file modules have no exports to find).
    inline Symbol ModuleSymbol(Module Library, char const* Name)
    {
        if (Library == nullptr)
            return nullptr;
        {
            std::lock_guard<std::mutex> Lock(Detail::Shared().ResourceModulesLock);
            if (Detail::ResourceModuleOf(Library) != nullptr)
                return nullptr;
        }
        return dlsym(Library, Name);
    }

    // GetModuleFileNameA for the executable (Library nullptr): the length written, 0 on failure.
    // Other modules are not named here yet.
    namespace Detail
    {
    // Whether Name is the file the loader handed out Library for: reopening a loaded file
    // with RTLD_NOLOAD gives the same handle and takes no new load.
    inline bool IsModuleFile(Module Library, char const* Name)
    {
        if (Name == nullptr || Name[0] == 0)
            return false;
        void* const Handle = dlopen(Name, RTLD_LAZY | RTLD_NOLOAD);
        if (Handle == nullptr)
            return false;
        dlclose(Handle);
        return Handle == Library;
    }

#if !defined(__APPLE__)
    // dl_iterate_phdr holds the loader's lock while it calls back, so the callback only
    // collects the names; they are matched (dlopen) after the iteration.
    inline int CollectModuleFile(dl_phdr_info* Info, std::size_t, void* Data)
    {
        if (Info->dlpi_name != nullptr && Info->dlpi_name[0] != 0)
            static_cast<std::vector<std::string>*>(Data)->push_back(Info->dlpi_name);
        return 0;
    }
#endif
    }

    // GetModuleFileNameA: the executable's path (Library nullptr) or a loaded module's. As
    // Windows: the length copied; Size when the path does not fit (truncated, terminated); 0 on
    // failure.
    inline FlULong ModulePath(Module Library, char* Path, FlULong Size)
    {
        if (Path == nullptr || Size == 0)
            return 0;
        std::string Found;
        if (Library == nullptr)
        {
            char Buffer[4096] = {};
#if defined(__APPLE__)
            std::uint32_t Length = sizeof(Buffer);
            if (_NSGetExecutablePath(Buffer, &Length) != 0)
                return 0;
#else
            ssize_t const Length = readlink("/proc/self/exe", Buffer, sizeof(Buffer) - 1);
            if (Length <= 0)
                return 0;
            Buffer[Length] = 0;
#endif
            Found = Buffer;
        }
        else
        {
            {
                std::lock_guard<std::mutex> Lock(Detail::Shared().ResourceModulesLock);
                if (Detail::FResourceModule const* const Resources =
                        Detail::ResourceModuleOf(Library))
                    Found = Resources->Path;
            }
#if defined(__APPLE__)
            for (std::uint32_t Index = 0, Count = _dyld_image_count();
                 Found.empty() && Index < Count; ++Index)
            {
                if (Detail::IsModuleFile(Library, _dyld_get_image_name(Index)))
                {
                    Found = _dyld_get_image_name(Index);
                    break;
                }
            }
#else
            std::vector<std::string> Names;
            if (Found.empty())
                dl_iterate_phdr(&Detail::CollectModuleFile, &Names);
            for (std::string const& Name : Names)
            {
                if (Detail::IsModuleFile(Library, Name.c_str()))
                {
                    Found = Name;
                    break;
                }
            }
#endif
            if (Found.empty())
                return 0;
        }
        if (Found.size() >= Size)
        {
            std::memcpy(Path, Found.data(), Size - 1);
            Path[Size - 1] = 0;
            return Size;
        }
        std::memcpy(Path, Found.c_str(), Found.size() + 1);
        return static_cast<FlULong>(Found.size());
    }

    // A resource-only DLL (LoadLibraryExA with LOAD_LIBRARY_AS_DATAFILE |
    // LOAD_LIBRARY_AS_IMAGE_RESOURCE, as the x64 Windows build loads them): a PE32 or PE32+
    // image, mapped read-only for its resources (FindResourceExA, LoadStringW, ModuleFree,
    // ModulePath; ModuleSymbol finds nothing). A bare name is looked for next to the
    // executable first, then in the current directory, as Windows' search starts with the
    // application's directory. Each load maps the file again, as Windows maps each data-file
    // load. nullptr when the file is missing or is not a PE image.
    inline Module ResourceModuleLoad(char const* Path)
    {
        if (Path == nullptr || Path[0] == 0)
            return nullptr;
        std::vector<std::string> Candidates;
        if (std::strpbrk(Path, "/\\") == nullptr)
        {
            char Executable[4096];
            FlULong const Length = ModulePath(nullptr, Executable, sizeof(Executable));
            if (Length != 0 && Length < sizeof(Executable))
            {
                std::string Directory(Executable, Length);
                Directory.erase(Directory.find_last_of('/') + 1);
                Candidates.push_back(Detail::ResolvePath((Directory + Path).c_str()));
            }
        }
        Candidates.push_back(Detail::ResolvePath(Path));

        for (std::string const& Candidate : Candidates)
        {
            // O_NONBLOCK: a FIFO without a writer must not hold the open (S_ISREG refuses it).
            int const Descriptor = open(Candidate.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
            if (Descriptor < 0)
                continue;
            struct stat Info = {};
            void* Mapped = MAP_FAILED;
            std::size_t Size = 0;
            if (fstat(Descriptor, &Info) == 0 && S_ISREG(Info.st_mode) && Info.st_size > 0 &&
                static_cast<std::uint64_t>(Info.st_size) <= SIZE_MAX)
            {
                Size = static_cast<std::size_t>(Info.st_size);
                Mapped = mmap(nullptr, Size, PROT_READ, MAP_PRIVATE, Descriptor, 0);
            }
            close(Descriptor);
            if (Mapped == MAP_FAILED)
                continue;

            std::unique_ptr<Detail::FResourceModule> Resources(new (std::nothrow)
                                                                   Detail::FResourceModule);
            if (Resources == nullptr)
            {
                munmap(Mapped, Size);
                return nullptr;
            }
            Resources->Base = static_cast<unsigned char const*>(Mapped);
            Resources->Size = Size;
            if (!PeResources::Open(Resources->Base, Size, Resources->Image))
                return nullptr; // not a PE image (the destructor unmaps it)
            try
            {
                Resources->Path = Candidate;
                Detail::FShared& State = Detail::Shared();
                std::lock_guard<std::mutex> Lock(State.ResourceModulesLock);
                State.ResourceModules[reinterpret_cast<std::uintptr_t>(Mapped)] = Resources.get();
            }
            catch (std::bad_alloc const&)
            {
                return nullptr;
            }
            Resources.release(); // the registry's now
            return Mapped;
        }
        return nullptr;
    }

    // FindResourceExA: the resource's data entry in a resource module (HRSRC), or nullptr. Type
    // and Name are MAKEINTRESOURCE ids, "#<id>" or strings. Library nullptr (the executable) and
    // the modules ModuleLoad loads have no PE resources here: nullptr.
    inline void const* ResourceFind(Module Library, char const* Type, char const* Name,
                                    unsigned short Language)
    {
        if (Library == nullptr || Type == nullptr || Name == nullptr)
            return nullptr;
        std::lock_guard<std::mutex> Lock(Detail::Shared().ResourceModulesLock);
        Detail::FResourceModule const* const Resources = Detail::ResourceModuleOf(Library);
        std::uint32_t Entry = 0;
        if (Resources == nullptr || !PeResources::Find(Resources->Image, PeResources::NameOf(Type),
                                                       PeResources::NameOf(Name), Language, Entry))
            return nullptr;
        return Resources->Base + Entry;
    }

    namespace Detail
    {
    // The data of the resource whose data entry is at Info: its address and size, or false.
    // The module is the one Info lies in; Library is not needed (as LoadResource and
    // SizeofResource take the module only to find the image). Under ResourceModulesLock.
    inline bool ResourceDataOf(void const* Info, void const*& Data, FlULong& Size)
    {
        FResourceModule const* const Resources = ResourceModuleHolding(Info);
        if (Resources == nullptr)
            return false;
        std::uint32_t Offset = 0, Bytes = 0;
        if (!PeResources::Data(Resources->Image,
                               static_cast<std::uint32_t>(static_cast<unsigned char const*>(Info) -
                                                          Resources->Base),
                               Offset, Bytes))
            return false;
        Data = Resources->Base + Offset;
        Size = Bytes;
        return true;
    }
    }

    // SizeofResource: the resource's size in bytes, 0 for an unknown HRSRC.
    inline FlULong ResourceSize(Module Library, void const* Info)
    {
        (void)Library;
        void const* Data = nullptr;
        FlULong Size = 0;
        std::lock_guard<std::mutex> Lock(Detail::Shared().ResourceModulesLock);
        return Detail::ResourceDataOf(Info, Data, Size) ? Size : 0;
    }

    // LoadResource (and LockResource, which returns it): the resource's data, in the module's
    // read-only mapping, or nullptr for an unknown HRSRC.
    inline void const* ResourceData(Module Library, void const* Info)
    {
        (void)Library;
        void const* Data = nullptr;
        FlULong Size = 0;
        std::lock_guard<std::mutex> Lock(Detail::Shared().ResourceModulesLock);
        return Detail::ResourceDataOf(Info, Data, Size) ? Data : nullptr;
    }

    // Resource text as wchar_t: Text points at UTF-16LE units in a resource of a resource
    // module (wchar_t is 4 bytes here). The whole resource is converted on first use, one
    // wchar_t per unit as the wire's text (rpc_wire_wide.h), so counts in units stay counts in
    // characters, and followed by a terminator; the result lives as long as the module, as the
    // mapped text does. At least Units units follow Text in the resource the copy is made of (a
    // damaged image's resources may overlap: one that holds them is taken). nullptr when Text is
    // not in a resource, falls between two units or has fewer than Units after it.
    inline wchar_t const* ResourceWideText(void const* Text, std::size_t Units = 0)
    {
        std::lock_guard<std::mutex> Lock(Detail::Shared().ResourceModulesLock);
        Detail::FResourceModule* const Resources = Detail::ResourceModuleHolding(Text);
        if (Resources == nullptr)
            return nullptr;
        try
        {
            if (!Resources->bIndexed)
            {
                // Built aside and kept only when complete: a failed attempt leaves nothing behind.
                std::vector<std::pair<std::uint32_t, std::uint32_t>> Index;
                PeResources::ForEach(Resources->Image, [&](std::uint32_t Offset, std::uint32_t Size)
                                     { Index.emplace_back(Offset, Size); });
                std::sort(Index.begin(), Index.end());
                Resources->Resources = std::move(Index);
                Resources->bIndexed = true;
            }
            std::uint32_t const Into = static_cast<std::uint32_t>(
                static_cast<unsigned char const*>(Text) - Resources->Base);
            // The nearest resource that starts at or before Text and holds Units units after it.
            auto Found = std::upper_bound(Resources->Resources.begin(), Resources->Resources.end(),
                                          std::make_pair(Into, ~std::uint32_t(0)));
            while (Found != Resources->Resources.begin())
            {
                --Found;
                std::uint32_t const Start = Found->first;
                std::uint32_t const Count = Found->second / 2;
                if ((Into - Start) % 2 != 0 || (Into - Start) / 2 > Count ||
                    Count - (Into - Start) / 2 < Units)
                    continue;
                std::unique_ptr<wchar_t[]>& Wide = Resources->Wide[*Found];
                if (Wide == nullptr)
                {
                    Wide.reset(new wchar_t[Count + 1]);
                    unsigned char const* const In = Resources->Base + Start;
                    for (std::uint32_t Index = 0; Index < Count; ++Index)
                        Wide[Index] =
                            static_cast<wchar_t>(In[Index * 2] | (In[Index * 2 + 1] << 8));
                    Wide[Count] = 0;
                }
                return Wide.get() + (Into - Start) / 2;
            }
            return nullptr;
        }
        catch (std::bad_alloc const&)
        {
            return nullptr;
        }
    }

    // ---------------------------------------------------------------------------------------
    // Paths
    // ---------------------------------------------------------------------------------------

    namespace Detail
    {
        // Copies Text into Out as GetCurrentDirectoryA / GetFullPathNameA do: the length written,
        // or the size needed (with the terminator) when Size is too small.
        inline FlULong CopyOut(std::string const& Text, char* Out, FlULong Size)
        {
            if (Out == nullptr || Size <= Text.size())
                return static_cast<FlULong>(Text.size() + 1);
            std::memcpy(Out, Text.c_str(), Text.size() + 1);
            return static_cast<FlULong>(Text.size());
        }
    }

    // GetCurrentDirectoryA: the length written, or the size needed when Size is too small.
    inline FlULong CurrentDirectory(char* Path, FlULong Size)
    {
        char Buffer[4096];
        if (getcwd(Buffer, sizeof(Buffer)) == nullptr)
            return 0;
        return Detail::CopyOut(Buffer, Path, Size);
    }

    // SetCurrentDirectoryA.
    inline bool ChangeDirectory(char const* Path) { return chdir(Detail::ResolvePath(Path).c_str()) == 0; }

    // GetFullPathNameA: the length written, or the size needed when Size is too small. FilePart
    // (may be nullptr) receives the last component. The path is made absolute and its '.' and
    // '..' are folded; it is not resolved against the disk.
    inline FlULong FullPath(char const* Path, char* Full, FlULong Size, char** FilePart = nullptr)
    {
        std::string Text = Path != nullptr ? Path : "";
        for (char& Character : Text)
        {
            if (Character == '\\')
                Character = '/';
        }
        if (Text.empty() || Text[0] != '/')
        {
            char Buffer[4096];
            if (getcwd(Buffer, sizeof(Buffer)) == nullptr)
                return 0;
            Text = std::string(Buffer) + "/" + Text;
        }
        std::string Folded;
        std::size_t Position = 0;
        while (Position <= Text.size())
        {
            std::size_t Slash = Text.find('/', Position);
            if (Slash == std::string::npos)
                Slash = Text.size();
            std::string const Component = Text.substr(Position, Slash - Position);
            Position = Slash + 1;
            if (Component.empty() || Component == ".")
                continue;
            if (Component == "..")
            {
                std::size_t const Last = Folded.find_last_of('/');
                Folded.erase(Last == std::string::npos ? 0 : Last);
                continue;
            }
            Folded += "/" + Component;
        }
        if (Folded.empty())
            Folded = "/";
        FlULong const Result = Detail::CopyOut(Folded, Full, Size);
        if (FilePart != nullptr && Full != nullptr && Result < Size)
            *FilePart = Full + Folded.find_last_of('/') + 1;
        return Result;
    }

    // GetTempPathA: the length written, with the trailing separator.
    inline FlULong TempDirectory(char* Path, FlULong Size)
    {
        char const* const Variable = std::getenv("TMPDIR");
        std::string Directory = Variable != nullptr && *Variable != 0 ? Variable : "/tmp";
        if (Directory.back() != '/')
            Directory += '/';
        return Detail::CopyOut(Directory, Path, Size);
    }
    } // namespace Platform

    // Defines the process's platform state: in dacom, and in a program that uses the layer without
    // dacom (the tests). Never destroyed: handles retail keeps open until the process ends stay
    // reachable, and a module's static destructors may still close handles after another's ran.
#define FL_PLATFORM_DEFINE_SHARED()                                                            \
        extern "C" __attribute__((visibility("default"))) Platform::Detail::FShared*               \
        FlPlatformShared()                                                                         \
        {                                                                                          \
            static Platform::Detail::FShared* const Instance = new Platform::Detail::FShared();    \
            return Instance;                                                                       \
        }
