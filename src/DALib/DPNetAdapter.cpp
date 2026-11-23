// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(dpnet): DPNet behind DirectPlay 8's COM interfaces (DPNetAdapter.h, docs/dpnet.md).
//
// DPNet's FServer and FClient have no socket, clock or thread of their own. This file supplies
// them: a UDP socket bound like DirectPlay's TCP/IP service provider, a network thread that feeds
// received datagrams in and calls Tick every 10 ms, and GetTickCount as the clock. One lock per
// object guards its DPNet state. The application's message handler is called with the lock
// held, on whichever thread is inside DPNet (the network thread, or the caller of SendTo,
// DestroyClient, CancelAsyncOperation or Close). DALib's handlers only queue messages under their
// own locks (CDPClient's also reads GetConnectionInfo, which takes the same lock again), so that
// is safe for them. A handler on the network thread may still Close the object or drop the last
// reference: NetLoop then finishes the teardown once it unwinds.

#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

#include "DPNetAdapter.h"
#include "dpnet_session.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#pragma comment(lib, "ws2_32.lib")

namespace
{
    // DirectPlay's TCP/IP service provider takes the first free port of 2302..2400 when the
    // device address names none (DPNA_DPNSVR_PORT is 6073; the game range starts at 2302).
    constexpr unsigned short kFirstDefaultPort = 2302;
    constexpr unsigned short kLastDefaultPort = 2400;
    constexpr DWORD kTickMs = 10;
    // IDirectPlay8Server::Close waits for the players to be told and gone.
    constexpr DWORD kCloseWaitMs = 5000;
    // The largest reply an EnumQuery may get, as DirectPlay's TCP/IP SP reports it.
    constexpr DWORD kMaxEnumResponse = 1000;

    uint32_t NowMs()
    {
        return GetTickCount();
    }

    // DPNet's strings are UTF-16, as WCHAR is on Windows. On macOS and Linux WCHAR is the 32-bit
    // wchar_t: a character past U+FFFF becomes a surrogate pair.
    std::u16string ToU16(const WCHAR* Text)
    {
        std::u16string Out;
        if (Text == nullptr)
            return Out;
        for (; *Text != 0; ++Text)
        {
            const uint32_t Char = (uint32_t)*Text;
            if (Char > 0xFFFF && Char <= 0x10FFFF)
            {
                Out += (char16_t)(0xD800 + ((Char - 0x10000) >> 10));
                Out += (char16_t)(0xDC00 + ((Char - 0x10000) & 0x3FF));
            }
            else
            {
                Out += (char16_t)Char;
            }
        }
        return Out;
    }

    DPNet::FGuid ToGuid(const GUID& In)
    {
        DPNet::FGuid Out;
        Out.Data1 = In.Data1;
        Out.Data2 = In.Data2;
        Out.Data3 = In.Data3;
        std::memcpy(Out.Data4, In.Data4, sizeof(Out.Data4));
        return Out;
    }

    DPNet::FAppDesc ToAppDesc(const DPN_APPLICATION_DESC& In)
    {
        DPNet::FAppDesc Out;
        Out.Flags = In.dwFlags;
        Out.MaxPlayers = In.dwMaxPlayers;
        Out.SessionName = ToU16(In.pwszSessionName);
        Out.Password = ToU16(In.pwszPassword);
        Out.Application = ToGuid(In.guidApplication);
        Out.Instance = ToGuid(In.guidInstance);
        if (In.pvApplicationReservedData != nullptr && In.dwApplicationReservedDataSize != 0)
        {
            const BYTE* Data = static_cast<const BYTE*>(In.pvApplicationReservedData);
            Out.ApplicationReserved.assign(Data, Data + In.dwApplicationReservedDataSize);
        }
        return Out;
    }

    sockaddr_in ToSockaddr(const DPNet::FAddress& In)
    {
        sockaddr_in Out{};
        Out.sin_family = AF_INET;
        std::memcpy(&Out.sin_addr, In.Ip.data(), 4);
        Out.sin_port = htons(In.Port);
        return Out;
    }

    DPNet::FAddress FromSockaddr(const sockaddr_in& In)
    {
        DPNet::FAddress Out;
        Out.Family = 4;
        std::memcpy(Out.Ip.data(), &In.sin_addr, 4);
        Out.Port = ntohs(In.sin_port);
        return Out;
    }

    // A UDP socket, closed when it goes.
    class FUdpSocket
    {
    public:
        FUdpSocket() = default;
        explicit FUdpSocket(SOCKET InHandle) : Handle(InHandle) {}
        FUdpSocket(FUdpSocket&& Other) noexcept : Handle(Other.Handle)
        {
            Other.Handle = INVALID_SOCKET;
        }
        FUdpSocket& operator=(FUdpSocket&& Other) noexcept
        {
            std::swap(Handle, Other.Handle);
            return *this;
        }
        ~FUdpSocket()
        {
            if (Handle != INVALID_SOCKET)
                closesocket(Handle);
        }

        SOCKET Get() const { return Handle; }
        explicit operator bool() const { return Handle != INVALID_SOCKET; }

    private:
        SOCKET Handle = INVALID_SOCKET;
    };

    // WSAStartup, undone by WSACleanup when it goes.
    struct FWinsockSession
    {
        FWinsockSession()
        {
            WSADATA WsaData;
            bStarted = WSAStartup(MAKEWORD(2, 2), &WsaData) == 0;
        }
        ~FWinsockSession()
        {
            if (bStarted)
                WSACleanup();
        }
        FWinsockSession(const FWinsockSession&) = delete;
        FWinsockSession& operator=(const FWinsockSession&) = delete;

        bool bStarted = false;
    };

    // Bound like DirectPlay's TCP/IP SP: all interfaces, the given port, else the first free
    // one of 2302..2400. Bound: the port it took.
    FUdpSocket OpenSocket(unsigned short Port, unsigned short& Bound)
    {
        const unsigned short First = Port != 0 ? Port : kFirstDefaultPort;
        const unsigned short Last = Port != 0 ? Port : kLastDefaultPort;
        for (unsigned int Candidate = First; Candidate <= Last; ++Candidate)
        {
            FUdpSocket Created(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
            if (!Created)
                return FUdpSocket();
#if !defined(_WIN32)
            // PORT(macOS): NetLoop waits with select, whose fd_set holds only descriptors below
            // FD_SETSIZE on POSIX (a Windows fd_set is a list of handles, with no such limit).
            if (Created.Get() >= FD_SETSIZE)
                return FUdpSocket();
#endif
#if defined(_WIN32)
            BOOL Exclusive = TRUE;
            setsockopt(Created.Get(), SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                       reinterpret_cast<const char*>(&Exclusive), sizeof(Exclusive));
#endif // (macOS and Linux bind exclusively unless SO_REUSEADDR / SO_REUSEPORT is set)
            sockaddr_in Local{};
            Local.sin_family = AF_INET;
            Local.sin_addr.s_addr = htonl(INADDR_ANY);
            Local.sin_port = htons((unsigned short)Candidate);
            if (bind(Created.Get(), reinterpret_cast<const sockaddr*>(&Local), sizeof(Local)) == 0)
            {
                // An ICMP port-unreachable from a vanished client must not fail the next
                // recvfrom (Windows reports it as WSAECONNRESET on a UDP socket).
                BOOL Report = FALSE;
                DWORD Returned = 0;
                WSAIoctl(Created.Get(), SIO_UDP_CONNRESET, &Report, sizeof(Report), nullptr, 0,
                         &Returned, nullptr, nullptr);
                u_long NonBlocking = 1;
                ioctlsocket(Created.Get(), FIONBIO, &NonBlocking);
                Bound = (unsigned short)Candidate;
                return Created;
            }
        }
        return FUdpSocket();
    }

    // ---------------------------------------------------------------------------------------
    // IDirectPlay8Address: a service provider, a device and named components, as
    // dpnet.dll keeps them. Enough for DALib: SetSP, AddComponent, GetComponentByName, GetURLA.

    class FAddressObject final : public IDirectPlay8Address
    {
    public:
        // IUnknown
        STDMETHOD(QueryInterface)(REFIID Iid, void** Out) override
        {
            if (Out == nullptr)
                return E_POINTER;
            if (Iid == IID_IUnknown || Iid == IID_IDirectPlay8Address)
            {
                AddRef();
                *Out = static_cast<IDirectPlay8Address*>(this);
                return S_OK;
            }
            *Out = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&Refs); }
        STDMETHOD_(ULONG, Release)() override
        {
            const LONG Left = InterlockedDecrement(&Refs);
            if (Left == 0)
                delete this;
            return (ULONG)Left;
        }

        // IDirectPlay8Address
        STDMETHOD(BuildFromURLW)(WCHAR* Url) override
        {
            if (Url == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::string Narrow;
            for (; *Url != 0; ++Url)
            {
                // A DirectPlay URL is ASCII; anything else is escaped (%XX) in it.
                if ((uint32_t)*Url > 0x7F)
                    return DPNERR_INVALIDURL;
                Narrow += (char)*Url;
            }
            return BuildFromUrl(Narrow);
        }
        STDMETHOD(BuildFromURLA)(CHAR* Url) override
        {
            if (Url == nullptr)
                return DPNERR_INVALIDPOINTER;
            return BuildFromUrl(Url);
        }
        STDMETHOD(Duplicate)(PDIRECTPLAY8ADDRESS* Out) override
        {
            if (Out == nullptr)
                return DPNERR_INVALIDPOINTER;
            FAddressObject* Copy = new FAddressObject(*this);
            Copy->Refs = 1;
            *Out = Copy;
            return S_OK;
        }
        STDMETHOD(SetEqual)(PDIRECTPLAY8ADDRESS) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(IsEqual)(PDIRECTPLAY8ADDRESS) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(Clear)() override
        {
            bHasSp = false;
            bHasDevice = false;
            Components.clear();
            UserData.clear();
            return S_OK;
        }
        STDMETHOD(GetURLW)(WCHAR* Url, PDWORD NumChars) override
        {
            const std::string Text = BuildUrl();
            if (NumChars == nullptr)
                return DPNERR_INVALIDPOINTER;
            const DWORD Need = (DWORD)Text.size() + 1;
            if (Url == nullptr || *NumChars < Need)
            {
                *NumChars = Need;
                return DPNERR_BUFFERTOOSMALL;
            }
            for (size_t Index = 0; Index < Text.size(); ++Index)
                Url[Index] = (WCHAR)(unsigned char)Text[Index];
            Url[Text.size()] = 0;
            *NumChars = Need;
            return S_OK;
        }
        STDMETHOD(GetURLA)(CHAR* Url, PDWORD NumChars) override
        {
            const std::string Text = BuildUrl();
            if (NumChars == nullptr)
                return DPNERR_INVALIDPOINTER;
            const DWORD Need = (DWORD)Text.size() + 1;
            if (Url == nullptr || *NumChars < Need)
            {
                *NumChars = Need;
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Url, Text.c_str(), Need);
            *NumChars = Need;
            return S_OK;
        }
        STDMETHOD(GetSP)(GUID* Sp) override
        {
            if (Sp == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (!bHasSp)
                return DPNERR_DOESNOTEXIST;
            *Sp = SpGuid;
            return S_OK;
        }
        STDMETHOD(GetUserData)(LPVOID Buffer, PDWORD Size) override
        {
            if (Size == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (UserData.empty())
                return DPNERR_DOESNOTEXIST;
            if (Buffer == nullptr || *Size < UserData.size())
            {
                *Size = (DWORD)UserData.size();
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Buffer, UserData.data(), UserData.size());
            *Size = (DWORD)UserData.size();
            return S_OK;
        }
        STDMETHOD(SetSP)(const GUID* Sp) override
        {
            if (Sp == nullptr)
                return DPNERR_INVALIDPOINTER;
            SpGuid = *Sp;
            bHasSp = true;
            return S_OK;
        }
        STDMETHOD(SetUserData)(const void* Data, DWORD Size) override
        {
            const BYTE* Bytes = static_cast<const BYTE*>(Data);
            if (Bytes == nullptr || Size == 0)
                UserData.clear();
            else
                UserData.assign(Bytes, Bytes + Size);
            return S_OK;
        }
        STDMETHOD(GetNumComponents)(PDWORD Count) override
        {
            if (Count == nullptr)
                return DPNERR_INVALIDPOINTER;
            *Count = (DWORD)Components.size();
            return S_OK;
        }
        STDMETHOD(GetComponentByName)(const WCHAR* Name, LPVOID Buffer, PDWORD Size, PDWORD Type) override
        {
            if (Name == nullptr || Size == nullptr || Type == nullptr)
                return DPNERR_INVALIDPOINTER;
            const FComponent* Found = Find(Name);
            if (Found == nullptr)
                return DPNERR_DOESNOTEXIST;
            *Type = Found->Type;
            if (Buffer == nullptr || *Size < Found->Data.size())
            {
                *Size = (DWORD)Found->Data.size();
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Buffer, Found->Data.data(), Found->Data.size());
            *Size = (DWORD)Found->Data.size();
            return S_OK;
        }
        STDMETHOD(GetComponentByIndex)(DWORD Index, WCHAR* Name, PDWORD NameLen, void* Buffer, PDWORD Size,
                                       PDWORD Type) override
        {
            if (NameLen == nullptr || Size == nullptr || Type == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (Index >= Components.size())
                return DPNERR_DOESNOTEXIST;
            const FComponent& Found = Components[Index];
            const DWORD NeedName = (DWORD)Found.Name.size() + 1;
            if (Name == nullptr || *NameLen < NeedName || Buffer == nullptr || *Size < Found.Data.size())
            {
                *NameLen = NeedName;
                *Size = (DWORD)Found.Data.size();
                return DPNERR_BUFFERTOOSMALL;
            }
            std::memcpy(Name, Found.Name.c_str(), NeedName * sizeof(WCHAR));
            std::memcpy(Buffer, Found.Data.data(), Found.Data.size());
            *NameLen = NeedName;
            *Size = (DWORD)Found.Data.size();
            *Type = Found.Type;
            return S_OK;
        }
        STDMETHOD(AddComponent)(const WCHAR* Name, const void* Data, DWORD Size, DWORD Type) override
        {
            if (Name == nullptr || (Data == nullptr && Size != 0))
                return DPNERR_INVALIDPOINTER;
            const BYTE* Bytes = static_cast<const BYTE*>(Data);
            FComponent* Existing = Find(Name);
            if (Existing == nullptr)
            {
                Components.push_back(FComponent());
                Existing = &Components.back();
                Existing->Name = Name;
            }
            Existing->Type = Type;
            Existing->Data.assign(Bytes, Bytes + Size);
            return S_OK;
        }
        STDMETHOD(GetDevice)(GUID* Device) override
        {
            if (Device == nullptr)
                return DPNERR_INVALIDPOINTER;
            if (!bHasDevice)
                return DPNERR_DOESNOTEXIST;
            *Device = DeviceGuid;
            return S_OK;
        }
        STDMETHOD(SetDevice)(const GUID* Device) override
        {
            if (Device == nullptr)
                return DPNERR_INVALIDPOINTER;
            DeviceGuid = *Device;
            bHasDevice = true;
            return S_OK;
        }
        // BuildFromDirectPlay4Address in later SDKs.
        STDMETHOD(BuildFromDPADDRESS)(LPVOID, DWORD) override { return DPNERR_UNSUPPORTED; }

        // The hostname and port components of a UDP endpoint, on the TCP/IP service provider.
        static FAddressObject* ForEndpoint(const DPNet::FAddress& Endpoint)
        {
            FAddressObject* Address = new FAddressObject();
            Address->SetSP(&CLSID_DP8SP_TCPIP);
            const std::string Host = Endpoint.HostText();
            const std::wstring Wide(Host.begin(), Host.end());
            Address->AddComponent(DPNA_KEY_HOSTNAME, Wide.c_str(), (DWORD)((Wide.size() + 1) * sizeof(WCHAR)),
                                  DPNA_DATATYPE_STRING);
            const DWORD Port = Endpoint.Port;
            Address->AddComponent(DPNA_KEY_PORT, &Port, sizeof(Port), DPNA_DATATYPE_DWORD);
            return Address;
        }

        // A string component (DPNA_DATATYPE_STRING or _ANSI), as ASCII.
        bool GetText(const WCHAR* Name, std::string& Out) const
        {
            const FComponent* Found = const_cast<FAddressObject*>(this)->Find(Name);
            if (Found == nullptr)
                return false;
            Out.clear();
            if (Found->Type == DPNA_DATATYPE_STRING)
            {
                const WCHAR* Text = reinterpret_cast<const WCHAR*>(Found->Data.data());
                for (size_t Index = 0;
                     Index < Found->Data.size() / sizeof(WCHAR) && Text[Index] != 0; ++Index)
                    Out += (char)Text[Index];
                return true;
            }
            if (Found->Type == DPNA_DATATYPE_STRING_ANSI)
            {
                Out.assign(reinterpret_cast<const char*>(Found->Data.data()), Found->Data.size());
                Out = Out.c_str();
                return true;
            }
            return false;
        }

        bool GetDword(const WCHAR* Name, DWORD& Out) const
        {
            const FComponent* Found = const_cast<FAddressObject*>(this)->Find(Name);
            if (Found == nullptr || Found->Type != DPNA_DATATYPE_DWORD || Found->Data.size() != sizeof(DWORD))
                return false;
            std::memcpy(&Out, Found->Data.data(), sizeof(DWORD));
            return true;
        }

    private:
        struct FComponent
        {
            std::wstring Name;
            DWORD Type = 0;
            std::vector<BYTE> Data;
        };

        FComponent* Find(const WCHAR* Name)
        {
            for (FComponent& Component : Components)
            {
                if (_wcsicmp(Component.Name.c_str(), Name) == 0)
                    return &Component;
            }
            return nullptr;
        }

        // "x-directplay:/key=value;key=value", values %XX-escaped, as dpnet.dll writes them
        // (GetURLA): provider and device are GUIDs, digits a DWORD, {...} a GUID, else a string.
        HRESULT BuildFromUrl(const std::string& Url)
        {
            static const char kHeader[] = "x-directplay:/";
            const size_t HeaderSize = sizeof(kHeader) - 1;
            if (Url.size() < HeaderSize || _strnicmp(Url.c_str(), kHeader, HeaderSize) != 0)
                return DPNERR_INVALIDURL;
            FAddressObject Parsed;
            size_t Start = HeaderSize;
            while (Start < Url.size())
            {
                size_t End = Url.find(';', Start);
                if (End == std::string::npos)
                    End = Url.size();
                const std::string Pair = Url.substr(Start, End - Start);
                Start = End + 1;
                if (Pair.empty())
                    continue;
                const size_t Equals = Pair.find('=');
                if (Equals == std::string::npos || Equals == 0)
                    return DPNERR_INVALIDURL;
                std::string Value;
                if (!Unescape(Pair.substr(Equals + 1), Value))
                    return DPNERR_INVALIDURL;
                const std::wstring Key(Pair.begin(), Pair.begin() + (std::ptrdiff_t)Equals);
                GUID Guid{};
                const bool bGuid = ParseGuid(Value, Guid);
                if (_wcsicmp(Key.c_str(), DPNA_KEY_PROVIDER) == 0 ||
                    _wcsicmp(Key.c_str(), DPNA_KEY_DEVICE) == 0)
                {
                    if (!bGuid)
                        return DPNERR_INVALIDURL;
                    if (_wcsicmp(Key.c_str(), DPNA_KEY_PROVIDER) == 0)
                        Parsed.SetSP(&Guid);
                    else
                        Parsed.SetDevice(&Guid);
                }
                else if (bGuid)
                {
                    Parsed.AddComponent(Key.c_str(), &Guid, sizeof(Guid), DPNA_DATATYPE_GUID);
                }
                else if (!Value.empty() && Value.size() <= 10 &&
                         Value.find_first_not_of("0123456789") == std::string::npos &&
                         std::stoull(Value) <= 0xFFFFFFFFull)
                {
                    const DWORD Number = (DWORD)std::stoull(Value);
                    Parsed.AddComponent(Key.c_str(), &Number, sizeof(Number), DPNA_DATATYPE_DWORD);
                }
                else
                {
                    const std::wstring Wide(Value.begin(), Value.end());
                    Parsed.AddComponent(Key.c_str(), Wide.c_str(),
                                        (DWORD)((Wide.size() + 1) * sizeof(WCHAR)),
                                        DPNA_DATATYPE_STRING);
                }
            }
            bHasSp = Parsed.bHasSp;
            SpGuid = Parsed.SpGuid;
            bHasDevice = Parsed.bHasDevice;
            DeviceGuid = Parsed.DeviceGuid;
            Components = std::move(Parsed.Components);
            UserData.clear();
            return S_OK;
        }

        static bool Unescape(const std::string& In, std::string& Out)
        {
            Out.clear();
            for (size_t Index = 0; Index < In.size(); ++Index)
            {
                if (In[Index] != '%')
                {
                    Out += In[Index];
                    continue;
                }
                if (Index + 2 >= In.size() || !std::isxdigit((unsigned char)In[Index + 1]) ||
                    !std::isxdigit((unsigned char)In[Index + 2]))
                    return false;
                Out += (char)std::stoi(In.substr(Index + 1, 2), nullptr, 16);
                Index += 2;
            }
            return true;
        }

        // "{XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX}", as GuidText writes it (unescaped).
        static bool ParseGuid(const std::string& Text, GUID& Out)
        {
            if (Text.size() != 38 || Text.front() != '{' || Text.back() != '}' || Text[9] != '-' ||
                Text[14] != '-' || Text[19] != '-' || Text[24] != '-')
                return false;
            std::string Hex;
            for (size_t Index = 1; Index < 37; ++Index)
            {
                if (Text[Index] == '-')
                    continue;
                if (!std::isxdigit((unsigned char)Text[Index]))
                    return false;
                Hex += Text[Index];
            }
            auto Field = [&](size_t Offset, size_t Digits)
            { return (unsigned long)std::stoul(Hex.substr(Offset, Digits), nullptr, 16); };
            Out.Data1 = (decltype(Out.Data1))Field(0, 8);
            Out.Data2 = (unsigned short)Field(8, 4);
            Out.Data3 = (unsigned short)Field(12, 4);
            for (size_t Index = 0; Index < 8; ++Index)
                Out.Data4[Index] = (unsigned char)Field(16 + Index * 2, 2);
            return true;
        }

        static std::string GuidText(const GUID& Guid)
        {
            char Text[64];
            std::snprintf(Text, sizeof(Text),
                          "%%7B%08lX-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X%%7D",
                          (unsigned long)Guid.Data1, Guid.Data2, Guid.Data3, Guid.Data4[0],
                          Guid.Data4[1], Guid.Data4[2], Guid.Data4[3], Guid.Data4[4], Guid.Data4[5],
                          Guid.Data4[6], Guid.Data4[7]);
            return Text;
        }

        // DPNA_HEADER, then provider, device and the components as key=value pairs.
        std::string BuildUrl() const
        {
            std::string Url = "x-directplay:/";
            bool bFirst = true;
            auto Append = [&](const std::string& Key, const std::string& Value) {
                if (!bFirst)
                    Url += ';';
                bFirst = false;
                Url += Key + "=" + Value;
            };
            if (bHasSp)
                Append("provider", GuidText(SpGuid));
            if (bHasDevice)
                Append("device", GuidText(DeviceGuid));
            for (const FComponent& Component : Components)
            {
                const std::string Key(Component.Name.begin(), Component.Name.end());
                std::string Value;
                if (Component.Type == DPNA_DATATYPE_DWORD && Component.Data.size() == sizeof(DWORD))
                {
                    DWORD Number = 0;
                    std::memcpy(&Number, Component.Data.data(), sizeof(Number));
                    Value = std::to_string(Number);
                }
                else if (Component.Type == DPNA_DATATYPE_STRING)
                {
                    const WCHAR* Text = reinterpret_cast<const WCHAR*>(Component.Data.data());
                    for (size_t Index = 0; Index < Component.Data.size() / sizeof(WCHAR) && Text[Index] != 0; ++Index)
                        Value += (char)Text[Index];
                }
                else if (Component.Type == DPNA_DATATYPE_STRING_ANSI)
                {
                    Value.assign(reinterpret_cast<const char*>(Component.Data.data()), Component.Data.size());
                    Value = Value.c_str();
                }
                else if (Component.Type == DPNA_DATATYPE_GUID && Component.Data.size() == sizeof(GUID))
                {
                    GUID Guid;
                    std::memcpy(&Guid, Component.Data.data(), sizeof(Guid));
                    Value = GuidText(Guid);
                }
                Append(Key, Value);
            }
            return Url;
        }

        LONG Refs = 1;
        bool bHasSp = false;
        GUID SpGuid{};
        bool bHasDevice = false;
        GUID DeviceGuid{};
        std::vector<FComponent> Components;
        std::vector<BYTE> UserData;
    };

    // Releases the reference it holds.
    struct FAddressRelease
    {
        void operator()(FAddressObject* Address) const { Address->Release(); }
    };
    using FAddressRef = std::unique_ptr<FAddressObject, FAddressRelease>;

    // ---------------------------------------------------------------------------------------
    // IDirectPlay8Server on DPNet::FServer.

    class FServerObject final : public IDirectPlay8Server, private DPNet::ITransport, private DPNet::IServerEvents
    {
    public:
        FServerObject() = default;
        ~FServerObject() { Shutdown(); }

        // IUnknown
        STDMETHOD(QueryInterface)(REFIID Iid, void** Out) override
        {
            if (Out == nullptr)
                return E_POINTER;
            if (Iid == IID_IUnknown || Iid == IID_IDirectPlay8Server)
            {
                AddRef();
                *Out = static_cast<IDirectPlay8Server*>(this);
                return S_OK;
            }
            *Out = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&Refs); }
        STDMETHOD_(ULONG, Release)() override
        {
            const LONG Left = InterlockedDecrement(&Refs);
            if (Left == 0)
            {
                // Dropped by a message handler on the network thread: NetLoop is still inside this
                // object, and deletes it once it unwinds.
                if (OnNetThread())
                {
                    Close(0);
                    bDeleteOnExit = true;
                }
                else
                {
                    delete this;
                }
            }
            return (ULONG)Left;
        }

        // IDirectPlay8Server
        STDMETHOD(Initialize)(PVOID UserContext, PFNDPNMESSAGEHANDLER Handler, DWORD) override
        {
            if (Handler == nullptr)
                return DPNERR_INVALIDPARAM;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler != nullptr)
                return DPNERR_ALREADYINITIALIZED;
            // A Close still under way: from a message handler, until NetLoop has torn down; from
            // another thread, until that Close returns (its last step is a teardown, which would
            // clear this session).
            if (Server != nullptr || ClosesUnderWay != 0)
                return DPNERR_ALREADYCLOSING;
            Winsock.emplace();
            if (!Winsock->bStarted)
            {
                Winsock.reset();
                return DPNERR_GENERIC;
            }
            MessageHandler = Handler;
            HandlerContext = UserContext;
            return S_OK;
        }
        STDMETHOD(EnumServiceProviders)(const GUID*, const GUID*, DPN_SERVICE_PROVIDER_INFO*, PDWORD, PDWORD,
                                        DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(CancelAsyncOperation)(DPNHANDLE, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetSendQueueInfo)(DPNID Player, DWORD* NumMsgs, DWORD* NumBytes, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            uint32_t Messages = 0;
            uint32_t Bytes = 0;
            if (Server == nullptr || !Server->GetSendQueueInfo(Player, Messages, Bytes))
                return DPNERR_INVALIDPLAYER;
            if (NumMsgs != nullptr)
                *NumMsgs = Messages;
            if (NumBytes != nullptr)
                *NumBytes = Bytes;
            return S_OK;
        }
        STDMETHOD(GetApplicationDesc)(DPN_APPLICATION_DESC*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(SetServerInfo)(const DPN_PLAYER_INFO*, PVOID, DPNHANDLE*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(GetClientInfo)(DPNID, DPN_PLAYER_INFO*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetClientAddress)(DPNID Player, IDirectPlay8Address** Address, DWORD) override
        {
            if (Address == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            DPNet::FAddress Endpoint;
            if (Server == nullptr || !Server->GetPlayerAddress(Player, Endpoint))
                return DPNERR_INVALIDPLAYER;
            *Address = FAddressObject::ForEndpoint(Endpoint);
            return S_OK;
        }
        STDMETHOD(GetLocalHostAddresses)(IDirectPlay8Address** Addresses, DWORD* Count, DWORD) override
        {
            if (Count == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Server == nullptr || !Server->IsHosting())
                return DPNERR_NOCONNECTION;
            if (Addresses == nullptr || *Count < 1)
            {
                *Count = 1;
                return DPNERR_BUFFERTOOSMALL;
            }
            Addresses[0] = FAddressObject::ForEndpoint(LocalEndpoint());
            *Count = 1;
            return S_OK;
        }
        STDMETHOD(SetApplicationDesc)(const DPN_APPLICATION_DESC* Desc, DWORD) override
        {
            if (Desc == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler == nullptr)
                return DPNERR_UNINITIALIZED;
            if (Server != nullptr && Server->IsHosting())
                Server->SetApplicationDesc(ToAppDesc(*Desc));
            return S_OK;
        }
        STDMETHOD(Host)(const DPN_APPLICATION_DESC* Desc, IDirectPlay8Address** Devices, DWORD NumDevices,
                        const DPN_SECURITY_DESC*, const DPN_SECURITY_CREDENTIALS*, void*, DWORD) override
        {
            if (Desc == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler == nullptr)
                return DPNERR_UNINITIALIZED;
            if (Server != nullptr)
                return DPNERR_ALREADYCONNECTED;

            // The device address may name a port; FAddressObject is the only kind there is.
            DWORD Port = 0;
            if (NumDevices != 0 && Devices != nullptr && Devices[0] != nullptr)
                static_cast<FAddressObject*>(Devices[0])->GetDword(DPNA_KEY_PORT, Port);
            unsigned short Bound = 0;
            FUdpSocket Opened = OpenSocket((unsigned short)Port, Bound);
            if (!Opened)
                return DPNERR_INVALIDDEVICEADDRESS;
            Socket = std::move(Opened);
            BoundPort = Bound;
            // Once per session: a host-name lookup can block, and every enumeration query and
            // connect names the device address. A failed lookup is retried (LocalEndpoint).
            LocalTriedAt = NowMs();
            bLocalResolved = ResolveLocalEndpoint(Local);

            LARGE_INTEGER Counter;
            QueryPerformanceCounter(&Counter);
            const uint64_t Seed = (uint64_t)Counter.QuadPart ^ ((uint64_t)GetCurrentProcessId() << 32);
            // The callback bases are private; convert here, where they are accessible.
            DPNet::ITransport& Transport = *this;
            DPNet::IServerEvents& Events = *this;
            Server = std::make_unique<DPNet::FServer>(Transport, Events);
            if (!Server->Host(ToAppDesc(*Desc), NowMs(), Seed))
            {
                Server.reset();
                CloseSocket();
                return DPNERR_GENERIC;
            }
            // A loop that a Close from a message handler stopped has torn down and returned, but
            // nobody joined it.
            if (NetThread.joinable())
                NetThread.join();
            bStop = false;
            NetThread = std::thread([this] { NetLoop(); });
            return S_OK;
        }
        STDMETHOD(SendTo)(DPNID Player, const DPN_BUFFER_DESC* Buffers, DWORD NumBuffers, DWORD, void* AsyncContext,
                          DPNHANDLE* AsyncHandle, DWORD Flags) override
        {
            if (Buffers == nullptr && NumBuffers != 0)
                return DPNERR_INVALIDPOINTER;
            std::vector<uint8_t> Data;
            for (DWORD Index = 0; Index < NumBuffers; ++Index)
                Data.insert(Data.end(), Buffers[Index].pBufferData, Buffers[Index].pBufferData + Buffers[Index].dwBufferSize);
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Server == nullptr || !Server->IsHosting())
                return DPNERR_NOCONNECTION;
            uint64_t Handle = 0;
            if (!Server->SendTo(Player, Data.data(), Data.size(), Flags, AsyncContext, &Handle, NowMs()))
                return DPNERR_INVALIDPLAYER;
            if (AsyncHandle != nullptr)
                *AsyncHandle = (DPNHANDLE)Handle;
            return (Flags & DPNSEND_NOCOMPLETE) != 0 ? S_OK : DPNSUCCESS_PENDING;
        }
        STDMETHOD(CreateGroup)(const DPN_GROUP_INFO*, void*, void*, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(DestroyGroup)(DPNID, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(AddPlayerToGroup)(DPNID, DPNID, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(RemovePlayerFromGroup)(DPNID, DPNID, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(SetGroupInfo)(DPNID, DPN_GROUP_INFO*, PVOID, DPNHANDLE*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetGroupInfo)(DPNID, DPN_GROUP_INFO*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(EnumPlayersAndGroups)(DPNID*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(EnumGroupMembers)(DPNID, DPNID*, DWORD*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(Close)(DWORD) override
        {
            {
                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (MessageHandler == nullptr)
                    return DPNERR_UNINITIALIZED;
                if (Server != nullptr && Server->IsHosting())
                    Server->Close(NowMs());
                // From a message handler on the network thread, NetLoop is inside FServer below
                // us: it can neither wait for the players nor free FServer. The handler is called
                // no more, and NetLoop tears down once it unwinds.
                if (OnNetThread())
                {
                    MessageHandler = nullptr;
                    HandlerContext = nullptr;
                    bTeardownOnExit = true;
                    bStop = true;
                    return S_OK;
                }
                ++ClosesUnderWay;
            }
            // The network thread carries the goodbyes out; wait for the last player to go.
            const DWORD Start = GetTickCount();
            for (;;)
            {
                {
                    std::lock_guard<std::recursive_mutex> Guard(Lock);
                    if (Server == nullptr || Server->IsIdle() || !NetThread.joinable())
                        break;
                }
                if (GetTickCount() - Start > kCloseWaitMs)
                    break;
                Sleep(kTickMs);
            }
            StopThread();
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            Teardown();
            --ClosesUnderWay;
            return S_OK;
        }
        STDMETHOD(DestroyClient)(DPNID Player, const void* Data, DWORD Size, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Server == nullptr || !Server->IsHosting())
                return DPNERR_NOCONNECTION;
            return Server->DestroyClient(Player, static_cast<const uint8_t*>(Data), Size, NowMs()) ? S_OK
                                                                                                     : DPNERR_INVALIDPLAYER;
        }
        STDMETHOD(ReturnBuffer)(DPNHANDLE Handle, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            return ReceiveBuffers.erase(Handle) != 0 ? S_OK : DPNERR_INVALIDHANDLE;
        }
        STDMETHOD(GetPlayerContext)(DPNID, PVOID*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetGroupContext)(DPNID, PVOID*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetCaps)(DPN_CAPS* Caps, DWORD) override
        {
            if (Caps == nullptr)
                return DPNERR_INVALIDPOINTER;
            const DWORD Size = Caps->dwSize;
            std::memset(Caps, 0, sizeof(*Caps));
            Caps->dwSize = Size;
            return S_OK;
        }
        // DPNet keeps its own timeouts and retry limits (docs/dpnet.md).
        STDMETHOD(SetCaps)(const DPN_CAPS*, DWORD) override { return S_OK; }
        STDMETHOD(SetSPCaps)(const GUID*, const DPN_SP_CAPS*, DWORD) override { return S_OK; }
        STDMETHOD(GetSPCaps)(const GUID*, DPN_SP_CAPS*, DWORD) override { return DPNERR_UNSUPPORTED; }
        STDMETHOD(GetConnectionInfo)(DPNID Player, DPN_CONNECTION_INFO* Info, DWORD) override
        {
            if (Info == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            const DPNet::FLinkStats* Stats = Server != nullptr ? Server->GetLinkStats(Player) : nullptr;
            if (Stats == nullptr)
                return DPNERR_INVALIDPLAYER;
            const DWORD Size = Info->dwSize;
            std::memset(Info, 0, sizeof(*Info));
            Info->dwSize = Size;
            Info->dwRoundTripLatencyMS = Stats->RttMs;
            Info->dwBytesSentGuaranteed = (DWORD)Stats->BytesSent;
            Info->dwPacketsSentGuaranteed = (DWORD)Stats->FramesSent;
            Info->dwBytesReceivedGuaranteed = (DWORD)Stats->BytesReceived;
            Info->dwPacketsReceivedGuaranteed = (DWORD)Stats->FramesReceived;
            Info->dwPacketsRetried = (DWORD)Stats->FramesRetried;
            Info->dwPacketsDropped = (DWORD)Stats->FramesDropped;
            Info->dwMessagesTransmittedNormalPriority = (DWORD)Stats->MessagesSent;
            Info->dwMessagesTimedOutNormalPriority = (DWORD)Stats->MessagesTimedOut;
            Info->dwMessagesReceived = (DWORD)Stats->MessagesReceived;
            return S_OK;
        }
        STDMETHOD(RegisterLobby)(DPNHANDLE, struct IDirectPlay8LobbiedApplication*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }

    private:
        // ITransport
        void SendTo(const DPNet::FAddress& To, const uint8_t* Data, size_t Size) override
        {
            if (!Socket || To.Family != 4)
                return;
            const sockaddr_in Target = ToSockaddr(To);
            sendto(Socket.Get(), reinterpret_cast<const char*>(Data), (int)Size, 0,
                   reinterpret_cast<const sockaddr*>(&Target), sizeof(Target));
        }

        // IServerEvents: each becomes the DPN_MSGID_ message dpnet.dll would send.
        HRESULT Notify(DWORD MessageId, void* Message)
        {
            return MessageHandler != nullptr ? MessageHandler(HandlerContext, MessageId, Message) : S_OK;
        }

        void ReturnAppBuffer(void* Buffer, void* Context)
        {
            if (Buffer == nullptr && Context == nullptr)
                return;
            DPNMSG_RETURN_BUFFER Message{};
            Message.dwSize = sizeof(Message);
            Message.hResultCode = S_OK;
            Message.pvBuffer = Buffer;
            Message.pvUserContext = Context;
            Notify(DPN_MSGID_RETURN_BUFFER, &Message);
        }

        bool ServerEnumQuery(const DPNet::FAddress& From, const DPNet::FBytes& QueryData, DPNet::FBytes& Response) override
        {
            const FAddressRef Sender(FAddressObject::ForEndpoint(From));
            const FAddressRef Device(FAddressObject::ForEndpoint(LocalEndpoint()));
            DPNMSG_ENUM_HOSTS_QUERY Message{};
            Message.dwSize = sizeof(Message);
            Message.pAddressSender = Sender.get();
            Message.pAddressDevice = Device.get();
            Message.pvReceivedData = QueryData.empty() ? nullptr : const_cast<uint8_t*>(QueryData.data());
            Message.dwReceivedDataSize = (DWORD)QueryData.size();
            Message.dwMaxResponseDataSize = kMaxEnumResponse;
            const HRESULT Result = Notify(DPN_MSGID_ENUM_HOSTS_QUERY, &Message);
            if (FAILED(Result))
                return false;
            const BYTE* Reply = static_cast<const BYTE*>(Message.pvResponseData);
            if (Reply != nullptr && Message.dwResponseDataSize != 0)
                Response.assign(Reply, Reply + Message.dwResponseDataSize);
            ReturnAppBuffer(Message.pvResponseData, Message.pvResponseContext);
            return true;
        }

        bool ServerIndicateConnect(const DPNet::FAddress& From, const DPNet::FPlayerConnectInfo& Info, void*& PlayerContext,
                                   DPNet::FBytes& Reply) override
        {
            const FAddressRef Player(FAddressObject::ForEndpoint(From));
            const FAddressRef Device(FAddressObject::ForEndpoint(LocalEndpoint()));
            DPNMSG_INDICATE_CONNECT Message{};
            Message.dwSize = sizeof(Message);
            Message.pvUserConnectData = Info.ConnectData.empty() ? nullptr : const_cast<uint8_t*>(Info.ConnectData.data());
            Message.dwUserConnectDataSize = (DWORD)Info.ConnectData.size();
            Message.pAddressPlayer = Player.get();
            Message.pAddressDevice = Device.get();
            const HRESULT Result = Notify(DPN_MSGID_INDICATE_CONNECT, &Message);
            PlayerContext = Message.pvPlayerContext;
            const BYTE* Data = static_cast<const BYTE*>(Message.pvReplyData);
            if (Data != nullptr && Message.dwReplyDataSize != 0)
                Reply.assign(Data, Data + Message.dwReplyDataSize);
            ReturnAppBuffer(Message.pvReplyData, Message.pvReplyContext);
            return SUCCEEDED(Result);
        }

        void ServerIndicatedConnectAborted(void* PlayerContext) override
        {
            DPNMSG_INDICATED_CONNECT_ABORTED Message{};
            Message.dwSize = sizeof(Message);
            Message.pvPlayerContext = PlayerContext;
            Notify(DPN_MSGID_INDICATED_CONNECT_ABORTED, &Message);
        }

        void ServerCreatePlayer(uint32_t Dpnid, void* PlayerContext) override
        {
            DPNMSG_CREATE_PLAYER Message{};
            Message.dwSize = sizeof(Message);
            Message.dpnidPlayer = Dpnid;
            Message.pvPlayerContext = PlayerContext;
            Notify(DPN_MSGID_CREATE_PLAYER, &Message);
        }

        void ServerDestroyPlayer(uint32_t Dpnid, void* PlayerContext, uint32_t Reason) override
        {
            DPNMSG_DESTROY_PLAYER Message{};
            Message.dwSize = sizeof(Message);
            Message.dpnidPlayer = Dpnid;
            Message.pvPlayerContext = PlayerContext;
            Message.dwReason = Reason;
            Notify(DPN_MSGID_DESTROY_PLAYER, &Message);
        }

        // The data stays ours until the application returns it: DPNSUCCESS_PENDING from the
        // handler, then ReturnBuffer with the handle.
        void ServerReceive(uint32_t Dpnid, void* PlayerContext, DPNet::FBytes&& Data) override
        {
            DPNHANDLE Handle = NextBufferHandle++;
            if (Handle == 0)
                Handle = NextBufferHandle++;
            DPNet::FBytes& Stored = ReceiveBuffers[Handle];
            Stored = std::move(Data);
            DPNMSG_RECEIVE Message{};
            Message.dwSize = sizeof(Message);
            Message.dpnidSender = Dpnid;
            Message.pvPlayerContext = PlayerContext;
            Message.pReceiveData = Stored.data();
            Message.dwReceiveDataSize = (DWORD)Stored.size();
            Message.hBufferHandle = Handle;
            if (Notify(DPN_MSGID_RECEIVE, &Message) != DPNSUCCESS_PENDING)
                ReceiveBuffers.erase(Handle);
        }

        void ServerSendComplete(uint64_t Handle, void* UserContext, bool bOk, uint32_t SendTimeMs) override
        {
            DPNMSG_SEND_COMPLETE Message{};
            Message.dwSize = sizeof(Message);
            Message.hAsyncOp = (DPNHANDLE)Handle;
            Message.pvUserContext = UserContext;
            Message.hResultCode = bOk ? S_OK : DPNERR_CONNECTIONLOST;
            Message.dwSendTime = SendTimeMs;
            Notify(DPN_MSGID_SEND_COMPLETE, &Message);
        }

        // --- socket and thread ---

        void CloseSocket()
        {
            Socket = FUdpSocket();
            BoundPort = 0;
        }

        // The address the clients reach, resolved when the session started. Until a lookup
        // succeeds it is loopback, and the lookup is tried again at most every 10 s, so a
        // failing resolver cannot stall every query.
        DPNet::FAddress LocalEndpoint()
        {
            if (!bLocalResolved && NowMs() - LocalTriedAt >= kLocalRetryMs)
            {
                LocalTriedAt = NowMs();
                bLocalResolved = ResolveLocalEndpoint(Local);
            }
            return Local;
        }

        // Out: the first IPv4 address of this host, else loopback (and false).
        bool ResolveLocalEndpoint(DPNet::FAddress& Out) const
        {
            Out = DPNet::FAddress::Ipv4(127, 0, 0, 1, BoundPort);
            char HostName[256] = {};
            if (gethostname(HostName, sizeof(HostName)) != 0)
                return false;
            addrinfo Hints{};
            Hints.ai_family = AF_INET;
            addrinfo* Found = nullptr;
            if (getaddrinfo(HostName, nullptr, &Hints, &Found) != 0 || Found == nullptr)
                return false;
            Out = FromSockaddr(*reinterpret_cast<const sockaddr_in*>(Found->ai_addr));
            Out.Port = BoundPort;
            freeaddrinfo(Found);
            return true;
        }

        void NetLoop()
        {
            std::vector<char> Buffer(64 * 1024);
            while (!bStop)
            {
                fd_set Readable;
                FD_ZERO(&Readable);
                FD_SET(Socket.Get(), &Readable);
                timeval Timeout{0, (FlLong)(kTickMs * 1000)};
                // The first argument is the highest descriptor + 1 (Windows ignores it).
                select((int)Socket.Get() + 1, &Readable, nullptr, nullptr, &Timeout);

                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (Server == nullptr)
                    break;
                for (;;)
                {
                    sockaddr_in From{};
                    socklen_t FromSize = sizeof(From);
                    const int Got = recvfrom(Socket.Get(), Buffer.data(), (int)Buffer.size(), 0,
                                             reinterpret_cast<sockaddr*>(&From), &FromSize);
                    if (Got < 0)
                        break;   // WSAEWOULDBLOCK: nothing more queued
                    if (From.sin_family != AF_INET)
                        continue;
                    Server->OnDatagram(FromSockaddr(From), reinterpret_cast<const uint8_t*>(Buffer.data()),
                                       (size_t)Got, NowMs());
                }
                Server->Tick(NowMs());
            }

            // Close stopped the loop. If it ran inside a handler on this thread, it left the rest
            // to us. A Close from another thread tears down itself once it has joined us: doing it
            // here would let an Initialize in between succeed, only for that Close to clear it.
            bool bDelete = false;
            {
                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (bTeardownOnExit)
                {
                    Teardown();
                    bTeardownOnExit = false;
                }
                bDelete = bDeleteOnExit;
            }
            if (bDelete)
            {
                NetThread.detach();
                delete this;
            }
        }

        // Under Lock.
        void Teardown()
        {
            Server.reset();
            CloseSocket();
            // As in dpnet.dll, a closed object must be initialized again before it is used.
            MessageHandler = nullptr;
            HandlerContext = nullptr;
            Winsock.reset();
        }

        bool OnNetThread() const
        {
            return NetThread.joinable() && NetThread.get_id() == std::this_thread::get_id();
        }

        void StopThread()
        {
            // Never on the network thread: Close and Release leave that case to NetLoop.
            bStop = true;
            if (NetThread.joinable())
                NetThread.join();
        }

        void Shutdown()
        {
            if (MessageHandler != nullptr)
                Close(0);
            StopThread();
        }

        LONG Refs = 1;
        std::recursive_mutex Lock;
        PFNDPNMESSAGEHANDLER MessageHandler = nullptr;
        void* HandlerContext = nullptr;
        std::optional<FWinsockSession> Winsock;
        std::unique_ptr<DPNet::FServer> Server;
        FUdpSocket Socket;
        unsigned short BoundPort = 0;
        static constexpr uint32_t kLocalRetryMs = 10000;
        DPNet::FAddress Local;
        bool bLocalResolved = false;
        uint32_t LocalTriedAt = 0;
        std::thread NetThread;
        std::atomic<bool> bStop{false};
        int ClosesUnderWay = 0; // Closes from other threads that have not returned yet
        bool bTeardownOnExit = false; // a Close on the network thread left the teardown to NetLoop
        bool bDeleteOnExit = false;
        std::map<DPNHANDLE, DPNet::FBytes> ReceiveBuffers;
        DPNHANDLE NextBufferHandle = 1;
    };

    // ---------------------------------------------------------------------------------------
    // IDirectPlay8Client on DPNet::FClient.
    //
    // Two FClients share one socket: one sends the enumeration queries and reads their answers
    // (datagrams whose first byte is 0), the other holds the connection; a fresh one for every
    // Connect, as an FClient connects once. The messages, as dpnet.dll sends them to a client:
    // ENUM_HOSTS_RESPONSE per answer and ASYNC_OP_COMPLETE when an enumeration ends,
    // CONNECT_COMPLETE once per Connect, RECEIVE, SEND_COMPLETE, and TERMINATE_SESSION when an
    // established session ends other than by our own Close.

    // Strings from the wire (UTF-16) as WCHAR: a surrogate pair becomes one character where
    // WCHAR is the 32-bit wchar_t (macOS, Linux).
    std::basic_string<WCHAR> FromU16(const std::u16string& Text)
    {
        std::basic_string<WCHAR> Out;
        for (size_t Index = 0; Index < Text.size(); ++Index)
        {
            const uint32_t Char = Text[Index];
            if (sizeof(WCHAR) == 4 && Char >= 0xD800 && Char < 0xDC00 && Index + 1 < Text.size() &&
                Text[Index + 1] >= 0xDC00 && Text[Index + 1] < 0xE000)
            {
                Out += (WCHAR)(0x10000 + ((Char - 0xD800) << 10) + (Text[Index + 1] - 0xDC00));
                ++Index;
            }
            else
            {
                Out += (WCHAR)Char;
            }
        }
        return Out;
    }

    GUID FromGuid(const DPNet::FGuid& In)
    {
        GUID Out{};
        Out.Data1 = In.Data1;
        Out.Data2 = In.Data2;
        Out.Data3 = In.Data3;
        std::memcpy(Out.Data4, In.Data4, sizeof(Out.Data4));
        return Out;
    }

    bool IsZero(const GUID& Guid)
    {
        static const GUID Zero{};
        return std::memcmp(&Guid, &Zero, sizeof(Guid)) == 0;
    }

    // A host name or dotted address, IPv4 only (as DPNet's socket is).
    bool ResolveHost(const std::string& Host, uint16_t Port, DPNet::FAddress& Out)
    {
        in_addr Numeric{};
        if (inet_pton(AF_INET, Host.c_str(), &Numeric) == 1)
        {
            sockaddr_in Address{};
            Address.sin_family = AF_INET;
            Address.sin_addr = Numeric;
            Out = FromSockaddr(Address);
            Out.Port = Port;
            return true;
        }
        addrinfo Hints{};
        Hints.ai_family = AF_INET;
        Hints.ai_socktype = SOCK_DGRAM;
        addrinfo* Found = nullptr;
        if (getaddrinfo(Host.c_str(), nullptr, &Hints, &Found) != 0 || Found == nullptr)
            return false;
        Out = FromSockaddr(*reinterpret_cast<const sockaddr_in*>(Found->ai_addr));
        Out.Port = Port;
        freeaddrinfo(Found);
        return true;
    }

    class FClientObject final : public IDirectPlay8Client,
                                private DPNet::ITransport,
                                private DPNet::IClientEvents
    {
    public:
        FClientObject() = default;
        ~FClientObject() { Shutdown(); }

        // IUnknown
        STDMETHOD(QueryInterface)(REFIID Iid, void** Out) override
        {
            if (Out == nullptr)
                return E_POINTER;
            if (Iid == IID_IUnknown || Iid == IID_IDirectPlay8Client)
            {
                AddRef();
                *Out = static_cast<IDirectPlay8Client*>(this);
                return S_OK;
            }
            *Out = nullptr;
            return E_NOINTERFACE;
        }
        STDMETHOD_(ULONG, AddRef)() override { return (ULONG)InterlockedIncrement(&Refs); }
        STDMETHOD_(ULONG, Release)() override
        {
            const LONG Left = InterlockedDecrement(&Refs);
            if (Left == 0)
            {
                // As FServerObject: dropped by a handler on the network thread, NetLoop deletes it.
                if (OnNetThread())
                {
                    Close(0);
                    bDeleteOnExit = true;
                }
                else
                {
                    delete this;
                }
            }
            return (ULONG)Left;
        }

        // IDirectPlay8Client
        STDMETHOD(Initialize)(PVOID UserContext, PFNDPNMESSAGEHANDLER Handler, DWORD) override
        {
            if (Handler == nullptr)
                return DPNERR_INVALIDPARAM;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler != nullptr)
                return DPNERR_ALREADYINITIALIZED;
            if (EnumSession != nullptr || ClosesUnderWay != 0)
                return DPNERR_ALREADYCLOSING;
            Winsock.emplace();
            if (!Winsock->bStarted)
            {
                Winsock.reset();
                return DPNERR_GENERIC;
            }
            MessageHandler = Handler;
            HandlerContext = UserContext;
            return S_OK;
        }
        STDMETHOD(EnumServiceProviders)(const GUID*, const GUID*, DPN_SERVICE_PROVIDER_INFO*,
                                        PDWORD, PDWORD, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(EnumHosts)(PDPN_APPLICATION_DESC Desc, IDirectPlay8Address* Host,
                             IDirectPlay8Address* Device, PVOID UserEnumData,
                             DWORD UserEnumDataSize, DWORD EnumCount, DWORD RetryInterval,
                             DWORD TimeOut, PVOID UserContext, DPNHANDLE* AsyncHandle,
                             DWORD Flags) override
        {
            if (Desc == nullptr || Device == nullptr || AsyncHandle == nullptr ||
                (UserEnumData == nullptr && UserEnumDataSize != 0))
                return DPNERR_INVALIDPOINTER;
            if ((Flags & DPNENUMHOSTS_SYNC) != 0)
                return DPNERR_UNSUPPORTED;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler == nullptr || bMuted)
                return DPNERR_UNINITIALIZED;

            // The host address names the host and maybe its port. Without a port the query goes
            // to DPNSVR's port, as DirectPlay sends it, and to the first game port, where a DPNet
            // host (which has no DPNSVR) listens by default. Without a host it is broadcast.
            FAddressObject* HostAddress = static_cast<FAddressObject*>(Host);
            std::string HostName;
            DWORD Port = 0;
            if (HostAddress != nullptr)
            {
                HostAddress->GetText(DPNA_KEY_HOSTNAME, HostName);
                HostAddress->GetDword(DPNA_KEY_PORT, Port);
            }
            if (Port > 0xFFFF)
                return DPNERR_INVALIDHOSTADDRESS;
            std::vector<uint16_t> Ports;
            if (Port != 0)
                Ports.push_back((uint16_t)Port);
            else
                Ports = {(uint16_t)DPNA_DPNSVR_PORT, kFirstDefaultPort};
            FEnumOp Op;
            for (const uint16_t Target : Ports)
            {
                DPNet::FAddress Address = DPNet::FAddress::Ipv4(255, 255, 255, 255, Target);
                if (!HostName.empty() && !ResolveHost(HostName, Target, Address))
                    return DPNERR_INVALIDHOSTADDRESS;
                if (HostName.empty() && (Flags & DPNENUMHOSTS_NOBROADCASTFALLBACK) != 0)
                    return DPNERR_ADDRESSING;
                Op.Targets.push_back(Address);
            }
            if (!StartNetwork(Device))
                return DPNERR_INVALIDDEVICEADDRESS;

            Op.Handle = NextAsyncHandle();
            Op.UserContext = UserContext;
            Op.bHasApplication = !IsZero(Desc->guidApplication);
            Op.Application = ToGuid(Desc->guidApplication);
            if (UserEnumData != nullptr)
            {
                const BYTE* Data = static_cast<const BYTE*>(UserEnumData);
                Op.UserData.assign(Data, Data + UserEnumDataSize);
            }
            // 0: the TCP/IP service provider's defaults (its DPN_SP_CAPS).
            Op.Left = EnumCount != 0 ? EnumCount : kDefaultEnumCount;
            Op.Interval = RetryInterval != 0 ? RetryInterval : kDefaultEnumInterval;
            Op.Timeout = TimeOut != 0 ? TimeOut : kDefaultEnumTimeout;
            Enums.push_back(std::move(Op));
            SendEnumQueries(Enums.back(), NowMs());
            *AsyncHandle = Enums.back().Handle;
            return DPNSUCCESS_PENDING;
        }
        STDMETHOD(CancelAsyncOperation)(DPNHANDLE Handle, DWORD Flags) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler == nullptr)
                return DPNERR_UNINITIALIZED;
            if (Handle != 0)
            {
                for (const FEnumOp& Op : Enums)
                {
                    if (Op.Handle == Handle)
                    {
                        FinishEnum(Handle, DPNERR_USERCANCEL);
                        return S_OK;
                    }
                }
                if (Phase == EPhase::Connecting && Handle == ConnectHandle)
                {
                    CancelConnect();
                    return S_OK;
                }
                return DPNERR_INVALIDHANDLE;
            }
            if ((Flags & (DPNCANCEL_ENUM | DPNCANCEL_ALL_OPERATIONS)) != 0)
                CancelEnums();
            if ((Flags & (DPNCANCEL_CONNECT | DPNCANCEL_ALL_OPERATIONS)) != 0 &&
                Phase == EPhase::Connecting)
                CancelConnect();
            // Sends are not cancelled: DPNet's link has no way to take a queued message back.
            // They complete, or fail when the connection goes.
            return S_OK;
        }
        STDMETHOD(Connect)(const DPN_APPLICATION_DESC* Desc, IDirectPlay8Address* Host,
                           IDirectPlay8Address* Device, const DPN_SECURITY_DESC*,
                           const DPN_SECURITY_CREDENTIALS*, const void* UserConnectData,
                           DWORD UserConnectDataSize, void* AsyncContext, DPNHANDLE* AsyncHandle,
                           DWORD Flags) override
        {
            if (Desc == nullptr || Host == nullptr || Device == nullptr ||
                (UserConnectData == nullptr && UserConnectDataSize != 0))
                return DPNERR_INVALIDPOINTER;
            if ((Flags & DPNCONNECT_SYNC) != 0)
                return DPNERR_UNSUPPORTED;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (MessageHandler == nullptr || bMuted)
                return DPNERR_UNINITIALIZED;
            if (Phase == EPhase::Connecting)
                return DPNERR_CONNECTING;
            if (Phase == EPhase::Connected)
                return DPNERR_ALREADYCONNECTED;

            FAddressObject* HostAddress = static_cast<FAddressObject*>(Host);
            std::string HostName;
            DWORD Port = 0;
            if (!HostAddress->GetText(DPNA_KEY_HOSTNAME, HostName) || HostName.empty())
                return DPNERR_INVALIDHOSTADDRESS;
            if (!HostAddress->GetDword(DPNA_KEY_PORT, Port))
                Port = kFirstDefaultPort;
            DPNet::FAddress Target;
            if (Port == 0 || Port > 0xFFFF || !ResolveHost(HostName, (uint16_t)Port, Target))
                return DPNERR_INVALIDHOSTADDRESS;
            if (!StartNetwork(Device))
                return DPNERR_INVALIDDEVICEADDRESS;

            DPNet::FConnectRequest Request;
            Request.Application = ToGuid(Desc->guidApplication);
            Request.Instance = ToGuid(Desc->guidInstance);
            if (Desc->pwszPassword != nullptr)
            {
                Request.bHasPassword = true;
                Request.Password = ToU16(Desc->pwszPassword);
            }
            if (UserConnectData != nullptr)
            {
                const BYTE* Data = static_cast<const BYTE*>(UserConnectData);
                Request.ConnectData.assign(Data, Data + UserConnectDataSize);
            }
            uint32_t SessId = 0;
            while (SessId == 0)
                SessId = (uint32_t)Random();
            DPNet::ITransport& Transport = *this;
            DPNet::IClientEvents& Events = *this;
            Connection = std::make_unique<DPNet::FClient>(Transport, Events);
            if (!Connection->Connect(Target, Request, SessId, NowMs()))
            {
                Connection.reset();
                return DPNERR_GENERIC;
            }
            Phase = EPhase::Connecting;
            ConnectHandle = NextAsyncHandle();
            ConnectContext = AsyncContext;
            if (AsyncHandle != nullptr)
                *AsyncHandle = ConnectHandle;
            return DPNSUCCESS_PENDING;
        }
        STDMETHOD(Send)(const DPN_BUFFER_DESC* Buffers, DWORD NumBuffers, DWORD, void* AsyncContext,
                        DPNHANDLE* AsyncHandle, DWORD Flags) override
        {
            if (Buffers == nullptr && NumBuffers != 0)
                return DPNERR_INVALIDPOINTER;
            std::vector<uint8_t> Data;
            for (DWORD Index = 0; Index < NumBuffers; ++Index)
                Data.insert(Data.end(), Buffers[Index].pBufferData,
                            Buffers[Index].pBufferData + Buffers[Index].dwBufferSize);
            if (Data.empty())
                return DPNERR_INVALIDPARAM;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Phase != EPhase::Connected || Connection == nullptr)
                return DPNERR_NOCONNECTION;
            uint64_t Handle = 0;
            if (!Connection->Send(Data.data(), Data.size(), Flags, AsyncContext, &Handle, NowMs()))
                return DPNERR_NOCONNECTION;
            if (AsyncHandle != nullptr)
                *AsyncHandle = (DPNHANDLE)Handle;
            return (Flags & DPNSEND_NOCOMPLETE) != 0 ? S_OK : DPNSUCCESS_PENDING;
        }
        STDMETHOD(GetSendQueueInfo)(DWORD* NumMsgs, DWORD* NumBytes, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            uint32_t Messages = 0;
            uint32_t Bytes = 0;
            if (Phase != EPhase::Connected || Connection == nullptr ||
                !Connection->GetSendQueueInfo(Messages, Bytes))
                return DPNERR_NOCONNECTION;
            if (NumMsgs != nullptr)
                *NumMsgs = Messages;
            if (NumBytes != nullptr)
                *NumBytes = Bytes;
            return S_OK;
        }
        STDMETHOD(GetApplicationDesc)(DPN_APPLICATION_DESC*, DWORD*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(SetClientInfo)(const DPN_PLAYER_INFO*, PVOID, DPNHANDLE*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(GetServerInfo)(DPN_PLAYER_INFO*, DWORD*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(GetServerAddress)(IDirectPlay8Address**, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(Close)(DWORD) override
        {
            bool bGoodbye = false;
            {
                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (MessageHandler == nullptr)
                    return DPNERR_UNINITIALIZED;
                // Outstanding enumerations and a pending connect complete as cancelled. Then the
                // handler hears nothing more: no TERMINATE_SESSION for our own Close.
                CancelEnums();
                if (Phase == EPhase::Connecting)
                    CancelConnect();
                bMuted = true;
                // Only an established connection is waited for; a refused or cancelled one is
                // already on its way out.
                bGoodbye = Connection != nullptr &&
                           Connection->GetState() == DPNet::FClient::EState::Connected;
                if (Connection != nullptr)
                    Connection->Close(NowMs());
                // From a message handler on the network thread, as FServerObject::Close.
                if (OnNetThread())
                {
                    MessageHandler = nullptr;
                    HandlerContext = nullptr;
                    bTeardownOnExit = true;
                    bStop = true;
                    return S_OK;
                }
                ++ClosesUnderWay;
            }
            // The network thread carries the goodbye out; wait for the server to answer it.
            const DWORD Start = GetTickCount();
            for (;;)
            {
                {
                    std::lock_guard<std::recursive_mutex> Guard(Lock);
                    if (!bGoodbye || Connection == nullptr ||
                        Connection->GetState() == DPNet::FClient::EState::Closed ||
                        !NetThread.joinable())
                        break;
                }
                if (GetTickCount() - Start > kCloseWaitMs)
                    break;
                Sleep(kTickMs);
            }
            StopThread();
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            Teardown();
            --ClosesUnderWay;
            return S_OK;
        }
        STDMETHOD(ReturnBuffer)(DPNHANDLE Handle, DWORD) override
        {
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            return ReceiveBuffers.erase(Handle) != 0 ? S_OK : DPNERR_INVALIDHANDLE;
        }
        STDMETHOD(GetCaps)(DPN_CAPS* Caps, DWORD) override
        {
            if (Caps == nullptr)
                return DPNERR_INVALIDPOINTER;
            const DWORD Size = Caps->dwSize;
            std::memset(Caps, 0, sizeof(*Caps));
            Caps->dwSize = Size;
            return S_OK;
        }
        // DPNet keeps its own timeouts and retry limits (docs/dpnet.md).
        STDMETHOD(SetCaps)(const DPN_CAPS*, DWORD) override { return S_OK; }
        STDMETHOD(SetSPCaps)(const GUID*, const DPN_SP_CAPS*, DWORD) override { return S_OK; }
        STDMETHOD(GetSPCaps)(const GUID*, DPN_SP_CAPS*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }
        STDMETHOD(GetConnectionInfo)(DPN_CONNECTION_INFO* Info, DWORD) override
        {
            if (Info == nullptr)
                return DPNERR_INVALIDPOINTER;
            std::lock_guard<std::recursive_mutex> Guard(Lock);
            if (Phase != EPhase::Connected || Connection == nullptr)
                return DPNERR_NOCONNECTION;
            const DPNet::FLinkStats& Stats = Connection->GetLinkStats();
            const DWORD Size = Info->dwSize;
            std::memset(Info, 0, sizeof(*Info));
            Info->dwSize = Size;
            Info->dwRoundTripLatencyMS = Stats.RttMs;
            Info->dwBytesSentGuaranteed = (DWORD)Stats.BytesSent;
            Info->dwPacketsSentGuaranteed = (DWORD)Stats.FramesSent;
            Info->dwBytesReceivedGuaranteed = (DWORD)Stats.BytesReceived;
            Info->dwPacketsReceivedGuaranteed = (DWORD)Stats.FramesReceived;
            Info->dwPacketsRetried = (DWORD)Stats.FramesRetried;
            Info->dwPacketsDropped = (DWORD)Stats.FramesDropped;
            Info->dwMessagesTransmittedNormalPriority = (DWORD)Stats.MessagesSent;
            Info->dwMessagesTimedOutNormalPriority = (DWORD)Stats.MessagesTimedOut;
            Info->dwMessagesReceived = (DWORD)Stats.MessagesReceived;
            return S_OK;
        }
        STDMETHOD(RegisterLobby)(DPNHANDLE, struct IDirectPlay8LobbiedApplication*, DWORD) override
        {
            return DPNERR_UNSUPPORTED;
        }

    private:
        // dpnet.dll's TCP/IP service provider defaults for EnumHosts' 0 arguments.
        static constexpr DWORD kDefaultEnumCount = 5;
        static constexpr DWORD kDefaultEnumInterval = 1500;
        static constexpr DWORD kDefaultEnumTimeout = 1500;
        // Answers are matched to their enumeration by the query's EnumPayload; this many recent
        // queries are remembered.
        static constexpr size_t kMaxEnumPayloads = 256;

        enum class EPhase
        {
            Idle, // no connection, or the last one is over
            Connecting, // Connect called, CONNECT_COMPLETE not yet sent
            Connected, // CONNECT_COMPLETE sent with success
        };

        struct FEnumOp
        {
            DPNHANDLE Handle = 0;
            void* UserContext = nullptr;
            std::vector<DPNet::FAddress> Targets;
            bool bHasApplication = false;
            DPNet::FGuid Application;
            DPNet::FBytes UserData;
            DWORD Left = 0; // queries still to send; INFINITE: until cancelled
            DWORD Interval = 0;
            DWORD Timeout = 0;
            uint32_t LastSent = 0;
        };

        // ITransport
        void SendTo(const DPNet::FAddress& To, const uint8_t* Data, size_t Size) override
        {
            if (!Socket || To.Family != 4)
                return;
            const sockaddr_in Target = ToSockaddr(To);
            sendto(Socket.Get(), reinterpret_cast<const char*>(Data), (int)Size, 0,
                   reinterpret_cast<const sockaddr*>(&Target), sizeof(Target));
        }

        HRESULT Notify(DWORD MessageId, void* Message)
        {
            return MessageHandler != nullptr && !bMuted
                       ? MessageHandler(HandlerContext, MessageId, Message)
                       : S_OK;
        }

        // IClientEvents: each becomes the DPN_MSGID_ message dpnet.dll would send.
        void ClientEnumResponse(const DPNet::FAddress& From, const DPNet::FEnumResponse& Response,
                                uint32_t RoundTripMs) override
        {
            const auto Payload = EnumPayloads.find(Response.EnumPayload);
            if (Payload == EnumPayloads.end())
                return;
            const FEnumOp* Op = FindEnum(Payload->second);
            if (Op == nullptr)
                return; // that enumeration is over: dpnet.dll drops late answers too
            const std::basic_string<WCHAR> SessionName = FromU16(Response.SessionName);
            DPN_APPLICATION_DESC Desc{};
            Desc.dwSize = sizeof(Desc);
            Desc.dwFlags = Response.Flags;
            Desc.guidInstance = FromGuid(Response.Instance);
            Desc.guidApplication = FromGuid(Response.Application);
            Desc.dwMaxPlayers = Response.MaxPlayers;
            Desc.dwCurrentPlayers = Response.CurrentPlayers;
            Desc.pwszSessionName = const_cast<WCHAR*>(SessionName.c_str());
            if (!Response.ApplicationReserved.empty())
            {
                Desc.pvApplicationReservedData =
                    const_cast<uint8_t*>(Response.ApplicationReserved.data());
                Desc.dwApplicationReservedDataSize = (DWORD)Response.ApplicationReserved.size();
            }
            const FAddressRef Sender(FAddressObject::ForEndpoint(From));
            const FAddressRef Device(
                FAddressObject::ForEndpoint(DPNet::FAddress::Ipv4(0, 0, 0, 0, BoundPort)));
            DPNMSG_ENUM_HOSTS_RESPONSE Message{};
            Message.dwSize = sizeof(Message);
            Message.pAddressSender = Sender.get();
            Message.pAddressDevice = Device.get();
            Message.pApplicationDescription = &Desc;
            if (!Response.UserData.empty())
            {
                Message.pvResponseData = const_cast<uint8_t*>(Response.UserData.data());
                Message.dwResponseDataSize = (DWORD)Response.UserData.size();
            }
            Message.pvUserContext = Op->UserContext;
            Message.dwRoundTripLatencyMS = RoundTripMs;
            Notify(DPN_MSGID_ENUM_HOSTS_RESPONSE, &Message);
        }

        void ClientConnectComplete(uint32_t Result, const DPNet::FBytes& Reply) override
        {
            if (Phase != EPhase::Connecting)
                return; // cancelled
            Phase = Result == DPNet::Result::Ok ? EPhase::Connected : EPhase::Idle;
            DPNMSG_CONNECT_COMPLETE Message{};
            Message.dwSize = sizeof(Message);
            Message.hAsyncOp = ConnectHandle;
            Message.pvUserContext = ConnectContext;
            Message.hResultCode = (HRESULT)Result;
            if (!Reply.empty())
            {
                Message.pvApplicationReplyData = const_cast<uint8_t*>(Reply.data());
                Message.dwApplicationReplyDataSize = (DWORD)Reply.size();
            }
            ConnectHandle = 0;
            ConnectContext = nullptr;
            Notify(DPN_MSGID_CONNECT_COMPLETE, &Message);
        }

        // The data stays ours until the application returns it, as FServerObject::ServerReceive.
        void ClientReceive(DPNet::FBytes&& Data) override
        {
            if (Phase != EPhase::Connected)
                return;
            DPNHANDLE Handle = NextBufferHandle++;
            if (Handle == 0)
                Handle = NextBufferHandle++;
            DPNet::FBytes& Stored = ReceiveBuffers[Handle];
            Stored = std::move(Data);
            DPNMSG_RECEIVE Message{};
            Message.dwSize = sizeof(Message);
            Message.dpnidSender = HostDpnid();
            Message.pReceiveData = Stored.data();
            Message.dwReceiveDataSize = (DWORD)Stored.size();
            Message.hBufferHandle = Handle;
            if (Notify(DPN_MSGID_RECEIVE, &Message) != DPNSUCCESS_PENDING)
                ReceiveBuffers.erase(Handle);
        }

        void ClientSendComplete(uint64_t Handle, void* UserContext, bool bOk,
                                uint32_t SendTimeMs) override
        {
            DPNMSG_SEND_COMPLETE Message{};
            Message.dwSize = sizeof(Message);
            Message.hAsyncOp = (DPNHANDLE)Handle;
            Message.pvUserContext = UserContext;
            Message.hResultCode = bOk ? S_OK : DPNERR_CONNECTIONLOST;
            Message.dwSendTime = SendTimeMs;
            Notify(DPN_MSGID_SEND_COMPLETE, &Message);
        }

        // DPNERR_HOSTTERMINATEDSESSION when the server ended it (DestroyClient, or its Close),
        // with the data it sent; otherwise the connection was lost.
        void ClientTerminated(bool bByHost, const DPNet::FBytes& Data, DPNet::ECloseReason) override
        {
            if (Phase != EPhase::Connected)
                return;
            Phase = EPhase::Idle;
            DPNMSG_TERMINATE_SESSION Message{};
            Message.dwSize = sizeof(Message);
            Message.hResultCode = bByHost ? DPNERR_HOSTTERMINATEDSESSION : DPNERR_CONNECTIONLOST;
            if (bByHost && !Data.empty())
            {
                Message.pvTerminateData = const_cast<uint8_t*>(Data.data());
                Message.dwTerminateDataSize = (DWORD)Data.size();
            }
            Notify(DPN_MSGID_TERMINATE_SESSION, &Message);
        }

        // The server's player, from the name table the server sent.
        DPNID HostDpnid() const
        {
            if (Connection == nullptr)
                return 0;
            for (const DPNet::FNameTableEntry& Entry : Connection->GetSessionInfo().Entries)
            {
                if ((Entry.Flags & DPNet::EntryFlags::Host) != 0)
                    return Entry.Dpnid;
            }
            return 0;
        }

        // --- enumeration ---

        DPNHANDLE NextAsyncHandle()
        {
            // Apart from the send handles (FClient counts those up from 1).
            DPNHANDLE Handle = AsyncHandles++;
            if (Handle == 0)
                Handle = AsyncHandles++;
            return Handle;
        }

        FEnumOp* FindEnum(DPNHANDLE Handle)
        {
            for (FEnumOp& Op : Enums)
            {
                if (Op.Handle == Handle)
                    return &Op;
            }
            return nullptr;
        }

        void SendEnumQueries(FEnumOp& Op, uint32_t Now)
        {
            for (const DPNet::FAddress& Target : Op.Targets)
            {
                const uint16_t Payload = EnumSession->EnumHosts(
                    Target, Op.bHasApplication ? &Op.Application : nullptr, Op.UserData, Now);
                EnumPayloads.erase(Payload);
                EnumPayloads.emplace(Payload, Op.Handle);
                EnumPayloadOrder.push_back(Payload);
                if (EnumPayloadOrder.size() > kMaxEnumPayloads)
                {
                    const uint16_t Oldest = EnumPayloadOrder.front();
                    EnumPayloadOrder.pop_front();
                    // The same payload may have come round again since: only forget the old one.
                    if (std::count(EnumPayloadOrder.begin(), EnumPayloadOrder.end(), Oldest) == 0)
                        EnumPayloads.erase(Oldest);
                }
            }
            if (Op.Left != INFINITE)
                --Op.Left;
            Op.LastSent = Now;
        }

        // Each enumeration sends its next query after Interval and ends Timeout after its last.
        void TickEnums(uint32_t Now)
        {
            std::vector<DPNHANDLE> Done;
            for (FEnumOp& Op : Enums)
            {
                if (Op.Left == 0)
                {
                    if (Now - Op.LastSent >= Op.Timeout)
                        Done.push_back(Op.Handle);
                }
                else if (Now - Op.LastSent >= Op.Interval)
                {
                    SendEnumQueries(Op, Now);
                }
            }
            for (const DPNHANDLE Handle : Done)
                FinishEnum(Handle, S_OK);
        }

        // DPN_MSGID_ASYNC_OP_COMPLETE: S_OK when it ran its course, DPNERR_USERCANCEL when
        // cancelled.
        void FinishEnum(DPNHANDLE Handle, HRESULT Result)
        {
            void* UserContext = nullptr;
            bool bFound = false;
            for (auto It = Enums.begin(); It != Enums.end(); ++It)
            {
                if (It->Handle == Handle)
                {
                    UserContext = It->UserContext;
                    Enums.erase(It);
                    bFound = true;
                    break;
                }
            }
            if (!bFound)
                return;
            for (auto It = EnumPayloads.begin(); It != EnumPayloads.end();)
                It = It->second == Handle ? EnumPayloads.erase(It) : std::next(It);
            DPNMSG_ASYNC_OP_COMPLETE Message{};
            Message.dwSize = sizeof(Message);
            Message.hAsyncOp = Handle;
            Message.pvUserContext = UserContext;
            Message.hResultCode = Result;
            Notify(DPN_MSGID_ASYNC_OP_COMPLETE, &Message);
        }

        void CancelEnums()
        {
            while (!Enums.empty())
                FinishEnum(Enums.front().Handle, DPNERR_USERCANCEL);
        }

        // CONNECT_COMPLETE with DPNERR_USERCANCEL; the half-made connection is dropped.
        void CancelConnect()
        {
            if (Connection != nullptr)
                Connection->Close(NowMs());
            ClientConnectComplete((uint32_t)DPNERR_USERCANCEL, DPNet::FBytes());
        }

        // --- socket and thread ---

        // The first Connect or EnumHosts opens the socket on the device address's port (else the
        // first free one of 2302..2400, as DirectPlay's TCP/IP SP) and starts the network thread.
        bool StartNetwork(IDirectPlay8Address* Device)
        {
            if (EnumSession != nullptr)
                return true;
            DWORD Port = 0;
            static_cast<FAddressObject*>(Device)->GetDword(DPNA_KEY_PORT, Port);
            if (Port > 0xFFFF)
                return false;
            unsigned short Bound = 0;
            FUdpSocket Opened = OpenSocket((unsigned short)Port, Bound);
            if (!Opened)
                return false;
            // Enumeration without a host name is broadcast.
            BOOL Broadcast = TRUE;
            setsockopt(Opened.Get(), SOL_SOCKET, SO_BROADCAST,
                       reinterpret_cast<const char*>(&Broadcast), sizeof(Broadcast));
            Socket = std::move(Opened);
            BoundPort = Bound;
            DPNet::ITransport& Transport = *this;
            DPNet::IClientEvents& Events = *this;
            EnumSession = std::make_unique<DPNet::FClient>(Transport, Events);
            if (NetThread.joinable())
                NetThread.join();
            bStop = false;
            NetThread = std::thread([this] { NetLoop(); });
            return true;
        }

        void NetLoop()
        {
            std::vector<char> Buffer(64 * 1024);
            while (!bStop)
            {
                fd_set Readable;
                FD_ZERO(&Readable);
                FD_SET(Socket.Get(), &Readable);
                timeval Timeout{0, (FlLong)(kTickMs * 1000)};
                select((int)Socket.Get() + 1, &Readable, nullptr, nullptr, &Timeout);

                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (EnumSession == nullptr)
                    break;
                for (;;)
                {
                    sockaddr_in From{};
                    socklen_t FromSize = sizeof(From);
                    const int Got = recvfrom(Socket.Get(), Buffer.data(), (int)Buffer.size(), 0,
                                             reinterpret_cast<sockaddr*>(&From), &FromSize);
                    if (Got < 0)
                        break; // WSAEWOULDBLOCK: nothing more queued
                    if (Got == 0 || From.sin_family != AF_INET)
                        continue;
                    const uint8_t* Data = reinterpret_cast<const uint8_t*>(Buffer.data());
                    // [MC-DPLHP] messages start with a zero byte, [MC-DPL8R] frames never do.
                    if (Data[0] == 0)
                        EnumSession->OnDatagram(FromSockaddr(From), Data, (size_t)Got, NowMs());
                    else if (Connection != nullptr)
                        Connection->OnDatagram(FromSockaddr(From), Data, (size_t)Got, NowMs());
                    if (EnumSession == nullptr)
                        break; // a handler closed us
                }
                if (EnumSession == nullptr)
                    break;
                if (Connection != nullptr)
                    Connection->Tick(NowMs());
                TickEnums(NowMs());
            }

            // As FServerObject::NetLoop.
            bool bDelete = false;
            {
                std::lock_guard<std::recursive_mutex> Guard(Lock);
                if (bTeardownOnExit)
                {
                    Teardown();
                    bTeardownOnExit = false;
                }
                bDelete = bDeleteOnExit;
            }
            if (bDelete)
            {
                NetThread.detach();
                delete this;
            }
        }

        // Under Lock.
        void Teardown()
        {
            Connection.reset();
            EnumSession.reset();
            Enums.clear();
            EnumPayloads.clear();
            EnumPayloadOrder.clear();
            Phase = EPhase::Idle;
            ConnectHandle = 0;
            ConnectContext = nullptr;
            Socket = FUdpSocket();
            BoundPort = 0;
            // As in dpnet.dll, a closed object must be initialized again before it is used.
            MessageHandler = nullptr;
            HandlerContext = nullptr;
            bMuted = false;
            Winsock.reset();
        }

        bool OnNetThread() const
        {
            return NetThread.joinable() && NetThread.get_id() == std::this_thread::get_id();
        }

        void StopThread()
        {
            bStop = true;
            if (NetThread.joinable())
                NetThread.join();
        }

        void Shutdown()
        {
            if (MessageHandler != nullptr)
                Close(0);
            StopThread();
        }

        uint64_t Random()
        {
            LARGE_INTEGER Counter;
            QueryPerformanceCounter(&Counter);
            RandomState ^= (uint64_t)Counter.QuadPart + 0x9E3779B97F4A7C15ull + (RandomState << 6) +
                           (RandomState >> 2);
            return RandomState ^ ((uint64_t)GetCurrentProcessId() << 32);
        }

        LONG Refs = 1;
        std::recursive_mutex Lock;
        PFNDPNMESSAGEHANDLER MessageHandler = nullptr;
        void* HandlerContext = nullptr;
        bool bMuted = false; // Close under way: the handler hears nothing more
        std::optional<FWinsockSession> Winsock;
        FUdpSocket Socket;
        unsigned short BoundPort = 0;
        std::unique_ptr<DPNet::FClient> EnumSession; // also: the network is up
        std::unique_ptr<DPNet::FClient> Connection;
        EPhase Phase = EPhase::Idle;
        DPNHANDLE ConnectHandle = 0;
        void* ConnectContext = nullptr;
        std::vector<FEnumOp> Enums;
        std::map<uint16_t, DPNHANDLE> EnumPayloads;
        std::deque<uint16_t> EnumPayloadOrder;
        DPNHANDLE AsyncHandles = 0x40000001;
        uint64_t RandomState = 0;
        std::thread NetThread;
        std::atomic<bool> bStop{false};
        int ClosesUnderWay = 0;
        bool bTeardownOnExit = false;
        bool bDeleteOnExit = false;
        std::map<DPNHANDLE, DPNet::FBytes> ReceiveBuffers;
        DPNHANDLE NextBufferHandle = 1;
    };
    }

    namespace DPNetAdapter
    {
    HRESULT CreateServer(IDirectPlay8Server** Out)
    {
        if (Out == nullptr)
            return E_POINTER;
        *Out = new FServerObject();
        return S_OK;
    }

    HRESULT CreateClient(IDirectPlay8Client** Out)
    {
        if (Out == nullptr)
            return E_POINTER;
        *Out = new FClientObject();
        return S_OK;
    }

    HRESULT CreateAddress(IDirectPlay8Address** Out)
    {
        if (Out == nullptr)
            return E_POINTER;
        *Out = new FAddressObject();
        return S_OK;
    }
    }
