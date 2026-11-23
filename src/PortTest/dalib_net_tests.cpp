// Freelancer Decomp: a reimplementation of the Freelancer (2003) game client and engine,
// faithful to the behaviour of the original game.

// PORT(dpnet): DALib's client and server, CDPClient and CDPServer, talking to each other over
// loopback through DPNet (DPNetAdapter.cpp, docs/dpnet.md) as the game and FLServer do: the
// client enumerates the server, connects, both send, the server drops the client, and a second
// client connects and goes away. A wrong application GUID is refused. The exit code is the
// number of failures.

#include "dalib_net.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
int G_Failures = 0;

void Check(bool bOk, const char* What)
{
    std::printf("%s: %s\n", bOk ? "ok" : "FAILED", What);
    if (!bOk)
        ++G_Failures;
}

using FBytes = std::vector<unsigned char>;

FBytes Bytes(const char* Text)
{
    return FBytes(Text, Text + std::strlen(Text));
}

// A UDP port that is free now: the one the system picks for port 0.
unsigned short FreeUdpPort()
{
    int const Socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (Socket < 0)
        return 0;
    sockaddr_in Local{};
    Local.sin_family = AF_INET;
    Local.sin_addr.s_addr = htonl(INADDR_ANY);
    socklen_t Size = sizeof(Local);
    unsigned short Port = 0;
    if (bind(Socket, reinterpret_cast<const sockaddr*>(&Local), sizeof(Local)) == 0 &&
        getsockname(Socket, reinterpret_cast<sockaddr*>(&Local), &Size) == 0)
        Port = ntohs(Local.sin_port);
    close(Socket);
    return Port;
}

GUID G_Application = {0x26f3a1d5, 0x0c39, 0x4c4e, {0x9b, 0x58, 0x6d, 0x2e, 0x11, 0x84, 0x07, 0x5a}};

class FTestProxy : public CDPClientProxy
{
public:
    void OnReceive(unsigned char* Data, FlULong Size) override
    {
        Received.assign(Data, Data + Size);
    }
    void OnConnect(FlLong) override { bConnected = true; }
    void OnDisconnect() override { ++G_Disconnects; }

    FBytes Received;
    bool bConnected = false;
    static int G_Disconnects;
};
int FTestProxy::G_Disconnects = 0;

class FTestServer : public CDPServer
{
public:
    CDPClientProxy* CreateClientProxy() override
    {
        Last = new FTestProxy();
        return Last;
    }

    FTestProxy* Last = nullptr;
};

class FTestClient : public CDPClient
{
public:
    void OnReceive(unsigned char* Data, FlULong Size) override
    {
        Received.assign(Data, Data + Size);
    }
    void OnConnect(FlLong Result) override
    {
        bConnectDone = true;
        ConnectResult = Result;
    }
    void OnDisconnect() override { bDisconnected = true; }
    void OnAddHost(_DPNMSG_ENUM_HOSTS_RESPONSE* Response) override
    {
        ++Hosts;
        const DPN_APPLICATION_DESC* Desc = Response->pApplicationDescription;
        SessionName = Desc->pwszSessionName;
        MaxPlayers = Desc->dwMaxPlayers;
        CurrentPlayers = Desc->dwCurrentPlayers;
        const char* Data = static_cast<const char*>(Response->pvResponseData);
        ResponseData.assign(Data, Data + Response->dwResponseDataSize);
        wchar_t Host[0x400];
        DWORD Size = sizeof(Host);
        DWORD Type = DPNA_DATATYPE_STRING;
        if (SUCCEEDED(
                Response->pAddressSender->GetComponentByName(L"hostname", Host, &Size, &Type)))
            HostName = Host;
        Size = sizeof(Port);
        Response->pAddressSender->GetComponentByName(L"port", &Port, &Size, &Type);
    }

    static FlULong PingsAnswered() { return m_dwPingReceiveCount; }
    DPNHANDLE EnumHandle() const { return PendingConnectHandle; }

    FBytes Received;
    bool bConnectDone = false;
    FlLong ConnectResult = -1;
    bool bDisconnected = false;
    int Hosts = 0;
    std::wstring SessionName;
    DWORD MaxPlayers = 0;
    DWORD CurrentPlayers = 0;
    std::string ResponseData;
    std::wstring HostName;
    DWORD Port = 0;
};

// Runs both game loops until Done, for at most TimeoutMs.
template <typename FDone>
bool Pump(FTestServer& Server, FTestClient* Client, FDone Done, int TimeoutMs = 5000)
{
    auto const Start = std::chrono::steady_clock::now();
    for (;;)
    {
        if (Client != nullptr)
            Client->DispatchMsgs(0);
        Server.DispatchMsgs();
        if (Done())
            return true;
        if (std::chrono::steady_clock::now() - Start > std::chrono::milliseconds(TimeoutMs))
            return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

bool Connect(FTestClient& Client, unsigned short Port)
{
    wchar_t Url[0x100];
    std::swprintf(Url, 0x100, L"x-directplay:/hostname=127.0.0.1;port=%u", (unsigned)Port);
    return Client.AddConnectAttempt(Url, nullptr) && Client.ProcessConnectAttempt();
}
}

int main()
{
    unsigned short const Port = FreeUdpPort();
    Check(Port != 0, "a free UDP port");
    if (Port == 0)
        return 1;

    FTestServer Server;
    Server.SetGUID(G_Application);
    Server.SetSessionName(L"DPNet loopback");
    Server.SetMaxPlayers(4);
    char const Reply[] = "1:0:65536:7:key:";
    Server.SetEnumResponse(const_cast<char*>(Reply), sizeof(Reply));
    Check(Server.BeginHosting(Port), "CDPServer hosts through DPNet");

    // 1. The server list's enumeration (CLoginMgr::PingServers: two queries, default timing).
    FTestClient* Client = new FTestClient();
    Client->SetGUID(G_Application);
    Check(Client->EnumerateHost(L"127.0.0.1", Port, 2, 0, 0), "EnumerateHost starts");
    Check(Pump(Server, Client, [&] { return Client->Hosts > 0; }),
          "the server answers the enumeration");
    Check(Client->SessionName == L"DPNet loopback", "with its session name");
    Check(Client->ResponseData == std::string(Reply, sizeof(Reply)), "and its enumeration reply");
    // SetMaxPlayers counts the server's own player in, as CLoginMgr::OnAddHost takes it out.
    Check(Client->MaxPlayers == 5 && Client->CurrentPlayers == 1,
          "and its player counts (its own player)");
    Check(Client->HostName == L"127.0.0.1" && Client->Port == Port,
          "the sender address names the server");
    Client->CancelEnums();

    // 2. A wrong application is refused, the attempt reported as failed.
    GUID Other = G_Application;
    Other.Data1 ^= 1;
    Client->SetGUID(Other);
    Check(Connect(*Client, Port), "a connect to the wrong application starts");
    Check(Pump(Server, Client, [&] { return Client->bConnectDone; }), "and completes");
    Check(Client->ConnectResult == (FlLong)DPNERR_INVALIDAPPLICATION,
          "refused: DPNERR_INVALIDAPPLICATION");

    // 3. Connect, send both ways.
    Client->SetGUID(G_Application);
    Client->bConnectDone = false;
    Check(Connect(*Client, Port), "Connect starts");
    Check(
        Pump(Server, Client, [&]
             { return Client->bConnectDone && Server.Last != nullptr && Server.Last->bConnected; }),
        "the client connects, and the server creates its player");
    Check(Client->ConnectResult == 0 && Client->GetConnectResult() == 0, "with success");
    FTestProxy* const Proxy = Server.Last;
    if (Client->ConnectResult != 0 || Proxy == nullptr)
    {
        delete Client;
        Server.StopHosting();
        return G_Failures;
    }

    FBytes const Up = Bytes("client to server");
    Check(Client->Send(const_cast<unsigned char*>(Up.data()), (FlULong)Up.size()),
          "CDPClient::Send");
    Check(Pump(Server, Client, [&] { return Proxy->Received == Up; }), "the server receives it");

    FBytes const Down = Bytes("server to client, server to client, server to client");
    Check(Proxy->Send(const_cast<unsigned char*>(Down.data()), (FlULong)Down.size()),
          "CDPClientProxy::Send");
    Check(Pump(Server, Client, [&] { return Client->Received == Down; }), "the client receives it");

    // The client's once-a-second ping, answered by the server's link-quality byte.
    Check(Pump(
              Server, Client, [] { return FTestClient::PingsAnswered() > 0; }, 3000),
          "a ping is answered");
    Check(Pump(Server, Client, [] { return CDPClient::GetSendQueueSize() == 0; }),
          "GetSendQueueSize: nothing left unacknowledged");

    // 4. The server drops the client: both sides hear of it.
    int const DisconnectsBefore = FTestProxy::G_Disconnects;
    Server.DisconnectClient(Proxy->PlayerId);
    Check(Pump(Server, Client, [&]
               { return Client->bDisconnected && FTestProxy::G_Disconnects > DisconnectsBefore; }),
          "DisconnectClient: the client's session ends, the server's player goes");
    Check(!Client->Send(const_cast<unsigned char*>(Up.data()), (FlULong)Up.size()),
          "no Send after that");
    delete Client;

    // 5. Another client connects and is destroyed (its destructor closes the connection).
    FTestClient* Second = new FTestClient();
    Second->SetGUID(G_Application);
    Server.Last = nullptr;
    Check(Connect(*Second, Port), "a second client connects");
    Check(
        Pump(Server, Second, [&]
             { return Second->bConnectDone && Server.Last != nullptr && Server.Last->bConnected; }),
        "and the server creates its player");
    Check(Second->ConnectResult == 0, "with success");
    int const Before = FTestProxy::G_Disconnects;
    delete Second; // Close: the goodbye goes out before it returns
    Check(Pump(Server, nullptr, [&] { return FTestProxy::G_Disconnects > Before; }),
          "the closed client's player goes on the server");

    Server.StopHosting();
    std::printf("%d failure(s)\n", G_Failures);
    return G_Failures;
}
