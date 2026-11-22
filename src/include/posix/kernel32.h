// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): the kernel32 file and memory calls the retail code makes directly, on macOS and
// Linux, over the POSIX platform layer (platform_posix.h). Part of the stand-in <windows.h>
// (posix/windows.h includes it), which the Windows builds never see.
//
// - Handles are the platform layer's (small numbers, so they survive retail's 32-bit fields).
// - GetLastError is per thread, set from errno by the calls here, with the Win32 error codes.
// - File mappings are read-only (PAGE_READONLY / FILE_MAP_READ): what the retail code uses.
// - VirtualAlloc commits fresh zeroed pages (mmap); the address is not below 4 GB, so it must
//   not go into a 32-bit field (LowArena is for that).

#pragma once

#if defined(_WIN32)
#error "posix/kernel32.h is for macOS and Linux"
#endif

#include "platform.h"

#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <mutex>
#include <string>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

// ---------------------------------------------------------------------------------------------
// Errors
// ---------------------------------------------------------------------------------------------

#define ERROR_SUCCESS 0
#define NO_ERROR 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_PATH_NOT_FOUND 3
#define ERROR_TOO_MANY_OPEN_FILES 4
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_NOT_ENOUGH_MEMORY 8
#define ERROR_OUTOFMEMORY 14
#define ERROR_SHARING_VIOLATION 32
#define ERROR_HANDLE_EOF 38
#define ERROR_FILE_EXISTS 80
#define ERROR_INVALID_PARAMETER 87
#define ERROR_DISK_FULL 112
#define ERROR_NEGATIVE_SEEK 131
#define ERROR_ALREADY_EXISTS 183

namespace PosixWin32
{
// The calling thread's last error: one per thread for the whole process (a pthread key of the
// platform's shared state; a thread_local would be one per module).
inline DWORD& LastError()
{
    const pthread_key_t Key = Platform::Detail::Shared().LastErrorKey;
    DWORD* Value = static_cast<DWORD*>(pthread_getspecific(Key));
    if (Value == nullptr)
    {
        Value = static_cast<DWORD*>(std::calloc(1, sizeof(DWORD)));
        if (Value == nullptr)
        {
            static thread_local DWORD Fallback = ERROR_SUCCESS; // out of memory: per module
            return Fallback;
        }
        pthread_setspecific(Key, Value);
    }
    return *Value;
}

    // The Win32 error for an errno value.
inline DWORD ErrorOf(int Errno)
{
    switch (Errno)
    {
    case 0:
        return ERROR_SUCCESS;
    case ENOENT:
        return ERROR_FILE_NOT_FOUND;
    case ENOTDIR:
        return ERROR_PATH_NOT_FOUND;
    case EACCES:
    case EPERM:
    case EISDIR:
    case EROFS:
        return ERROR_ACCESS_DENIED;
    case EBADF:
        return ERROR_INVALID_HANDLE;
    case ENOMEM:
        return ERROR_NOT_ENOUGH_MEMORY;
    case EMFILE:
    case ENFILE:
        return ERROR_TOO_MANY_OPEN_FILES;
    case EEXIST:
        return ERROR_FILE_EXISTS;
    case ENOSPC:
        return ERROR_DISK_FULL;
    case EINVAL:
        return ERROR_INVALID_PARAMETER;
    default:
        return ERROR_ACCESS_DENIED;
    }
}

inline void SetErrorFromErrno()
{
    LastError() = ErrorOf(errno);
}

    // The descriptor behind a file handle, or -1 (ERROR_INVALID_HANDLE set).
inline int DescriptorOf(HANDLE File)
{
    Platform::Detail::FFile* const Object = Platform::Detail::As<Platform::Detail::FFile>(File);
    if (Object == nullptr)
    {
        LastError() = ERROR_INVALID_HANDLE;
        return -1;
    }
    return Object->Descriptor;
}
}

inline DWORD GetLastError()
{
    return PosixWin32::LastError();
}
inline void SetLastError(DWORD Error)
{
    PosixWin32::LastError() = Error;
}

// ---------------------------------------------------------------------------------------------
// Files
// ---------------------------------------------------------------------------------------------

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1
#define FILE_SHARE_WRITE 0x2
#define FILE_SHARE_DELETE 0x4
#define CREATE_NEW 1
#define CREATE_ALWAYS 2
#define OPEN_EXISTING 3
#define OPEN_ALWAYS 4
#define TRUNCATE_EXISTING 5
#define FILE_FLAG_SEQUENTIAL_SCAN 0x08000000
#define FILE_FLAG_RANDOM_ACCESS 0x10000000
#define FILE_BEGIN 0
#define FILE_CURRENT 1
#define FILE_END 2
#define INVALID_SET_FILE_POINTER ((DWORD)0xFFFFFFFFu)
#define INVALID_FILE_SIZE ((DWORD)0xFFFFFFFFu)

struct SECURITY_ATTRIBUTES
{
    DWORD nLength;
    LPVOID lpSecurityDescriptor;
    BOOL bInheritHandle;
};
typedef SECURITY_ATTRIBUTES* LPSECURITY_ATTRIBUTES;
struct OVERLAPPED;
typedef OVERLAPPED* LPOVERLAPPED;

inline BOOL CloseHandle(HANDLE Object)
{
    if (Platform::CloseHandle(Object))
        return TRUE;
    PosixWin32::LastError() = ERROR_INVALID_HANDLE;
    return FALSE;
}

// CreateFileA. Security, flags and template are not used here (no ACLs, no overlapped I/O).
inline HANDLE CreateFileA(LPCSTR Path, DWORD Access, DWORD Share, LPSECURITY_ATTRIBUTES,
                          DWORD Creation, DWORD FlagsAndAttributes, HANDLE)
{
    errno = 0;
    HANDLE const File = Platform::FileOpen(Path, Access, Share, Creation, FlagsAndAttributes);
    if (File == Platform::InvalidHandle())
    {
        // A directory (refused without errno) is ERROR_ACCESS_DENIED, as on Windows.
        PosixWin32::LastError() = errno != 0 ? PosixWin32::ErrorOf(errno) : ERROR_ACCESS_DENIED;
        return File;
    }
    // OPEN_ALWAYS / CREATE_ALWAYS report an existing file through ERROR_ALREADY_EXISTS.
    PosixWin32::LastError() = ERROR_SUCCESS;
    return File;
}

// ReadFile / WriteFile, synchronous (no OVERLAPPED here). Reading at the end succeeds with 0.
inline BOOL ReadFile(HANDLE File, LPVOID Buffer, DWORD Size, LPDWORD Read, LPOVERLAPPED)
{
    errno = 0;
    if (Platform::FileRead(File, Buffer, Size, Read))
        return TRUE;
    PosixWin32::LastError() = errno != 0 ? PosixWin32::ErrorOf(errno) : ERROR_INVALID_HANDLE;
    return FALSE;
}
inline BOOL WriteFile(HANDLE File, LPCVOID Buffer, DWORD Size, LPDWORD Written, LPOVERLAPPED)
{
    errno = 0;
    if (Platform::FileWrite(File, Buffer, Size, Written))
        return TRUE;
    PosixWin32::LastError() = errno != 0 ? PosixWin32::ErrorOf(errno) : ERROR_INVALID_HANDLE;
    return FALSE;
}

// SetFilePointer. Without High, Distance is a signed 32-bit move. A position before the start
// fails with ERROR_NEGATIVE_SEEK and leaves the pointer where it was.
inline DWORD SetFilePointer(HANDLE File, LONG Distance, LONG* High, DWORD Method)
{
    int const Descriptor = PosixWin32::DescriptorOf(File);
    if (Descriptor < 0)
        return INVALID_SET_FILE_POINTER;
    std::int64_t const Move =
        High != nullptr
            ? static_cast<std::int64_t>(
                  (static_cast<std::uint64_t>(static_cast<std::uint32_t>(*High)) << 32) |
                  static_cast<std::uint32_t>(Distance))
            : static_cast<std::int64_t>(Distance);
    std::int64_t Base = 0;
    if (Method == FILE_CURRENT)
        Base = lseek(Descriptor, 0, SEEK_CUR);
    else if (Method == FILE_END)
    {
        struct stat Info;
        Base = fstat(Descriptor, &Info) == 0 ? static_cast<std::int64_t>(Info.st_size) : -1;
    }
    else if (Method != FILE_BEGIN)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return INVALID_SET_FILE_POINTER;
    }
    if (Base < 0)
    {
        PosixWin32::SetErrorFromErrno();
        return INVALID_SET_FILE_POINTER;
    }
    std::int64_t const Target = Base + Move;
    if (Target < 0)
    {
        PosixWin32::LastError() = ERROR_NEGATIVE_SEEK;
        return INVALID_SET_FILE_POINTER;
    }
    if (lseek(Descriptor, static_cast<off_t>(Target), SEEK_SET) < 0)
    {
        PosixWin32::SetErrorFromErrno();
        return INVALID_SET_FILE_POINTER;
    }
    if (High != nullptr)
        *High = static_cast<LONG>(static_cast<std::uint64_t>(Target) >> 32);
    PosixWin32::LastError() = ERROR_SUCCESS;
    return static_cast<DWORD>(static_cast<std::uint64_t>(Target) & 0xFFFFFFFFu);
}

inline DWORD GetFileSize(HANDLE File, LPDWORD High)
{
    int const Descriptor = PosixWin32::DescriptorOf(File);
    struct stat Info;
    if (Descriptor < 0 || fstat(Descriptor, &Info) != 0)
    {
        if (Descriptor >= 0)
            PosixWin32::SetErrorFromErrno();
        return INVALID_FILE_SIZE;
    }
    std::uint64_t const Size = static_cast<std::uint64_t>(Info.st_size);
    if (High != nullptr)
        *High = static_cast<DWORD>(Size >> 32);
    PosixWin32::LastError() = ERROR_SUCCESS;
    return static_cast<DWORD>(Size & 0xFFFFFFFFu);
}

// GetFileTime: creation is the status-change time here (POSIX keeps no creation time portably).
inline BOOL GetFileTime(HANDLE File, LPFILETIME Creation, LPFILETIME Access, LPFILETIME Write)
{
    int const Descriptor = PosixWin32::DescriptorOf(File);
    struct stat Info;
    if (Descriptor < 0 || fstat(Descriptor, &Info) != 0)
    {
        if (Descriptor >= 0)
            PosixWin32::SetErrorFromErrno();
        return FALSE;
    }
    if (Creation != nullptr)
        Platform::Detail::ToFileTime(Info.st_ctime, &Creation->dwLowDateTime,
                                     &Creation->dwHighDateTime);
    if (Access != nullptr)
        Platform::Detail::ToFileTime(Info.st_atime, &Access->dwLowDateTime,
                                     &Access->dwHighDateTime);
    if (Write != nullptr)
        Platform::Detail::ToFileTime(Info.st_mtime, &Write->dwLowDateTime, &Write->dwHighDateTime);
    return TRUE;
}

// DosDateTimeToFileTime: the DOS fields as they are (no time zone change), as on Windows. Fails
// for a field out of range.
inline BOOL DosDateTimeToFileTime(WORD Date, WORD Time, LPFILETIME Out)
{
    unsigned const Year = 1980u + (Date >> 9);
    unsigned const Month = (Date >> 5) & 0x0Fu;
    unsigned const Day = Date & 0x1Fu;
    unsigned const Hour = Time >> 11;
    unsigned const Minute = (Time >> 5) & 0x3Fu;
    unsigned const Second = (Time & 0x1Fu) * 2u;
    static constexpr unsigned kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool const bLeap = (Year % 4 == 0 && Year % 100 != 0) || Year % 400 == 0;
    if (Month < 1 || Month > 12 || Day < 1 ||
        Day > kDays[Month - 1] + (Month == 2 && bLeap ? 1u : 0u) || Hour > 23 || Minute > 59 ||
        Second > 59)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    // Days since 1970-01-01 (Howard Hinnant's days_from_civil), then 100 ns ticks since 1601.
    int const Y = static_cast<int>(Year) - (Month <= 2 ? 1 : 0);
    int const Era = Y / 400;
    unsigned const YearOfEra = static_cast<unsigned>(Y - Era * 400);
    unsigned const DayOfYear = (153 * (Month + (Month > 2 ? -3 : 9)) + 2) / 5 + Day - 1;
    unsigned const DayOfEra = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
    std::int64_t const Days =
        static_cast<std::int64_t>(Era) * 146097 + static_cast<std::int64_t>(DayOfEra) - 719468;
    std::int64_t const Seconds = Days * 86400 + Hour * 3600 + Minute * 60 + Second;
    std::uint64_t const Ticks = static_cast<std::uint64_t>(Seconds + 11644473600LL) * 10000000ULL;
    Out->dwLowDateTime = static_cast<DWORD>(Ticks & 0xFFFFFFFFu);
    Out->dwHighDateTime = static_cast<DWORD>(Ticks >> 32);
    return TRUE;
}

// _fullpath: the absolute path ('.' and '..' folded), into Buffer (Size bytes), or a new
// malloc'd one when Buffer is nullptr. nullptr when it does not fit.
inline char* _fullpath(char* Buffer, char const* Path, std::size_t Size)
{
    char Full[4096];
    FlULong const Length = Platform::FullPath(Path, Full, sizeof(Full));
    if (Length == 0 || Length >= sizeof(Full))
        return nullptr;
    if (Buffer == nullptr)
        return strdup(Full);
    if (Length + 1 > Size)
        return nullptr;
    std::memcpy(Buffer, Full, Length + 1);
    return Buffer;
}

// ---------------------------------------------------------------------------------------------
// File mappings (read-only)
// ---------------------------------------------------------------------------------------------

#define PAGE_NOACCESS 0x01
#define PAGE_READONLY 0x02
#define PAGE_READWRITE 0x04
#define FILE_MAP_COPY 0x01
#define FILE_MAP_WRITE 0x02
#define FILE_MAP_READ 0x04

namespace PosixWin32
{
    // A file mapping object: its own descriptor of the file, so it outlives the file handle,
    // as on Windows.
struct FMapping : Platform::Detail::FObject
{
    static constexpr unsigned char kKind = Platform::Detail::kKindMapping;
    FMapping() { Kind = kKind; }
    int Descriptor = -1;
    std::uint64_t Size = 0;
    ~FMapping() override
    {
        if (Descriptor >= 0)
            close(Descriptor);
    }
};

    // The length of each view (and VirtualAlloc block), for UnmapViewOfFile / VirtualFree.
// The view and block lengths are the process's (a view mapped in one module may be unmapped in
// another): the platform's shared state.
inline void Remember(void const* Address, std::size_t Length)
{
    Platform::Detail::FShared& State = Platform::Detail::Shared();
    std::lock_guard<std::mutex> Lock(State.RegionsLock);
    State.Regions[Address] = Length;
}
    // The length of the region at Address (removed), or 0.
inline std::size_t Forget(void const* Address)
{
    Platform::Detail::FShared& State = Platform::Detail::Shared();
    std::lock_guard<std::mutex> Lock(State.RegionsLock);
    auto const Found = State.Regions.find(Address);
    if (Found == State.Regions.end())
        return 0;
    std::size_t const Length = Found->second;
    State.Regions.erase(Found);
    return Length;
}
}

// CreateFileMappingA over a file, read-only and unnamed. Size 0 (both halves) is the file's.
inline HANDLE CreateFileMappingA(HANDLE File, LPSECURITY_ATTRIBUTES, DWORD Protect, DWORD SizeHigh,
                                 DWORD SizeLow, LPCSTR Name)
{
    int const Descriptor = PosixWin32::DescriptorOf(File);
    if (Descriptor < 0)
        return nullptr;
    if (Protect != PAGE_READONLY || Name != nullptr)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return nullptr;
    }
    struct stat Info;
    if (fstat(Descriptor, &Info) != 0)
    {
        PosixWin32::SetErrorFromErrno();
        return nullptr;
    }
    std::uint64_t Size = (static_cast<std::uint64_t>(SizeHigh) << 32) | SizeLow;
    if (Size == 0)
        Size = static_cast<std::uint64_t>(Info.st_size);
    if (Size == 0 || Size > static_cast<std::uint64_t>(Info.st_size))
    {
        // Windows refuses a mapping of an empty file (ERROR_FILE_INVALID); a read-only mapping
        // cannot grow the file either.
        PosixWin32::LastError() = Size == 0 ? 1006 /* ERROR_FILE_INVALID */ : ERROR_ACCESS_DENIED;
        return nullptr;
    }
    PosixWin32::FMapping* const Mapping = new (std::nothrow) PosixWin32::FMapping();
    if (Mapping == nullptr || (Mapping->Descriptor = fcntl(Descriptor, F_DUPFD_CLOEXEC, 0)) < 0)
    {
        delete Mapping;
        PosixWin32::LastError() = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    Mapping->Size = Size;
    HANDLE const Value = Platform::Detail::NewHandle(Mapping);
    if (Value == nullptr)
    {
        delete Mapping;
        PosixWin32::LastError() = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    PosixWin32::LastError() = ERROR_SUCCESS;
    return Value;
}

// MapViewOfFile, FILE_MAP_READ. Size 0 maps from Offset to the end of the mapping. The offset
// must be a multiple of the allocation granularity, as on Windows (64 KB there; the page size
// here divides it).
inline LPVOID MapViewOfFile(HANDLE MappingHandle, DWORD Access, DWORD OffsetHigh, DWORD OffsetLow,
                            SIZE_T Size)
{
    PosixWin32::FMapping* const Mapping = Platform::Detail::As<PosixWin32::FMapping>(MappingHandle);
    if (Mapping == nullptr)
    {
        PosixWin32::LastError() = ERROR_INVALID_HANDLE;
        return nullptr;
    }
    std::uint64_t const Offset = (static_cast<std::uint64_t>(OffsetHigh) << 32) | OffsetLow;
    if (Access != FILE_MAP_READ || Offset > Mapping->Size || Offset % 0x10000 != 0 ||
        (Size != 0 && Size > Mapping->Size - Offset))
    {
        PosixWin32::LastError() =
            Access != FILE_MAP_READ ? ERROR_ACCESS_DENIED : ERROR_INVALID_PARAMETER;
        return nullptr;
    }
    std::size_t const Length = Size != 0 ? Size : static_cast<std::size_t>(Mapping->Size - Offset);
    if (Length == 0)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return nullptr;
    }
    void* const View = mmap(nullptr, Length, PROT_READ, MAP_PRIVATE, Mapping->Descriptor,
                            static_cast<off_t>(Offset));
    if (View == MAP_FAILED)
    {
        PosixWin32::SetErrorFromErrno();
        return nullptr;
    }
    PosixWin32::Remember(View, Length);
    PosixWin32::LastError() = ERROR_SUCCESS;
    return View;
}

inline BOOL UnmapViewOfFile(LPCVOID View)
{
    std::size_t const Length = PosixWin32::Forget(View);
    if (Length == 0 || munmap(const_cast<void*>(View), Length) != 0)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Virtual memory
// ---------------------------------------------------------------------------------------------

#define MEM_COMMIT 0x1000
#define MEM_RESERVE 0x2000
#define MEM_DECOMMIT 0x4000
#define MEM_RELEASE 0x8000

// VirtualAlloc of fresh read-write pages (MEM_COMMIT, with or without MEM_RESERVE), zeroed, at
// an address of the system's choosing.
inline LPVOID VirtualAlloc(LPVOID Address, SIZE_T Size, DWORD Type, DWORD Protect)
{
    if (Address != nullptr || Size == 0 || (Type & MEM_COMMIT) == 0 || Protect != PAGE_READWRITE)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return nullptr;
    }
    void* const Block = mmap(nullptr, Size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (Block == MAP_FAILED)
    {
        PosixWin32::LastError() = ERROR_NOT_ENOUGH_MEMORY;
        return nullptr;
    }
    PosixWin32::Remember(Block, Size);
    return Block;
}

// VirtualFree(Block, 0, MEM_RELEASE) of a VirtualAlloc block.
inline BOOL VirtualFree(LPVOID Block, SIZE_T Size, DWORD Type)
{
    if (Size != 0 || Type != MEM_RELEASE)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    std::size_t const Length = PosixWin32::Forget(Block);
    if (Length == 0 || munmap(Block, Length) != 0)
    {
        PosixWin32::LastError() = ERROR_INVALID_PARAMETER;
        return FALSE;
    }
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// The process heap: the C library's malloc and free (one heap, as the process heap is).
// ---------------------------------------------------------------------------------------------

#define HEAP_ZERO_MEMORY 0x00000008

inline HANDLE GetProcessHeap()
{
    return reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(0x4));
}
inline LPVOID HeapAlloc(HANDLE, DWORD Flags, SIZE_T Size)
{
    void* const Block = (Flags & HEAP_ZERO_MEMORY) != 0 ? std::calloc(1, Size != 0 ? Size : 1)
                                                        : std::malloc(Size != 0 ? Size : 1);
    if (Block == nullptr)
        PosixWin32::LastError() = ERROR_NOT_ENOUGH_MEMORY;
    return Block;
}
inline BOOL HeapFree(HANDLE, DWORD, LPVOID Block)
{
    std::free(Block);
    return TRUE;
}

// ---------------------------------------------------------------------------------------------
// Critical sections
// ---------------------------------------------------------------------------------------------

typedef Platform::CriticalSection CRITICAL_SECTION;
typedef CRITICAL_SECTION* LPCRITICAL_SECTION;
inline void InitializeCriticalSection(LPCRITICAL_SECTION Lock)
{
    Platform::LockInit(Lock);
}
inline void EnterCriticalSection(LPCRITICAL_SECTION Lock)
{
    Platform::LockEnter(Lock);
}
inline void LeaveCriticalSection(LPCRITICAL_SECTION Lock)
{
    Platform::LockLeave(Lock);
}
inline void DeleteCriticalSection(LPCRITICAL_SECTION Lock)
{
    Platform::LockDelete(Lock);
}

// ---------------------------------------------------------------------------------------------
// Time, sleep, interlocked counters and the process id, over the platform layer
// ---------------------------------------------------------------------------------------------

inline DWORD GetTickCount()
{
    return Platform::TickCountMs();
}
inline void Sleep(DWORD Milliseconds)
{
    Platform::SleepMs(Milliseconds);
}
inline LONG InterlockedIncrement(LONG volatile* Value)
{
    return Platform::AtomicIncrement(Value);
}
inline LONG InterlockedDecrement(LONG volatile* Value)
{
    return Platform::AtomicDecrement(Value);
}
inline BOOL QueryPerformanceCounter(LARGE_INTEGER* Counter)
{
    std::int64_t Ticks = 0;
    if (Counter == nullptr || !Platform::PerfCounter(&Ticks))
        return FALSE;
    Counter->QuadPart = Ticks;
    return TRUE;
}
inline BOOL QueryPerformanceFrequency(LARGE_INTEGER* Frequency)
{
    std::int64_t TicksPerSecond = 0;
    if (Frequency == nullptr || !Platform::PerfFrequency(&TicksPerSecond))
        return FALSE;
    Frequency->QuadPart = TicksPerSecond;
    return TRUE;
}
inline DWORD GetCurrentProcessId()
{
    return static_cast<DWORD>(getpid());
}

// CompareFileTime: -1, 0 or 1 as the first time is earlier, the same or later.
inline LONG CompareFileTime(const FILETIME* First, const FILETIME* Second)
{
    const std::uint64_t A = ((std::uint64_t)First->dwHighDateTime << 32) | First->dwLowDateTime;
    const std::uint64_t B = ((std::uint64_t)Second->dwHighDateTime << 32) | Second->dwLowDateTime;
    return A < B ? -1 : (A > B ? 1 : 0);
}

// SystemTimeToFileTime: the inverse of FileTimeToSystemTime (no time zone is applied; the
// caller's SYSTEMTIME is taken as it is, as Windows does). FALSE for an invalid date.
inline BOOL SystemTimeToFileTime(const SYSTEMTIME* Time, LPFILETIME Out)
{
    if (Time == nullptr || Out == nullptr || Time->wMonth < 1 || Time->wMonth > 12 ||
        Time->wDay < 1 || Time->wDay > 31 || Time->wHour > 23 || Time->wMinute > 59 ||
        Time->wSecond > 59 || Time->wMilliseconds > 999)
        return FALSE;
    // The day must exist in its month (February 29 only in a leap year).
    static const unsigned char kDaysInMonth[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    const unsigned FullYear = Time->wYear;
    const bool bLeap = (FullYear % 4 == 0 && FullYear % 100 != 0) || FullYear % 400 == 0;
    if (Time->wDay > kDaysInMonth[Time->wMonth - 1] + (Time->wMonth == 2 && bLeap ? 1 : 0))
        return FALSE;
    // Days from 1970-01-01 to the civil date (Howard Hinnant's days_from_civil).
    const int Year = (int)Time->wYear - (Time->wMonth <= 2 ? 1 : 0);
    const int Era = (Year >= 0 ? Year : Year - 399) / 400;
    const unsigned YearOfEra = (unsigned)(Year - Era * 400);
    const unsigned Month = Time->wMonth;
    const unsigned DayOfYear = (153 * (Month > 2 ? Month - 3 : Month + 9) + 2) / 5 + Time->wDay - 1;
    const unsigned DayOfEra = YearOfEra * 365 + YearOfEra / 4 - YearOfEra / 100 + DayOfYear;
    const long long Days = (long long)Era * 146097 + (long long)DayOfEra - 719468;
    const long long Seconds =
        Days * 86400 + Time->wHour * 3600LL + Time->wMinute * 60LL + Time->wSecond + 11644473600LL;
    if (Seconds < 0)
        return FALSE;
    const std::uint64_t Ticks =
        (std::uint64_t)Seconds * 10000000ULL + Time->wMilliseconds * 10000ULL;
    Out->dwLowDateTime = (DWORD)(Ticks & 0xFFFFFFFFu);
    Out->dwHighDateTime = (DWORD)(Ticks >> 32);
    return TRUE;
}

// GetStringTypeW for CT_CTYPE1, C1_SPACE only: the characters Windows 10 classes as spaces
// (0x09-0x0D, 0x20, 0x85, 0xA0, U+1680, U+180E, U+2000-U+200A, U+2028, U+2029, U+202F, U+205F,
// U+3000). The code asks nothing else of it.
#define CT_CTYPE1 0x0001
#define C1_SPACE 0x0008
inline BOOL GetStringTypeW(DWORD InfoType, const wchar_t* Text, int Count, WORD* Types)
{
    if (InfoType != CT_CTYPE1 || Text == nullptr || Types == nullptr)
        return FALSE;
    if (Count < 0)
        Count = (int)std::wcslen(Text) + 1;
    for (int Index = 0; Index < Count; ++Index)
    {
        const std::uint32_t C = (std::uint32_t)Text[Index];
        const bool bSpace = (C >= 0x09 && C <= 0x0D) || C == 0x20 || C == 0x85 || C == 0xA0 ||
                            C == 0x1680 || C == 0x180E || (C >= 0x2000 && C <= 0x200A) ||
                            C == 0x2028 || C == 0x2029 || C == 0x202F || C == 0x205F || C == 0x3000;
        Types[Index] = bSpace ? C1_SPACE : 0;
    }
    return TRUE;
}
