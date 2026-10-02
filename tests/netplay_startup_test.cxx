// Startup failure coverage against the installed GGPO library. No game loads.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>

#include "../src/sf4e/sf4e__NetplayStartup.hxx"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

sf4e::NetplayStartupWatchdog startup;
int saves = 0;
int advances = 0;
bool Begin(const char*) { return true; }
bool Save(unsigned char**, int*, int*, int) { ++saves; return false; }
bool Load(unsigned char*, int) { return false; }
bool Advance(int) { ++advances; return false; }
void Free(void*) {}
bool Event(GGPOEvent* event) {
    if (event->code == GGPO_EVENTCODE_RUNNING) startup.MarkRunning();
    return true;
}

GGPOPlayer Player(int number, GGPOPlayerType type) {
    GGPOPlayer player = {};
    player.size = sizeof(player);
    player.player_num = number;
    player.type = type;
    return player;
}
}

int main() {
    Require(!startup.Expired(100000), "no deadline before a session starts");
    startup.Arm(0);
    Require(!startup.Expired(29999), "first startup allows the full connection window");
    Require(startup.Expired(30000), "a start at tick zero still times out");
    startup.Reset();
    Require(!startup.Expired(30001), "closed sessions cannot time out again");
    startup.Arm(UINT64_C(0xffffffff) - 10000);
    Require(!startup.Expired(UINT64_C(0xffffffff) + 19999), "deadline survives the 32-bit tick boundary");
    Require(startup.Expired(UINT64_C(0xffffffff) + 20000), "deadline fires across the 32-bit boundary");
    startup.MarkRunning();
    Require(!startup.Expired(UINT64_C(0xffffffff) + 100000), "successful sessions are not ended by the startup deadline");
    startup.Arm(200000);
    Require(!startup.Running() && startup.Expired(230000), "a new epoch cannot inherit the previous Running event");

    GGPOPlayer slots[3] = { Player(1, GGPO_PLAYERTYPE_LOCAL), Player(2, GGPO_PLAYERTYPE_REMOTE),
        Player(0, GGPO_PLAYERTYPE_SPECTATOR) };
    Require(sf4e::ValidNetplayPlayerSlots(slots, 3), "one local and one remote may feed spectators");
    slots[0].type = GGPO_PLAYERTYPE_REMOTE;
    slots[1].type = GGPO_PLAYERTYPE_LOCAL;
    Require(sf4e::ValidNetplayPlayerSlots(slots, 2), "local side two is valid");
    slots[0].type = GGPO_PLAYERTYPE_LOCAL;
    Require(!sf4e::ValidNetplayPlayerSlots(slots, 2), "two local slots must never start an online game");
    slots[0].type = slots[1].type = GGPO_PLAYERTYPE_REMOTE;
    Require(!sf4e::ValidNetplayPlayerSlots(slots, 2), "a player game cannot lack a local side");
    slots[0] = Player(1, GGPO_PLAYERTYPE_LOCAL);
    slots[1] = Player(1, GGPO_PLAYERTYPE_REMOTE);
    Require(!sf4e::ValidNetplayPlayerSlots(slots, 2), "duplicate player numbers are invalid");
    Require(!sf4e::ValidNetplayPlayerSlots(slots, 1), "a missing opponent is invalid");
    Require(!sf4e::ValidNetplayPlayerSlots(nullptr, 2), "a missing roster is invalid");

    // Reserve a real UDP destination that never replies. GGPO alone remains
    // in startup; our application-clock deadline must recover even with zero
    // simulated frames and no GGPO RUNNING event.
    WSADATA data;
    Require(WSAStartup(MAKEWORD(2, 2), &data) == 0, "Winsock starts");
    SOCKET silentPeer = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    Require(silentPeer != INVALID_SOCKET, "silent peer socket opens");
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    Require(bind(silentPeer, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0,
        "silent peer owns a unique port");
    int length = sizeof(address);
    Require(getsockname(silentPeer, reinterpret_cast<sockaddr*>(&address), &length) == 0,
        "silent peer address is available");

    GGPOSessionCallbacks callbacks = {};
    callbacks.begin_game = Begin;
    callbacks.save_game_state = Save;
    callbacks.load_game_state = Load;
    callbacks.advance_frame = Advance;
    callbacks.free_buffer = Free;
    callbacks.on_event = Event;
    GGPOSession* session = nullptr;
    startup.Arm(0);
    Require(GGPO_SUCCEEDED(ggpo_start_session(&session, &callbacks, "startup-test", 2, sizeof(int), 0)),
        "real GGPO session starts");
    slots[0] = Player(1, GGPO_PLAYERTYPE_LOCAL);
    slots[1] = Player(2, GGPO_PLAYERTYPE_REMOTE);
    strcpy_s(slots[1].u.remote.ip_address, "127.0.0.1");
    slots[1].u.remote.port = ntohs(address.sin_port);
    GGPOPlayerHandle local = GGPO_INVALID_HANDLE, remote = GGPO_INVALID_HANDLE;
    Require(GGPO_SUCCEEDED(ggpo_add_player(session, &slots[0], &local)), "local side added");
    Require(GGPO_SUCCEEDED(ggpo_add_player(session, &slots[1], &remote)), "silent remote side added");
    for (int tick = 0; tick < 100; ++tick)
        Require(GGPO_SUCCEEDED(ggpo_idle(session, 1)), "startup network pump remains active");
    Require(!startup.Running(), "silent peer never supplies the Running event");
    Require(saves == 0 && advances == 0, "startup deadline does not depend on simulation callbacks");
    Require(startup.Expired(30000), "application clock recovers a real GGPO stalled start");
    Require(GGPO_SUCCEEDED(ggpo_close_session(session)), "stalled GGPO closes before engine teardown");
    session = nullptr;
    startup.Reset();
    Require(!startup.Expired(60000), "closed startup remains disarmed");
    closesocket(silentPeer);
    WSACleanup();
    std::cout << "Netplay startup recovery checks passed\n";
    return 0;
}
