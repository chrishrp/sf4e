#include "../src/session/sf4e__SessionServer.hxx"

#include <GameNetworkingSockets/steam/steamnetworkingsockets.h>
#include <spdlog/spdlog.h>
#include <chrono>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {
using json = nlohmann::json;
namespace P = sf4e::SessionProtocol;

void Require(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }

struct Network {
    Network() {
        SteamDatagramErrMsg error;
        Require(GameNetworkingSockets_Init(nullptr, error), error);
    }
    ~Network() { GameNetworkingSockets_Kill(); }
};

struct Peer {
    HSteamNetConnection client = k_HSteamNetConnection_Invalid;
    HSteamNetConnection server = k_HSteamNetConnection_Invalid;
    P::ConnectionID cid;
    P::SessionDataUpdate update;
    std::vector<P::InstantRematchEvent> events;
    std::vector<P::DirectPeer> directPeers;
    int allReady = 0, synced = 0, snapshots = 0, barrier = 0;

    void Send(const json& message) {
        const std::string bytes = message.dump();
        Require(SteamNetworkingSockets()->SendMessageToConnection(client, bytes.data(), (uint32)bytes.size(),
            k_nSteamNetworkingSend_Reliable | k_nSteamNetworkingSend_NoNagle, nullptr) == k_EResultOK, "wire send failed");
    }
    void Read() {
        if (client == k_HSteamNetConnection_Invalid) return;
        ISteamNetworkingMessage* messages[32] = {};
        const int count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(client, messages, 32);
        Require(count >= 0, "wire receive failed");
        for (int i = 0; i < count; ++i) {
            const char* bytes = static_cast<const char*>(messages[i]->m_pData);
            const std::string copy(bytes, bytes + messages[i]->m_cbSize);
            messages[i]->Release();
            const json message = json::parse(copy);
            const auto type = message.at("type").get<P::MessageType>();
            if (type == P::MT_SESSION_HELLO_RESP) cid = message.get<P::SessionHelloResp>().cid;
            else if (type == P::MT_SESSION_JOINREJ) throw std::runtime_error("server rejected test participant");
            else if (type == P::MT_SESSION_DATAUPDATE) update = message.get<P::SessionDataUpdate>();
            else if (type == P::MT_LOBBY_ALLREADY) ++allReady;
            else if (type == P::MT_BATTLE_SYNCED) ++synced;
            else if (type == P::MT_BATTLE_SNAPSHOT) ++snapshots;
            else if (type == P::MT_REMATCH_EVENT) events.push_back(message.get<P::InstantRematchEvent>());
            else if (type == P::MT_DIRECT_PEER) directPeers.push_back(message.get<P::DirectPeer>());
            else if (type == P::MT_FORWARD) barrier = message.at("msg").value("test_barrier", 0);
        }
    }
    int Events(P::InstantRematchAction action, uint64_t oldMatch) const {
        int count = 0;
        for (const auto& event : events) if (event.action == action && event.previousMatchId == oldMatch) ++count;
        return count;
    }
};

struct Room {
    sf4e::SessionServer server;
    std::vector<std::unique_ptr<Peer>> peers;
    int nextBarrier = 0;

    Room() : server("wire-test", "wire-test-build", false, 3, P::FixedPoint{0, 99}) {}
    ~Room() {
        for (const auto& peer : peers) SteamNetworkingSockets()->CloseConnection(peer->client, 0, nullptr, false);
        server.Close();
    }
    void Pump() {
        server.PrepareForCallbacks();
        SteamNetworkingSockets()->RunCallbacks();
        Require(server.Step() == 0, "server poll failed");
        for (const auto& peer : peers) peer->Read();
    }
    void Until(const std::function<bool()>& predicate, const char* description) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        do {
            Pump();
            if (predicate()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        } while (std::chrono::steady_clock::now() < deadline);
        throw std::runtime_error(description);
    }
    Peer& Join(const char* name, bool spectator) {
        std::unique_ptr<Peer> peer(new Peer());
        // true is important: actual UDP over 127.0.0.1 ephemeral ports, not
        // the GNS in-memory shortcut. No externally reachable listener exists.
        Require(SteamNetworkingSockets()->CreateSocketPair(&peer->server, &peer->client, true, nullptr, nullptr),
            "could not create network loopback socket pair");
        server.AddConnection(peer->server);
        Peer& p = *peer;
        peers.push_back(std::move(peer));
        p.Send(P::SessionHelloMsg());
        Until([&] { return !p.cid.user.empty(); }, "hello response timeout");
        P::SessionJoinRequest join;
        join.sidecarHash = "wire-test-build"; join.username = name;
        join.port = (uint16_t)(29000 + peers.size()); join.spectator = spectator;
        p.Send(join);
        Until([&] { return p.update.lobbyData.members.size() == peers.size(); }, "join update timeout");
        return p;
    }
    void Barrier(Peer& peer) {
        P::ForwardMessage fence;
        fence.src = peer.cid; fence.dest = peer.cid; fence.msg = { {"test_barrier", ++nextBarrier} };
        peer.Send(fence);
        Until([&] { return peer.barrier == nextBarrier; }, "processing barrier timeout");
    }
    void BarrierAll() { for (const auto& peer : peers) if (peer->client != k_HSteamNetConnection_Invalid) Barrier(*peer); }
    void Disconnect(Peer& peer) {
        Require(SteamNetworkingSockets()->CloseConnection(peer.client, 0, "test spectator departure", false), "disconnect failed");
        peer.client = k_HSteamNetConnection_Invalid;
    }
    uint64_t Start(Peer& p1, Peer& p2) {
        P::PreBattleSetChara pick = {};
        pick.chara.charaID = 0; p1.Send(pick);
        pick.chara.charaID = 1; p2.Send(pick);
        P::PreBattleSetEnv env; env.rngSeed = 12345; p1.Send(env);
        P::PreBattleSetStage stage; stage.stageID = 3; p1.Send(stage);
        P::LobbyReady ready; ready.inputDelay = 2; p1.Send(ready); p2.Send(ready);
        Until([&] { return p1.allReady > 0 && p2.allReady > 0 && peers[2]->allReady > 0; }, "all-ready timeout");
        const uint64_t id = p1.update.matchData.matchId;
        Require(id != 0 && p2.update.matchData.matchId == id, "clients agree on initial epoch");
        P::BattleLoaded loaded; loaded.matchId = id;
        p1.Send(loaded); p2.Send(loaded); peers[2]->Send(loaded);
        Until([&] { return p1.synced > 0 && p2.synced > 0 && peers[2]->synced > 0; }, "initial loaded barrier timeout");
        return id;
    }
};

void DirectOfferSafety() {
    Room room;
    Peer& host = room.Join("Local host", false);
    Peer& partner = room.Join("Remote partner", false);
    P::DirectOffer local;
    local.ip = local.localIp = "127.0.0.1";
    local.port = local.localPort = 23457;
    P::DirectOffer remote;
    remote.ip = remote.localIp = "100.100.10.2";
    remote.port = remote.localPort = 23457;
    host.Send(local);
    partner.Send(remote);
    room.BarrierAll();
    Require(host.directPeers.empty() && partner.directPeers.empty(),
        "local-only host offer must keep both players on the relay");

    // Even an older client with a bad public candidate can supply a usable
    // LAN/VPN candidate. Only that reachable endpoint should reach its peer.
    local.localIp = "100.100.10.1";
    host.Send(local);
    room.Until([&] { return host.directPeers.size() == 1 && partner.directPeers.size() == 1; }, "safe peer exchange timeout");
    Require(partner.directPeers.back().ip.empty() && partner.directPeers.back().port == 0
        && partner.directPeers.back().localIp == local.localIp && partner.directPeers.back().localPort == 23457,
        "server forwarded a loopback candidate beside the safe local candidate");
    Require(host.directPeers.back().ip == remote.ip && host.directPeers.back().port == remote.port,
        "server discarded a usable VPN endpoint");
}

void Run() {
    Room room;
    Peer& p1 = room.Join("Alice", false);
    Peer& p2 = room.Join("Bob", false);
    Peer& spectator = room.Join("Watcher", true);
    const uint64_t oldMatch = room.Start(p1, p2);
    const uint64_t aliceId = p1.update.lobbyData.members[0].roomMemberId;
    const uint64_t bobId = p1.update.lobbyData.members[1].roomMemberId;
    Require(p1.update.lobbyData.instantRematchAvailable, "server advertises rematch capability");
    Peer& late = room.Join("Late watcher", true);
    Require(!late.update.lobbyData.members.back().watching, "late spectator waits for a normal match");

    P::InstantRematchRequest vote;
    vote.matchId = oldMatch; vote.resultFrame = 500; vote.loserSide = 0; // P2 wins.
    p1.Send(vote); p1.Send(vote); p2.Send(vote); late.Send(vote);
    room.BarrierAll();
    Require(p1.Events(P::IR_PREPARE, oldMatch) == 0, "players cannot skip retained spectator readiness");
    spectator.Send(vote);
    room.Until([&] { return p1.Events(P::IR_PREPARE, oldMatch) == 1
        && p2.Events(P::IR_PREPARE, oldMatch) == 1 && spectator.Events(P::IR_PREPARE, oldMatch) == 1; }, "prepare timeout");
    room.BarrierAll();
    Require(late.events.empty(), "late spectator receives no retained-state prepare");

    P::InstantRematchAck ack; ack.matchId = oldMatch;
    p1.Send(ack); p1.Send(ack); p2.Send(ack); late.Send(ack);
    room.BarrierAll();
    Require(p1.Events(P::IR_START, oldMatch) == 0, "restore barrier includes retained spectator");
    spectator.Send(ack);
    room.Until([&] { return p1.Events(P::IR_START, oldMatch) == 1
        && p2.Events(P::IR_START, oldMatch) == 1 && spectator.Events(P::IR_START, oldMatch) == 1; }, "start timeout");
    room.BarrierAll();
    const uint64_t nextMatch = p1.update.matchData.matchId;
    Require(nextMatch > oldMatch && p1.events.back().nextMatchId == nextMatch, "new epoch reaches wire and lobby update");
    for (const auto& peer : room.peers) {
        const auto& members = peer->update.lobbyData.members;
        Require(members[0].roomMemberId == aliceId && members[1].roomMemberId == bobId, "winner does not rotate on instant restart");
        Require(members[0].roomScore.losses == 1 && members[1].roomScore.wins == 1, "all observers see exactly one result");
        Require(peer->update.matchData.chara[0].charaID == 0 && peer->update.matchData.chara[1].charaID == 1
            && peer->update.matchData.stageID == 3, "character and stage settings survive restart");
    }
    Require(late.events.empty() && late.allReady == 0, "late observer never starts an unavailable retained scene");

    // Reliable old packets and duplicate acknowledgments must not damage the
    // replacement epoch or broadcast an old load/snapshot into frame zero.
    P::LobbyReportResults oldResult; oldResult.matchId = oldMatch; oldResult.loserSide = 0;
    p1.Send(oldResult); spectator.Send(ack);
    const int synced = p1.synced;
    P::BattleLoaded oldLoaded; oldLoaded.matchId = oldMatch; p1.Send(oldLoaded); p2.Send(oldLoaded);
    P::BattleSnapshot oldSnapshot = {}; oldSnapshot.matchId = oldMatch; p1.Send(oldSnapshot);
    room.BarrierAll();
    Require(p1.update.matchData.matchId == nextMatch && p1.Events(P::IR_START, oldMatch) == 1, "old result and duplicate ack leave new epoch intact");
    Require(p1.synced == synced && p2.snapshots == 0, "old synchronization packets are not forwarded into new epoch");

    // Model a restore/start failure crossing the committed Start packet: this
    // caller still knows only the old ID, while another has received the new.
    P::InstantRematchCancel cancel; cancel.matchId = oldMatch; cancel.restartPending = true;
    p1.Send(cancel);
    room.Until([&] { return p1.Events(P::IR_ABORT, oldMatch) == 1
        && p2.Events(P::IR_ABORT, oldMatch) == 1 && spectator.Events(P::IR_ABORT, oldMatch) == 1; }, "startup cancellation timeout");
    room.BarrierAll();
    Require(p1.events.back().nextMatchId == nextMatch, "committed abort identifies both epochs");
    for (const auto& peer : room.peers) {
        Require(peer->update.matchData.matchId == 0 && !peer->update.matchData.IsAllReady(), "startup cancellation releases room readiness");
        Require(peer->update.lobbyData.members[0].roomScore.losses == 1
            && peer->update.lobbyData.members[1].roomScore.wins == 1, "aborted replacement adds no score");
    }
    p1.Send(oldResult); cancel.matchId = nextMatch; p2.Send(cancel);
    room.BarrierAll();
    Require(p1.update.lobbyData.members[0].roomScore.losses == 1
        && p1.update.lobbyData.members[1].roomScore.wins == 1, "duplicate teardown cannot double-count previous win");
}

void SpectatorDeparture(bool afterCommit) {
    Room room;
    Peer& p1 = room.Join("Alice", false);
    Peer& p2 = room.Join("Bob", false);
    Peer& watcher = room.Join("Watcher", true);
    uint64_t active = room.Start(p1, p2);
    if (afterCommit) {
        const uint64_t old = active;
        P::InstantRematchRequest vote; vote.matchId = old; vote.resultFrame = 500; vote.loserSide = 0;
        p1.Send(vote); p2.Send(vote); watcher.Send(vote);
        room.Until([&] { return p1.Events(P::IR_PREPARE, old) == 1
            && p2.Events(P::IR_PREPARE, old) == 1 && watcher.Events(P::IR_PREPARE, old) == 1; }, "disconnect fixture prepare timeout");
        P::InstantRematchAck ack; ack.matchId = old;
        p1.Send(ack); p2.Send(ack); watcher.Send(ack);
        room.Until([&] { return p1.Events(P::IR_START, old) == 1
            && p2.Events(P::IR_START, old) == 1 && watcher.Events(P::IR_START, old) == 1; }, "disconnect fixture start timeout");
        room.BarrierAll();
        active = p1.update.matchData.matchId;
        Require(active > old, "spectator disconnect fixture entered new epoch");
    }
    room.Disconnect(watcher);
    room.Until([&] { return p1.Events(P::IR_ABORT, active) == 1 && p2.Events(P::IR_ABORT, active) == 1; }, "spectator departure notification timeout");
    room.BarrierAll();
    Require(p1.events.back().nextMatchId == 0 && p1.events.back().reason == "participant_left",
        "spectator departure disables fast rematch without cancelling current score epoch");
    Require(p1.update.matchData.matchId == active && p1.update.matchData.IsAllReady()
        && p1.update.lobbyData.members.size() == 2, "players keep their active fight after spectator leaves");
    P::LobbyReportResults result; result.matchId = active; result.loserSide = 1;
    p1.Send(result);
    room.Until([&] { return p1.update.matchData.matchId == 0 && p2.update.matchData.matchId == 0; }, "result after spectator departure timeout");
    room.BarrierAll();
    const auto& members = p1.update.lobbyData.members;
    Require(members[0].roomScore.wins == 1 && members[1].roomScore.losses == 1,
        "fight finishing after spectator departure still records its result");
    Require(members[0].roomScore.losses == (afterCommit ? 1 : 0)
        && members[1].roomScore.wins == (afterCommit ? 1 : 0), "earlier committed score is preserved");
}
}

int main() {
    try {
        spdlog::set_level(spdlog::level::warn);
        Network network;
        DirectOfferSafety();
        Run();
        SpectatorDeparture(false);
        SpectatorDeparture(true);
        std::cout << "Instant rematch server wire tests passed over UDP loopback: players, spectators, epochs, scores and cancellation.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
