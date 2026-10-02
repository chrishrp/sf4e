#include "../src/session/sf4e__HolePunch.hxx"
#include "../src/session/sf4e__DirectEndpoint.hxx"

#include <ws2tcpip.h>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
void Require(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}

struct Network {
    Network() { WSADATA data; Require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "WSAStartup failed"); }
    ~Network() { WSACleanup(); }
};

struct Socket {
    SOCKET value = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Socket() { Require(value != INVALID_SOCKET, "UDP socket failed"); }
    ~Socket() { closesocket(value); }
};

std::string LocalUnicastAddress() {
    Socket socket;
    DWORD bytes = 0;
    WSAIoctl(socket.value, SIO_ADDRESS_LIST_QUERY, nullptr, 0, nullptr, 0, &bytes, nullptr, nullptr);
    if (bytes < sizeof(SOCKET_ADDRESS_LIST)) return {};
    std::vector<char> storage(bytes);
    if (WSAIoctl(socket.value, SIO_ADDRESS_LIST_QUERY, nullptr, 0, storage.data(), bytes,
        &bytes, nullptr, nullptr) != 0) return {};
    const auto* list = reinterpret_cast<const SOCKET_ADDRESS_LIST*>(storage.data());
    for (int i = 0; i < list->iAddressCount; ++i) {
        const SOCKET_ADDRESS& address = list->Address[i];
        if (address.iSockaddrLength < sizeof(sockaddr_in) || address.lpSockaddr->sa_family != AF_INET) continue;
        char ip[INET_ADDRSTRLEN] = {};
        const auto* v4 = reinterpret_cast<const sockaddr_in*>(address.lpSockaddr);
        inet_ntop(AF_INET, &v4->sin_addr, ip, sizeof(ip));
        if (sf4e::DirectEndpoint::Usable(ip, 1)) return ip;
    }
    return {};
}

void EndpointPolicy() {
    using namespace sf4e::DirectEndpoint;
    for (const char* ip : { "127.0.0.1", "127.9.8.7", "0.0.0.0", "0.1.2.3", "224.0.0.1",
        "239.255.255.250", "255.255.255.255", "", "localhost", "300.1.2.3", "1.2.3", "01.2.3.4" }) {
        Require(!Usable(ip, 23457), "invalid peer address accepted");
    }
    for (const char* ip : { "192.168.1.10", "10.0.0.3", "172.16.9.5", "100.96.207.115", "203.0.113.8" }) {
        Require(Usable(ip, 23457), "LAN, VPN or public peer address rejected");
        Require(!Usable(ip, 0), "zero peer port accepted");
    }
    Require(!Same("203.0.113.8", 30000, "203.0.113.8", 30001),
        "two players behind one NAT must retain distinct port endpoints");
}

void RejectSelfProof(const std::string& localIp) {
    sf4e::HolePunch punch;
    Require(punch.Open(0), "could not bind test punch socket");
    Require(punch.BoundPort() != 0, "ephemeral port was not recorded");
    for (bool matchStart : { false, true }) {
        std::string chosen = "stale";
        uint16_t port = 1;
        // This is the live failure: the server gives a player the other PC's
        // loopback endpoint, at the same port this player's socket has bound.
        // Before the fix it echoes its own token and reports a proven peer.
        Require(!punch.Punch("127.0.0.1", punch.BoundPort(), "127.0.0.1", punch.BoundPort(),
            "self-echo-regression", 500, chosen, port, matchStart), "loopback proved a connection to self");
        Require(chosen.empty() && port == 0, "rejected proof retained a stale endpoint");
        if (!localIp.empty()) {
            Require(!punch.Punch(localIp, punch.BoundPort(), "", 0, "local-self-echo",
                500, chosen, port, matchStart), "local interface proved a connection to self");
        }
    }
    punch.publicIp = "203.0.113.8";
    punch.publicPort = 45000;
    std::string chosen;
    uint16_t port = 0;
    Require(!punch.Punch(punch.publicIp, punch.publicPort, "", 0, "nat-self-echo",
        500, chosen, port), "our exact external mapping accepted as an opponent");
}

void LocalServerDiscovery() {
    Socket server;
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Require(bind(server.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "fake discovery bind failed");
    int addressLength = sizeof(address);
    Require(getsockname(server.value, reinterpret_cast<sockaddr*>(&address), &addressLength) == 0, "fake server address failed");
    DWORD timeout = 2000;
    setsockopt(server.value, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    std::atomic<bool> answered{false};
    std::thread responder([&] {
        char buffer[1500] = {};
        sockaddr_in from = {};
        int length = sizeof(from);
        const int received = recvfrom(server.value, buffer, sizeof(buffer), 0, reinterpret_cast<sockaddr*>(&from), &length);
        if (received <= 0) return;
        const std::string reply = "{\"ok\":true,\"ip\":\"127.0.0.1\",\"port\":"
            + std::to_string(ntohs(from.sin_port)) + "}";
        answered = sendto(server.value, reply.c_str(), static_cast<int>(reply.size()), 0,
            reinterpret_cast<sockaddr*>(&from), sizeof(from)) > 0;
    });
    sf4e::HolePunch punch;
    const bool opened = punch.Open(0);
    const bool discovered = opened && punch.Discover(address, nullptr, 1000);
    responder.join();
    Require(opened && answered, "local discovery fixture failed");
    Require(!discovered && punch.publicPort == 0 && punch.localPort == 0,
        "local server advertised a loopback endpoint to the other PC");
}

void DifferentPortsRemainPeers(const std::string& ip) {
    if (ip.empty()) {
        std::cout << "No unicast interface; positive local-interface UDP fixture skipped.\n";
        return;
    }
    sf4e::HolePunch first, second;
    Require(first.Open(0) && second.Open(0), "peer fixture bind failed");
    // The same address is legitimate for two endpoints with distinct ports.
    // This also exercises the normal probe, marked probe and ACK machinery.
    first.publicIp = first.localIp = second.publicIp = second.localIp = ip;
    first.publicPort = first.localPort = first.BoundPort();
    second.publicPort = second.localPort = second.BoundPort();
    std::string firstPeer, secondPeer;
    uint16_t firstPort = 0, secondPort = 0;
    bool firstOk = false;
    std::thread firstThread([&] {
        firstOk = first.Punch(ip, second.BoundPort(), ip, second.BoundPort(), "real-peers",
            2000, firstPeer, firstPort, true);
    });
    const bool secondOk = second.Punch(ip, first.BoundPort(), ip, first.BoundPort(), "real-peers",
        2000, secondPeer, secondPort, true);
    firstThread.join();
    Require(firstOk && secondOk, "valid separate peer sockets failed to prove both directions");
    Require(firstPeer == ip && firstPort == second.BoundPort()
        && secondPeer == ip && secondPort == first.BoundPort(), "peer proof selected the wrong endpoint");
}
}

int main() {
    try {
        Network network;
        EndpointPolicy();
        const std::string localIp = LocalUnicastAddress();
        RejectSelfProof(localIp);
        LocalServerDiscovery();
        DifferentPortsRemainPeers(localIp);
        std::cout << "Hole-punch UDP tests passed: self echo rejected, localhost discovery relayed, distinct peers connect.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
