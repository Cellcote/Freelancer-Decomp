// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for <winsock2.h> on macOS and Linux (src/include/posix, non-Windows
// only), over BSD sockets: what DPNet's adapter (src/DALib/DPNetAdapter.cpp) uses.
//
// - A SOCKET is the file descriptor; INVALID_SOCKET and SOCKET_ERROR are -1, as the BSD calls
//   return.
// - WSAStartup / WSACleanup have nothing to set up and succeed.
// - ioctlsocket supports FIONBIO (O_NONBLOCK) and FIONREAD.
// - WSAGetLastError maps errno to the WSAE codes.
// - WSAIoctl knows SIO_UDP_CONNRESET only, and succeeds: a UDP socket here does not report an
//   ICMP port-unreachable on recvfrom unless it is connected, which is what turning
//   SIO_UDP_CONNRESET off asks for.
// - Exclusive binding is the default here (a second bind to a port in use fails without
//   SO_REUSEADDR / SO_REUSEPORT), so there is no SO_EXCLUSIVEADDRUSE.

#pragma once

#if defined(_WIN32)
#error "posix/winsock2.h is the stand-in for macOS and Linux; Windows uses the SDK's winsock2.h"
#endif

#include <windows.h>

#include <cerrno>

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

typedef int SOCKET;
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

typedef struct sockaddr SOCKADDR;
typedef struct sockaddr_in SOCKADDR_IN;
typedef struct sockaddr_in* PSOCKADDR_IN;
typedef struct sockaddr* PSOCKADDR;
typedef struct sockaddr* LPSOCKADDR;
typedef struct hostent HOSTENT;

// ---------------------------------------------------------------------------------------------
// Errors (WSABASEERR + the BSD number, as Windows has them)
// ---------------------------------------------------------------------------------------------

#define WSABASEERR 10000
#define WSAEINTR 10004
#define WSAEBADF 10009
#define WSAEACCES 10013
#define WSAEFAULT 10014
#define WSAEINVAL 10022
#define WSAEMFILE 10024
#define WSAEWOULDBLOCK 10035
#define WSAEINPROGRESS 10036
#define WSAEALREADY 10037
#define WSAENOTSOCK 10038
#define WSAEDESTADDRREQ 10039
#define WSAEMSGSIZE 10040
#define WSAEPROTOTYPE 10041
#define WSAENOPROTOOPT 10042
#define WSAEPROTONOSUPPORT 10043
#define WSAEOPNOTSUPP 10045
#define WSAEAFNOSUPPORT 10047
#define WSAEADDRINUSE 10048
#define WSAEADDRNOTAVAIL 10049
#define WSAENETDOWN 10050
#define WSAENETUNREACH 10051
#define WSAENETRESET 10052
#define WSAECONNABORTED 10053
#define WSAECONNRESET 10054
#define WSAENOBUFS 10055
#define WSAEISCONN 10056
#define WSAENOTCONN 10057
#define WSAETIMEDOUT 10060
#define WSAECONNREFUSED 10061
#define WSAEHOSTUNREACH 10065
#define WSANOTINITIALISED 10093
#define WSASYSCALLFAILURE 10107

// The WSAE code of the last socket call's errno (per thread, as errno is).
inline int WSAGetLastError()
{
    switch (errno)
    {
    case 0:
        return 0;
#if EAGAIN != EWOULDBLOCK
    case EAGAIN:
#endif
    case EWOULDBLOCK:
        return WSAEWOULDBLOCK;
    case EINTR:
        return WSAEINTR;
    case EBADF:
        return WSAEBADF;
    case EACCES:
        return WSAEACCES;
    case EFAULT:
        return WSAEFAULT;
    case EINVAL:
        return WSAEINVAL;
    case EMFILE:
        return WSAEMFILE;
    case EINPROGRESS:
        return WSAEINPROGRESS;
    case EALREADY:
        return WSAEALREADY;
    case ENOTSOCK:
        return WSAENOTSOCK;
    case EDESTADDRREQ:
        return WSAEDESTADDRREQ;
    case EMSGSIZE:
        return WSAEMSGSIZE;
    case EPROTOTYPE:
        return WSAEPROTOTYPE;
    case ENOPROTOOPT:
        return WSAENOPROTOOPT;
    case EPROTONOSUPPORT:
        return WSAEPROTONOSUPPORT;
    case EOPNOTSUPP:
        return WSAEOPNOTSUPP;
    case EAFNOSUPPORT:
        return WSAEAFNOSUPPORT;
    case EADDRINUSE:
        return WSAEADDRINUSE;
    case EADDRNOTAVAIL:
        return WSAEADDRNOTAVAIL;
    case ENETDOWN:
        return WSAENETDOWN;
    case ENETUNREACH:
        return WSAENETUNREACH;
    case ENETRESET:
        return WSAENETRESET;
    case ECONNABORTED:
        return WSAECONNABORTED;
    case ECONNRESET:
        return WSAECONNRESET;
    case ENOBUFS:
        return WSAENOBUFS;
    case EISCONN:
        return WSAEISCONN;
    case ENOTCONN:
        return WSAENOTCONN;
    case ETIMEDOUT:
        return WSAETIMEDOUT;
    case ECONNREFUSED:
        return WSAECONNREFUSED;
    case EHOSTUNREACH:
        return WSAEHOSTUNREACH;
    default:
        return WSASYSCALLFAILURE;
    }
}

// ---------------------------------------------------------------------------------------------
// Start-up
// ---------------------------------------------------------------------------------------------

#define WSADESCRIPTION_LEN 256
#define WSASYS_STATUS_LEN 128

typedef struct WSAData
{
    WORD wVersion;
    WORD wHighVersion;
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char* lpVendorInfo;
    char szDescription[WSADESCRIPTION_LEN + 1];
    char szSystemStatus[WSASYS_STATUS_LEN + 1];
} WSADATA, *LPWSADATA;

// There is nothing to start: the version asked for, and success.
inline int WSAStartup(WORD Version, LPWSADATA Data)
{
    if (Data != nullptr)
    {
        std::memset(Data, 0, sizeof(*Data));
        Data->wVersion = Version;
        Data->wHighVersion = MAKEWORD(2, 2);
        std::strcpy(Data->szDescription, "BSD sockets");
        std::strcpy(Data->szSystemStatus, "Running");
    }
    return 0;
}
inline int WSACleanup()
{
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Calls
// ---------------------------------------------------------------------------------------------

inline int closesocket(SOCKET Socket)
{
    return close(Socket);
}

// FIONBIO (non-zero *Argument: non-blocking) and FIONREAD (the bytes waiting).
inline int ioctlsocket(SOCKET Socket, unsigned long Command, u_long* Argument)
{
    if (Argument == nullptr)
    {
        errno = EFAULT;
        return SOCKET_ERROR;
    }
    if (Command == static_cast<unsigned long>(FIONBIO))
    {
        int const Flags = fcntl(Socket, F_GETFL, 0);
        if (Flags == -1)
            return SOCKET_ERROR;
        int const NewFlags = *Argument != 0 ? (Flags | O_NONBLOCK) : (Flags & ~O_NONBLOCK);
        return fcntl(Socket, F_SETFL, NewFlags) == -1 ? SOCKET_ERROR : 0;
    }
    if (Command == static_cast<unsigned long>(FIONREAD))
    {
        int Waiting = 0;
        if (ioctl(Socket, FIONREAD, &Waiting) == -1)
            return SOCKET_ERROR;
        *Argument = static_cast<u_long>(Waiting);
        return 0;
    }
    errno = EINVAL;
    return SOCKET_ERROR;
}

// WSAIoctl's one control code here: SIO_UDP_CONNRESET, _WSAIOW(IOC_VENDOR, 12). Its value, not
// the IOC_ macros: BSD's <sys/ioccom.h> and Linux's <asm-generic/ioctl.h> define those too, and
// Linux's IOC_IN and IOC_OUT are the other way round.
#define SIO_UDP_CONNRESET ((DWORD)0x9800000Cu)

typedef struct _WSAOVERLAPPED* LPWSAOVERLAPPED;
typedef void (*LPWSAOVERLAPPED_COMPLETION_ROUTINE)(DWORD Error, DWORD Transferred,
                                                   LPWSAOVERLAPPED Overlapped, DWORD Flags);

// SIO_UDP_CONNRESET only, which succeeds: an unconnected UDP socket here does not report ICMP
// errors. Overlapped calls are not supported.
inline int WSAIoctl(SOCKET Socket, DWORD Code, LPVOID In, DWORD InSize, LPVOID Out, DWORD OutSize,
                    LPDWORD Returned, LPWSAOVERLAPPED Overlapped,
                    LPWSAOVERLAPPED_COMPLETION_ROUTINE Completion)
{
    (void)Socket;
    (void)In;
    (void)InSize;
    (void)Out;
    (void)OutSize;
    if (Overlapped != nullptr || Completion != nullptr)
    {
        errno = EOPNOTSUPP;
        return SOCKET_ERROR;
    }
    if (Code == SIO_UDP_CONNRESET)
    {
        if (Returned != nullptr)
            *Returned = 0;
        return 0;
    }
    errno = EOPNOTSUPP;
    return SOCKET_ERROR;
}
