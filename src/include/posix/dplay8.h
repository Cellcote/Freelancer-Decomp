// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(macOS): a stand-in for the DirectX 8 SDK's <dplay8.h> on macOS and Linux
// (src/include/posix, non-Windows only): the DirectPlay 8 names DALib uses (dalib_net.h, the
// CDP* classes and DPNetAdapter.cpp), with the SDK's values and vtable order, so their code stays
// as it is.
//
// There is no Microsoft DirectPlay here. The server objects are DPNet's (the port's own DirectPlay
// 8, docs/dpnet.md), made by src/DALib/DPNetAdapter.cpp and compiled from this header like their
// callers, so the structure layouts only have to agree within the process. CoCreateInstance finds
// no DirectPlay class (posix/unknwn.h): the client path (CDPClient) is not ported yet.

#pragma once

#if defined(_WIN32)
#error "posix/dplay8.h is the stand-in for macOS and Linux; Windows uses the DirectX 8 SDK"
#endif

#include <windows.h>

#include "posix/dpaddr.h"

// ---------------------------------------------------------------------------------------------
// Ids
// ---------------------------------------------------------------------------------------------

// {743F1DC6-5ABA-429f-8BDF-C54D03253DC2}
inline constexpr CLSID CLSID_DirectPlay8Client = {
    0x743f1dc6, 0x5aba, 0x429f, {0x8b, 0xdf, 0xc5, 0x4d, 0x03, 0x25, 0x3d, 0xc2}};
// {DA825E1B-6830-43d7-835D-0B5AD82956A2}
inline constexpr CLSID CLSID_DirectPlay8Server = {
    0xda825e1b, 0x6830, 0x43d7, {0x83, 0x5d, 0x0b, 0x5a, 0xd8, 0x29, 0x56, 0xa2}};
// {286F484D-375E-4458-A272-B138E2F80A6A}
inline constexpr CLSID CLSID_DirectPlay8Peer = {
    0x286f484d, 0x375e, 0x4458, {0xa2, 0x72, 0xb1, 0x38, 0xe2, 0xf8, 0x0a, 0x6a}};

// {5102DACD-241B-11d3-AEA7-006097B01411}
inline constexpr IID IID_IDirectPlay8Client = {
    0x5102dacd, 0x241b, 0x11d3, {0xae, 0xa7, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};
// {5102DACE-241B-11d3-AEA7-006097B01411}
inline constexpr IID IID_IDirectPlay8Server = {
    0x5102dace, 0x241b, 0x11d3, {0xae, 0xa7, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};
// {5102DACF-241B-11d3-AEA7-006097B01411}
inline constexpr IID IID_IDirectPlay8Peer = {
    0x5102dacf, 0x241b, 0x11d3, {0xae, 0xa7, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};
// {819074A3-016C-11d3-AE14-006097B01411}
inline constexpr IID IID_IDirectPlay8LobbiedApplication = {
    0x819074a3, 0x016c, 0x11d3, {0xae, 0x14, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};

// The service providers.
// {EBFE7BA0-628D-11D2-AE0F-006097B01411}
inline constexpr GUID CLSID_DP8SP_TCPIP = {
    0xebfe7ba0, 0x628d, 0x11d2, {0xae, 0x0f, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};
// {53934290-628D-11D2-AE0F-006097B01411}
inline constexpr GUID CLSID_DP8SP_IPX = {
    0x53934290, 0x628d, 0x11d2, {0xae, 0x0f, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};
// {6D4A3650-628D-11D2-AE0F-006097B01411}
inline constexpr GUID CLSID_DP8SP_MODEM = {
    0x6d4a3650, 0x628d, 0x11d2, {0xae, 0x0f, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};
// {743B5D60-628D-11D2-AE0F-006097B01411}
inline constexpr GUID CLSID_DP8SP_SERIAL = {
    0x743b5d60, 0x628d, 0x11d2, {0xae, 0x0f, 0x00, 0x60, 0x97, 0xb0, 0x14, 0x11}};

// ---------------------------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------------------------

typedef DWORD DPNID;
typedef DPNID* PDPNID;
typedef DWORD DPNHANDLE;
typedef DPNHANDLE* PDPNHANDLE;

#define DPNID_ALL_PLAYERS_GROUP 0

// The application's message handler (IDirectPlay8*::Initialize).
typedef HRESULT(WINAPI* PFNDPNMESSAGEHANDLER)(PVOID UserContext, DWORD MessageId, PVOID Message);

// ---------------------------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------------------------

#define _DPN_FACILITY_CODE 0x015
#define _DPNHRESULT_BASE 0x8000
#define MAKE_DPNHRESULT(Code) MAKE_HRESULT(1, _DPN_FACILITY_CODE, ((Code) + _DPNHRESULT_BASE))

#define DPN_OK S_OK
#define DPNSUCCESS_EQUAL MAKE_HRESULT(0, _DPN_FACILITY_CODE, (0x5 + _DPNHRESULT_BASE))
#define DPNSUCCESS_NOTEQUAL MAKE_HRESULT(0, _DPN_FACILITY_CODE, (0x0A + _DPNHRESULT_BASE))
#define DPNSUCCESS_PENDING MAKE_HRESULT(0, _DPN_FACILITY_CODE, (0x0e + _DPNHRESULT_BASE))

#define DPNERR_ABORTED MAKE_DPNHRESULT(0x30)
#define DPNERR_ADDRESSING MAKE_DPNHRESULT(0x40)
#define DPNERR_ALREADYCLOSING MAKE_DPNHRESULT(0x50)
#define DPNERR_ALREADYCONNECTED MAKE_DPNHRESULT(0x60)
#define DPNERR_ALREADYDISCONNECTING MAKE_DPNHRESULT(0x70)
#define DPNERR_ALREADYINITIALIZED MAKE_DPNHRESULT(0x80)
#define DPNERR_ALREADYREGISTERED MAKE_DPNHRESULT(0x90)
#define DPNERR_BUFFERTOOSMALL MAKE_DPNHRESULT(0x100)
#define DPNERR_CANNOTCANCEL MAKE_DPNHRESULT(0x110)
#define DPNERR_CANTCREATEGROUP MAKE_DPNHRESULT(0x120)
#define DPNERR_CANTCREATEPLAYER MAKE_DPNHRESULT(0x130)
#define DPNERR_CANTLAUNCHAPPLICATION MAKE_DPNHRESULT(0x140)
#define DPNERR_CONNECTING MAKE_DPNHRESULT(0x150)
#define DPNERR_CONNECTIONLOST MAKE_DPNHRESULT(0x160)
#define DPNERR_CONVERSION MAKE_DPNHRESULT(0x170)
#define DPNERR_DATATOOLARGE MAKE_DPNHRESULT(0x175)
#define DPNERR_DOESNOTEXIST MAKE_DPNHRESULT(0x180)
#define DPNERR_DPNSVRNOTAVAILABLE MAKE_DPNHRESULT(0x185)
#define DPNERR_DUPLICATECOMMAND MAKE_DPNHRESULT(0x190)
#define DPNERR_ENDPOINTNOTRECEIVING MAKE_DPNHRESULT(0x200)
#define DPNERR_ENUMQUERYTOOLARGE MAKE_DPNHRESULT(0x210)
#define DPNERR_ENUMRESPONSETOOLARGE MAKE_DPNHRESULT(0x220)
#define DPNERR_EXCEPTION MAKE_DPNHRESULT(0x230)
#define DPNERR_GENERIC E_FAIL
#define DPNERR_GROUPNOTEMPTY MAKE_DPNHRESULT(0x240)
#define DPNERR_HOSTING MAKE_DPNHRESULT(0x250)
#define DPNERR_HOSTREJECTEDCONNECTION MAKE_DPNHRESULT(0x260)
#define DPNERR_HOSTTERMINATEDSESSION MAKE_DPNHRESULT(0x270)
#define DPNERR_INCOMPLETEADDRESS MAKE_DPNHRESULT(0x280)
#define DPNERR_INVALIDADDRESSFORMAT MAKE_DPNHRESULT(0x290)
#define DPNERR_INVALIDAPPLICATION MAKE_DPNHRESULT(0x300)
#define DPNERR_INVALIDCOMMAND MAKE_DPNHRESULT(0x310)
#define DPNERR_INVALIDDEVICEADDRESS MAKE_DPNHRESULT(0x320)
#define DPNERR_INVALIDENDPOINT MAKE_DPNHRESULT(0x330)
#define DPNERR_INVALIDFLAGS MAKE_DPNHRESULT(0x340)
#define DPNERR_INVALIDGROUP MAKE_DPNHRESULT(0x350)
#define DPNERR_INVALIDHANDLE MAKE_DPNHRESULT(0x360)
#define DPNERR_INVALIDHOSTADDRESS MAKE_DPNHRESULT(0x370)
#define DPNERR_INVALIDINSTANCE MAKE_DPNHRESULT(0x380)
#define DPNERR_INVALIDINTERFACE MAKE_DPNHRESULT(0x390)
#define DPNERR_INVALIDOBJECT MAKE_DPNHRESULT(0x400)
#define DPNERR_INVALIDPARAM E_INVALIDARG
#define DPNERR_INVALIDPASSWORD MAKE_DPNHRESULT(0x410)
#define DPNERR_INVALIDPLAYER MAKE_DPNHRESULT(0x420)
#define DPNERR_INVALIDPOINTER E_POINTER
#define DPNERR_INVALIDPRIORITY MAKE_DPNHRESULT(0x430)
#define DPNERR_INVALIDSTRING MAKE_DPNHRESULT(0x440)
#define DPNERR_INVALIDURL MAKE_DPNHRESULT(0x450)
#define DPNERR_INVALIDVERSION MAKE_DPNHRESULT(0x460)
#define DPNERR_NOCAPS MAKE_DPNHRESULT(0x470)
#define DPNERR_NOCONNECTION MAKE_DPNHRESULT(0x480)
#define DPNERR_NOHOSTPLAYER MAKE_DPNHRESULT(0x490)
#define DPNERR_NOINTERFACE E_NOINTERFACE
#define DPNERR_NOMOREADDRESSCOMPONENTS MAKE_DPNHRESULT(0x500)
#define DPNERR_NORESPONSE MAKE_DPNHRESULT(0x510)
#define DPNERR_NOTALLOWED MAKE_DPNHRESULT(0x520)
#define DPNERR_NOTHOST MAKE_DPNHRESULT(0x530)
#define DPNERR_NOTREADY MAKE_DPNHRESULT(0x540)
#define DPNERR_NOTREGISTERED MAKE_DPNHRESULT(0x550)
#define DPNERR_OUTOFMEMORY E_OUTOFMEMORY
#define DPNERR_PENDING DPNSUCCESS_PENDING
#define DPNERR_PLAYERALREADYINGROUP MAKE_DPNHRESULT(0x560)
#define DPNERR_PLAYERLOST MAKE_DPNHRESULT(0x570)
#define DPNERR_PLAYERNOTINGROUP MAKE_DPNHRESULT(0x580)
#define DPNERR_PLAYERNOTREACHABLE MAKE_DPNHRESULT(0x590)
#define DPNERR_SENDTOOLARGE MAKE_DPNHRESULT(0x600)
#define DPNERR_SESSIONFULL MAKE_DPNHRESULT(0x610)
#define DPNERR_TABLEFULL MAKE_DPNHRESULT(0x620)
#define DPNERR_TIMEDOUT MAKE_DPNHRESULT(0x630)
#define DPNERR_UNINITIALIZED MAKE_DPNHRESULT(0x640)
#define DPNERR_UNSUPPORTED E_NOTIMPL
#define DPNERR_USERCANCEL MAKE_DPNHRESULT(0x650)

// ---------------------------------------------------------------------------------------------
// Message ids
// ---------------------------------------------------------------------------------------------

#define DPN_MSGID_OFFSET 0xFFFF0000
#define DPN_MSGID_ADD_PLAYER_TO_GROUP (DPN_MSGID_OFFSET | 0x0001)
#define DPN_MSGID_APPLICATION_DESC (DPN_MSGID_OFFSET | 0x0002)
#define DPN_MSGID_ASYNC_OP_COMPLETE (DPN_MSGID_OFFSET | 0x0003)
#define DPN_MSGID_CLIENT_INFO (DPN_MSGID_OFFSET | 0x0004)
#define DPN_MSGID_CONNECT_COMPLETE (DPN_MSGID_OFFSET | 0x0005)
#define DPN_MSGID_CREATE_GROUP (DPN_MSGID_OFFSET | 0x0006)
#define DPN_MSGID_CREATE_PLAYER (DPN_MSGID_OFFSET | 0x0007)
#define DPN_MSGID_DESTROY_GROUP (DPN_MSGID_OFFSET | 0x0008)
#define DPN_MSGID_DESTROY_PLAYER (DPN_MSGID_OFFSET | 0x0009)
#define DPN_MSGID_ENUM_HOSTS_QUERY (DPN_MSGID_OFFSET | 0x000a)
#define DPN_MSGID_ENUM_HOSTS_RESPONSE (DPN_MSGID_OFFSET | 0x000b)
#define DPN_MSGID_GROUP_INFO (DPN_MSGID_OFFSET | 0x000c)
#define DPN_MSGID_HOST_MIGRATE (DPN_MSGID_OFFSET | 0x000d)
#define DPN_MSGID_INDICATE_CONNECT (DPN_MSGID_OFFSET | 0x000e)
#define DPN_MSGID_INDICATED_CONNECT_ABORTED (DPN_MSGID_OFFSET | 0x000f)
#define DPN_MSGID_PEER_INFO (DPN_MSGID_OFFSET | 0x0010)
#define DPN_MSGID_RECEIVE (DPN_MSGID_OFFSET | 0x0011)
#define DPN_MSGID_REMOVE_PLAYER_FROM_GROUP (DPN_MSGID_OFFSET | 0x0012)
#define DPN_MSGID_RETURN_BUFFER (DPN_MSGID_OFFSET | 0x0013)
#define DPN_MSGID_SEND_COMPLETE (DPN_MSGID_OFFSET | 0x0014)
#define DPN_MSGID_SERVER_INFO (DPN_MSGID_OFFSET | 0x0015)
#define DPN_MSGID_TERMINATE_SESSION (DPN_MSGID_OFFSET | 0x0016)

// ---------------------------------------------------------------------------------------------
// Flags
// ---------------------------------------------------------------------------------------------

#define DPNCANCEL_CONNECT 0x0001
#define DPNCANCEL_ENUM 0x0002
#define DPNCANCEL_SEND 0x0004
#define DPNCANCEL_ALL_OPERATIONS 0x8000

#define DPNCONNECT_SYNC 0x80000000
#define DPNCONNECT_OKTOQUERYFORADDRESSING 0x0001

#define DPNENUMHOSTS_SYNC 0x80000000
#define DPNENUMHOSTS_OKTOQUERYFORADDRESSING 0x0001
#define DPNENUMHOSTS_NOBROADCASTFALLBACK 0x0002

#define DPNHOST_OKTOQUERYFORADDRESSING 0x0001

#define DPNSEND_SYNC 0x80000000
#define DPNSEND_NOCOPY 0x0001
#define DPNSEND_NOCOMPLETE 0x0002
#define DPNSEND_COMPLETEONPROCESS 0x0004
#define DPNSEND_GUARANTEED 0x0008
#define DPNSEND_NONSEQUENTIAL 0x0010
#define DPNSEND_NOLOOPBACK 0x0020
#define DPNSEND_PRIORITY_LOW 0x0040
#define DPNSEND_PRIORITY_HIGH 0x0080

#define DPNSESSION_CLIENT_SERVER 0x0001
#define DPNSESSION_MIGRATE_HOST 0x0004
#define DPNSESSION_NODPNSVR 0x0040
#define DPNSESSION_REQUIREPASSWORD 0x0080

#define DPNGETSENDQUEUEINFO_PRIORITY_NORMAL 0x0001
#define DPNGETSENDQUEUEINFO_PRIORITY_HIGH 0x0002
#define DPNGETSENDQUEUEINFO_PRIORITY_LOW 0x0004

#define DPNINFO_NAME 0x0001
#define DPNINFO_DATA 0x0002

#define DPNPLAYER_LOCAL 0x0002
#define DPNPLAYER_HOST 0x0004

#define DPNDESTROYPLAYERREASON_NORMAL 0x0001
#define DPNDESTROYPLAYERREASON_CONNECTIONLOST 0x0002
#define DPNDESTROYPLAYERREASON_SESSIONTERMINATED 0x0003
#define DPNDESTROYPLAYERREASON_HOSTDESTROYEDPLAYER 0x0004

#define DPNSPCAPS_SUPPORTSDPNSRV 0x0001
#define DPNSPCAPS_SUPPORTSBROADCAST 0x0002
#define DPNSPCAPS_SUPPORTSALLADAPTERS 0x0004

// ---------------------------------------------------------------------------------------------
// Structures
// ---------------------------------------------------------------------------------------------

typedef struct _DPN_APPLICATION_DESC
{
    DWORD dwSize;
    DWORD dwFlags;
    GUID guidInstance;
    GUID guidApplication;
    DWORD dwMaxPlayers;
    DWORD dwCurrentPlayers;
    WCHAR* pwszSessionName;
    WCHAR* pwszPassword;
    PVOID pvReservedData;
    DWORD dwReservedDataSize;
    PVOID pvApplicationReservedData;
    DWORD dwApplicationReservedDataSize;
} DPN_APPLICATION_DESC, *PDPN_APPLICATION_DESC;

typedef struct _BUFFERDESC
{
    DWORD dwBufferSize;
    BYTE* pBufferData;
} BUFFERDESC, DPN_BUFFER_DESC, *PBUFFERDESC, *PDPN_BUFFER_DESC;

typedef struct _DPN_CAPS
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwConnectTimeout;
    DWORD dwConnectRetries;
    DWORD dwTimeoutUntilKeepAlive;
} DPN_CAPS, *PDPN_CAPS;

typedef struct _DPN_CONNECTION_INFO
{
    DWORD dwSize;
    DWORD dwRoundTripLatencyMS;
    DWORD dwThroughputBPS;
    DWORD dwPeakThroughputBPS;
    DWORD dwBytesSentGuaranteed;
    DWORD dwPacketsSentGuaranteed;
    DWORD dwBytesSentNonGuaranteed;
    DWORD dwPacketsSentNonGuaranteed;
    DWORD dwBytesRetried;
    DWORD dwPacketsRetried;
    DWORD dwBytesDropped;
    DWORD dwPacketsDropped;
    DWORD dwMessagesTransmittedHighPriority;
    DWORD dwMessagesTimedOutHighPriority;
    DWORD dwMessagesTransmittedNormalPriority;
    DWORD dwMessagesTimedOutNormalPriority;
    DWORD dwMessagesTransmittedLowPriority;
    DWORD dwMessagesTimedOutLowPriority;
    DWORD dwBytesReceivedGuaranteed;
    DWORD dwPacketsReceivedGuaranteed;
    DWORD dwBytesReceivedNonGuaranteed;
    DWORD dwPacketsReceivedNonGuaranteed;
    DWORD dwMessagesReceived;
} DPN_CONNECTION_INFO, *PDPN_CONNECTION_INFO;
// All DWORDs: the SDK's 0x5c bytes on every platform (CDPServer::GetConnectionStats sets it).
static_assert(sizeof(DPN_CONNECTION_INFO) == 0x5c, "DPN_CONNECTION_INFO is 23 DWORDs");

typedef struct _DPN_SP_CAPS
{
    DWORD dwSize;
    DWORD dwFlags;
    DWORD dwNumThreads;
    DWORD dwDefaultEnumCount;
    DWORD dwDefaultEnumRetryInterval;
    DWORD dwDefaultEnumTimeout;
    DWORD dwMaxEnumPayloadSize;
    DWORD dwBuffersPerThread;
    DWORD dwSystemBufferSize;
} DPN_SP_CAPS, *PDPN_SP_CAPS;

typedef struct _DPN_PLAYER_INFO
{
    DWORD dwSize;
    DWORD dwInfoFlags;
    PWSTR pwszName;
    PVOID pvData;
    DWORD dwDataSize;
    DWORD dwPlayerFlags;
} DPN_PLAYER_INFO, *PDPN_PLAYER_INFO;

typedef struct _DPN_GROUP_INFO
{
    DWORD dwSize;
    DWORD dwInfoFlags;
    PWSTR pwszName;
    PVOID pvData;
    DWORD dwDataSize;
    DWORD dwGroupFlags;
} DPN_GROUP_INFO, *PDPN_GROUP_INFO;

typedef struct _DPN_SERVICE_PROVIDER_INFO
{
    DWORD dwFlags;
    GUID guid;
    WCHAR* pwszName;
    PVOID pvReserved;
    DWORD dwReserved;
} DPN_SERVICE_PROVIDER_INFO, *PDPN_SERVICE_PROVIDER_INFO;

typedef struct _DPN_SECURITY_DESC
{
    DWORD dwSize;
    DWORD dwFlags;
    PVOID pvSecurityDescriptor; // PSECURITY_DESCRIPTOR
    DWORD dwReserved;
} DPN_SECURITY_DESC, *PDPN_SECURITY_DESC;

typedef struct _DPN_SECURITY_CREDENTIALS
{
    DWORD dwSize;
    DWORD dwFlags;
    PVOID pvCredentials;
    DWORD dwReserved;
} DPN_SECURITY_CREDENTIALS, *PDPN_SECURITY_CREDENTIALS;

// ---------------------------------------------------------------------------------------------
// Messages (the MsgData of each DPN_MSGID_)
// ---------------------------------------------------------------------------------------------

typedef struct _DPNMSG_ASYNC_OP_COMPLETE
{
    DWORD dwSize;
    DPNHANDLE hAsyncOp;
    PVOID pvUserContext;
    HRESULT hResultCode;
} DPNMSG_ASYNC_OP_COMPLETE, *PDPNMSG_ASYNC_OP_COMPLETE;

typedef struct _DPNMSG_CONNECT_COMPLETE
{
    DWORD dwSize;
    DPNHANDLE hAsyncOp;
    PVOID pvUserContext;
    HRESULT hResultCode;
    PVOID pvApplicationReplyData;
    DWORD dwApplicationReplyDataSize;
} DPNMSG_CONNECT_COMPLETE, *PDPNMSG_CONNECT_COMPLETE;

typedef struct _DPNMSG_CREATE_PLAYER
{
    DWORD dwSize;
    DPNID dpnidPlayer;
    PVOID pvPlayerContext;
} DPNMSG_CREATE_PLAYER, *PDPNMSG_CREATE_PLAYER;

typedef struct _DPNMSG_DESTROY_PLAYER
{
    DWORD dwSize;
    DPNID dpnidPlayer;
    PVOID pvPlayerContext;
    DWORD dwReason;
} DPNMSG_DESTROY_PLAYER, *PDPNMSG_DESTROY_PLAYER;

typedef struct _DPNMSG_ENUM_HOSTS_QUERY
{
    DWORD dwSize;
    IDirectPlay8Address* pAddressSender;
    IDirectPlay8Address* pAddressDevice;
    PVOID pvReceivedData;
    DWORD dwReceivedDataSize;
    DWORD dwMaxResponseDataSize;
    PVOID pvResponseData;
    DWORD dwResponseDataSize;
    PVOID pvResponseContext;
} DPNMSG_ENUM_HOSTS_QUERY, *PDPNMSG_ENUM_HOSTS_QUERY;

typedef struct _DPNMSG_ENUM_HOSTS_RESPONSE
{
    DWORD dwSize;
    IDirectPlay8Address* pAddressSender;
    IDirectPlay8Address* pAddressDevice;
    const DPN_APPLICATION_DESC* pApplicationDescription;
    PVOID pvResponseData;
    DWORD dwResponseDataSize;
    PVOID pvUserContext;
    DWORD dwRoundTripLatencyMS;
} DPNMSG_ENUM_HOSTS_RESPONSE, *PDPNMSG_ENUM_HOSTS_RESPONSE;

typedef struct _DPNMSG_INDICATE_CONNECT
{
    DWORD dwSize;
    PVOID pvUserConnectData;
    DWORD dwUserConnectDataSize;
    PVOID pvReplyData;
    DWORD dwReplyDataSize;
    PVOID pvReplyContext;
    PVOID pvPlayerContext;
    IDirectPlay8Address* pAddressPlayer;
    IDirectPlay8Address* pAddressDevice;
} DPNMSG_INDICATE_CONNECT, *PDPNMSG_INDICATE_CONNECT;

typedef struct _DPNMSG_INDICATED_CONNECT_ABORTED
{
    DWORD dwSize;
    PVOID pvPlayerContext;
} DPNMSG_INDICATED_CONNECT_ABORTED, *PDPNMSG_INDICATED_CONNECT_ABORTED;

typedef struct _DPNMSG_RECEIVE
{
    DWORD dwSize;
    DPNID dpnidSender;
    PVOID pvPlayerContext;
    PBYTE pReceiveData;
    DWORD dwReceiveDataSize;
    DPNHANDLE hBufferHandle;
} DPNMSG_RECEIVE, *PDPNMSG_RECEIVE;

typedef struct _DPNMSG_RETURN_BUFFER
{
    DWORD dwSize;
    HRESULT hResultCode;
    PVOID pvBuffer;
    PVOID pvUserContext;
} DPNMSG_RETURN_BUFFER, *PDPNMSG_RETURN_BUFFER;

typedef struct _DPNMSG_SEND_COMPLETE
{
    DWORD dwSize;
    DPNHANDLE hAsyncOp;
    PVOID pvUserContext;
    HRESULT hResultCode;
    DWORD dwSendTime;
} DPNMSG_SEND_COMPLETE, *PDPNMSG_SEND_COMPLETE;

typedef struct _DPNMSG_TERMINATE_SESSION
{
    DWORD dwSize;
    HRESULT hResultCode;
    PVOID pvTerminateData;
    DWORD dwTerminateDataSize;
} DPNMSG_TERMINATE_SESSION, *PDPNMSG_TERMINATE_SESSION;

// ---------------------------------------------------------------------------------------------
// Interfaces
// ---------------------------------------------------------------------------------------------

// The lobby (dplobby8.h): named by RegisterLobby only. Its structures are not declared here.
struct DPL_PROGRAM_DESC;
struct DPL_CONNECTION_SETTINGS;
struct IDirectPlay8LobbiedApplication : public IUnknown
{
    STDMETHOD(Initialize)(PVOID UserContext, PFNDPNMESSAGEHANDLER Handler, DPNHANDLE* Connection,
                          DWORD Flags) PURE;
    STDMETHOD(RegisterProgram)(DPL_PROGRAM_DESC* Program, DWORD Flags) PURE;
    STDMETHOD(UnRegisterProgram)(GUID* Application, DWORD Flags) PURE;
    STDMETHOD(Send)(DPNHANDLE Target, BYTE* Message, DWORD Size, DWORD Flags) PURE;
    STDMETHOD(SetAppAvailable)(BOOL Available, DWORD Flags) PURE;
    STDMETHOD(UpdateStatus)(DPNHANDLE Lobby, DWORD Status, DWORD Flags) PURE;
    STDMETHOD(Close)(DWORD Flags) PURE;
    STDMETHOD(GetConnectionSettings)(DPNHANDLE Lobby, DPL_CONNECTION_SETTINGS* Settings,
                                     DWORD* Size, DWORD Flags) PURE;
    STDMETHOD(SetConnectionSettings)(DPNHANDLE Lobby, const DPL_CONNECTION_SETTINGS* Settings,
                                     DWORD Flags) PURE;
};

struct IDirectPlay8Client : public IUnknown
{
    STDMETHOD(Initialize)(PVOID UserContext, PFNDPNMESSAGEHANDLER Handler, DWORD Flags) PURE;
    STDMETHOD(EnumServiceProviders)(const GUID* ServiceProvider, const GUID* Application,
                                    DPN_SERVICE_PROVIDER_INFO* Buffer, PDWORD BufferSize,
                                    PDWORD Returned, DWORD Flags) PURE;
    STDMETHOD(EnumHosts)(PDPN_APPLICATION_DESC ApplicationDesc, IDirectPlay8Address* Host,
                         IDirectPlay8Address* Device, PVOID UserEnumData, DWORD UserEnumDataSize,
                         DWORD EnumCount, DWORD RetryInterval, DWORD TimeOut, PVOID UserContext,
                         DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(CancelAsyncOperation)(DPNHANDLE AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(Connect)(const DPN_APPLICATION_DESC* ApplicationDesc, IDirectPlay8Address* Host,
                       IDirectPlay8Address* Device, const DPN_SECURITY_DESC* Security,
                       const DPN_SECURITY_CREDENTIALS* Credentials, const void* UserConnectData,
                       DWORD UserConnectDataSize, void* AsyncContext, DPNHANDLE* AsyncHandle,
                       DWORD Flags) PURE;
    STDMETHOD(Send)(const DPN_BUFFER_DESC* Buffers, DWORD NumBuffers, DWORD TimeOut,
                    void* AsyncContext, DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(GetSendQueueInfo)(DWORD* NumMsgs, DWORD* NumBytes, DWORD Flags) PURE;
    STDMETHOD(GetApplicationDesc)(DPN_APPLICATION_DESC* Buffer, DWORD* Size, DWORD Flags) PURE;
    STDMETHOD(SetClientInfo)(const DPN_PLAYER_INFO* Info, PVOID AsyncContext,
                             DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(GetServerInfo)(DPN_PLAYER_INFO* Info, DWORD* Size, DWORD Flags) PURE;
    STDMETHOD(GetServerAddress)(IDirectPlay8Address** Address, DWORD Flags) PURE;
    STDMETHOD(Close)(DWORD Flags) PURE;
    STDMETHOD(ReturnBuffer)(DPNHANDLE BufferHandle, DWORD Flags) PURE;
    STDMETHOD(GetCaps)(DPN_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(SetCaps)(const DPN_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(SetSPCaps)(const GUID* Sp, const DPN_SP_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(GetSPCaps)(const GUID* Sp, DPN_SP_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(GetConnectionInfo)(DPN_CONNECTION_INFO* Info, DWORD Flags) PURE;
    STDMETHOD(RegisterLobby)(DPNHANDLE Handle, IDirectPlay8LobbiedApplication* Lobby,
                             DWORD Flags) PURE;
};

struct IDirectPlay8Server : public IUnknown
{
    STDMETHOD(Initialize)(PVOID UserContext, PFNDPNMESSAGEHANDLER Handler, DWORD Flags) PURE;
    STDMETHOD(EnumServiceProviders)(const GUID* ServiceProvider, const GUID* Application,
                                    DPN_SERVICE_PROVIDER_INFO* Buffer, PDWORD BufferSize,
                                    PDWORD Returned, DWORD Flags) PURE;
    STDMETHOD(CancelAsyncOperation)(DPNHANDLE AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(GetSendQueueInfo)(DPNID Player, DWORD* NumMsgs, DWORD* NumBytes, DWORD Flags) PURE;
    STDMETHOD(GetApplicationDesc)(DPN_APPLICATION_DESC* Buffer, DWORD* Size, DWORD Flags) PURE;
    STDMETHOD(SetServerInfo)(const DPN_PLAYER_INFO* Info, PVOID AsyncContext,
                             DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(GetClientInfo)(DPNID Player, DPN_PLAYER_INFO* Info, DWORD* Size, DWORD Flags) PURE;
    STDMETHOD(GetClientAddress)(DPNID Player, IDirectPlay8Address** Address, DWORD Flags) PURE;
    STDMETHOD(GetLocalHostAddresses)(IDirectPlay8Address** Addresses, DWORD* Count,
                                     DWORD Flags) PURE;
    STDMETHOD(SetApplicationDesc)(const DPN_APPLICATION_DESC* ApplicationDesc, DWORD Flags) PURE;
    STDMETHOD(Host)(const DPN_APPLICATION_DESC* ApplicationDesc, IDirectPlay8Address** Devices,
                    DWORD NumDevices, const DPN_SECURITY_DESC* Security,
                    const DPN_SECURITY_CREDENTIALS* Credentials, void* PlayerContext,
                    DWORD Flags) PURE;
    STDMETHOD(SendTo)(DPNID Player, const DPN_BUFFER_DESC* Buffers, DWORD NumBuffers, DWORD TimeOut,
                      void* AsyncContext, DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(CreateGroup)(const DPN_GROUP_INFO* Info, void* GroupContext, void* AsyncContext,
                           DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(DestroyGroup)(DPNID Group, PVOID AsyncContext, DPNHANDLE* AsyncHandle,
                            DWORD Flags) PURE;
    STDMETHOD(AddPlayerToGroup)(DPNID Group, DPNID Client, PVOID AsyncContext,
                                DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(RemovePlayerFromGroup)(DPNID Group, DPNID Client, PVOID AsyncContext,
                                     DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(SetGroupInfo)(DPNID Group, DPN_GROUP_INFO* Info, PVOID AsyncContext,
                            DPNHANDLE* AsyncHandle, DWORD Flags) PURE;
    STDMETHOD(GetGroupInfo)(DPNID Group, DPN_GROUP_INFO* Info, DWORD* Size, DWORD Flags) PURE;
    STDMETHOD(EnumPlayersAndGroups)(DPNID* Ids, DWORD* Count, DWORD Flags) PURE;
    STDMETHOD(EnumGroupMembers)(DPNID Group, DPNID* Ids, DWORD* Count, DWORD Flags) PURE;
    STDMETHOD(Close)(DWORD Flags) PURE;
    STDMETHOD(DestroyClient)(DPNID Client, const void* DestroyData, DWORD DestroyDataSize,
                             DWORD Flags) PURE;
    STDMETHOD(ReturnBuffer)(DPNHANDLE BufferHandle, DWORD Flags) PURE;
    STDMETHOD(GetPlayerContext)(DPNID Player, PVOID* PlayerContext, DWORD Flags) PURE;
    STDMETHOD(GetGroupContext)(DPNID Group, PVOID* GroupContext, DWORD Flags) PURE;
    STDMETHOD(GetCaps)(DPN_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(SetCaps)(const DPN_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(SetSPCaps)(const GUID* Sp, const DPN_SP_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(GetSPCaps)(const GUID* Sp, DPN_SP_CAPS* Caps, DWORD Flags) PURE;
    STDMETHOD(GetConnectionInfo)(DPNID Player, DPN_CONNECTION_INFO* Info, DWORD Flags) PURE;
    STDMETHOD(RegisterLobby)(DPNHANDLE Handle, IDirectPlay8LobbiedApplication* Lobby,
                             DWORD Flags) PURE;
};

typedef IDirectPlay8Client* PDIRECTPLAY8CLIENT;
typedef IDirectPlay8Server* PDIRECTPLAY8SERVER;
