// Integration test for the installed, patched GGPO library. No game is loaded.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <ggponet.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

namespace {
    constexpr int TerminalFrame = 90;
    constexpr int EpochsPerDelay = 3;
    constexpr int ProvisionalKoInputFrame = 40;
    constexpr uint32_t KoInput = 1000003;

    struct Input {
        uint32_t epoch;
        uint32_t value;
    };

    struct State {
        int frame = 0;
        uint64_t hash = UINT64_C(0xcbf29ce484222325);
        uint64_t position[2] = { 0, 0 };
        bool provisionalKo = false;
    };

    struct Instance {
        GGPOSession* session = nullptr;
        GGPOPlayerHandle local = GGPO_INVALID_HANDLE;
        State state;
        State baseline;
        int side = 0;
        int epoch = 0;
        int delay = 0;
        bool retractionCase = false;
        bool running = false;
        bool confirmedVote = false;
        int saves = 0;
        int frees = 0;
        int loads = 0;
        int rollbackFrames = 0;
    };

    // GGPO's C callbacks carry no user pointer. All API entries below use this
    // single-threaded guard, including calls made from rollback callbacks.
    Instance* current = nullptr;
    bool failed = false;
    std::string failure;

    struct Context {
        Instance* previous;
        explicit Context(Instance& instance) : previous(current) { current = &instance; }
        ~Context() { current = previous; }
    };

    void Check(bool ok, const char* message) {
        if (!ok && !failed) {
            failed = true;
            failure = message;
            std::cerr << "FAIL: " << message;
            if (current) std::cerr << " (epoch " << current->epoch << ", side "
                << current->side << ", frame " << current->state.frame << ')';
            std::cerr << '\n';
        }
    }

    bool Ok(GGPOErrorCode result, const char* operation) {
        if (!GGPO_SUCCEEDED(result)) {
            Check(false, operation);
            std::cerr << "GGPO error: " << static_cast<int>(result) << '\n';
            return false;
        }
        return true;
    }

    Input MakeInput(int epoch, int side, int frame, int delay, bool retractionCase) {
        // The last confirmed input before the trap is a hit. The real next
        // input releases it; holding the sender lets GGPO predict a repeat.
        if (retractionCase && side == 1 && frame == ProvisionalKoInputFrame - delay - 1)
            return { static_cast<uint32_t>(epoch), KoInput };
        return { static_cast<uint32_t>(epoch),
            static_cast<uint32_t>((frame * 37 + side * 101 + epoch * 17) % 251 + 1) };
    }

    void Simulate(State& state, const Input (&input)[2], int epoch, bool retractionCase) {
        Check(state.frame < TerminalFrame, "simulation cannot pass the held terminal frame");
        Check(!state.provisionalKo, "a provisional knockout holds ordinary simulation");
        if (retractionCase && state.frame == ProvisionalKoInputFrame)
            state.provisionalKo = input[1].value == KoInput;
        for (int side = 0; side < 2; ++side) {
            // GGPO initially pads input delay with zeroes. Any nonzero packet
            // must belong to this epoch, even when the same UDP ports recur.
            Check(input[side].epoch == 0 || input[side].epoch == static_cast<uint32_t>(epoch),
                "old-epoch input must not enter the new match");
            Check(input[side].epoch != 0 || input[side].value == 0,
                "initial padding must be completely zero");
            state.position[side] += input[side].value;
            state.hash ^= (static_cast<uint64_t>(input[side].epoch) << 32) | input[side].value;
            state.hash *= UINT64_C(0x100000001b3);
            state.hash ^= static_cast<uint64_t>(side + state.frame * 2);
        }
        ++state.frame;
    }

    bool Same(const State& a, const State& b) {
        return a.frame == b.frame && a.hash == b.hash &&
            a.position[0] == b.position[0] && a.position[1] == b.position[1] &&
            a.provisionalKo == b.provisionalKo;
    }

    bool __cdecl Begin(const char*) { Check(current != nullptr, "begin callback has context"); return true; }
    bool __cdecl LogState(char*, unsigned char*, int) { return true; }

    bool __cdecl Save(unsigned char** buffer, int* length, int* checksum, int frame) {
        Check(current != nullptr, "save callback has context");
        Check(current->state.frame == frame, "GGPO save frame equals the simulation state frame");
        State* saved = new State(current->state);
        *buffer = reinterpret_cast<unsigned char*>(saved);
        *length = sizeof(State);
        *checksum = static_cast<int>(saved->hash ^ (saved->hash >> 32));
        ++current->saves;
        return true;
    }

    bool __cdecl Load(unsigned char* buffer, int length) {
        Check(current != nullptr && buffer != nullptr, "load callback has state and context");
        Check(length == sizeof(State), "saved-state size is unchanged");
        current->state = *reinterpret_cast<State*>(buffer);
        ++current->loads;
        return true;
    }

    void __cdecl Free(void* buffer) {
        Check(current != nullptr, "free callback has context");
        if (buffer) {
            ++current->frees;
            delete static_cast<State*>(buffer);
        }
    }

    bool __cdecl Advance(int) {
        Check(current != nullptr, "rollback callback has context");
        Instance& instance = *current;
        Context context(instance);
        Input input[2] = {};
        int disconnected = 0;
        if (!Ok(ggpo_synchronize_input(instance.session, input, sizeof(input), &disconnected),
            "rollback input synchronization")) return false;
        Check(disconnected == 0, "rollback cannot disconnect a player");
        Simulate(instance.state, input, instance.epoch, instance.retractionCase);
        ++instance.rollbackFrames;
        return Ok(ggpo_advance_frame(instance.session), "rollback frame advance");
    }

    bool __cdecl Event(GGPOEvent* event) {
        Check(current != nullptr, "event callback has context");
        if (event->code == GGPO_EVENTCODE_RUNNING) current->running = true;
        if (event->code == GGPO_EVENTCODE_DISCONNECTED_FROM_PEER)
            Check(false, "loopback peer unexpectedly disconnected");
        return true;
    }

    void __cdecl Assertion(const char* message) {
        Check(false, "GGPO internal assertion");
        std::cerr << message << '\n';
    }

    GGPOSessionCallbacks Callbacks() {
        GGPOSessionCallbacks callbacks = {};
        callbacks.begin_game = Begin;
        callbacks.advance_frame = Advance;
        callbacks.load_game_state = Load;
        callbacks.save_game_state = Save;
        callbacks.free_buffer = Free;
        callbacks.on_event = Event;
        callbacks.log_game_state = LogState;
        return callbacks;
    }

    bool ChoosePorts(unsigned short (&ports)[3]) {
        SOCKET reservations[3] = { INVALID_SOCKET, INVALID_SOCKET, INVALID_SOCKET };
        bool success = true;
        for (int i = 0; i < 3 && success; ++i) {
            reservations[i] = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
            sockaddr_in address = {};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            int length = sizeof(address);
            success = reservations[i] != INVALID_SOCKET &&
                bind(reservations[i], reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0 &&
                getsockname(reservations[i], reinterpret_cast<sockaddr*>(&address), &length) == 0;
            if (success) ports[i] = ntohs(address.sin_port);
        }
        for (SOCKET reservation : reservations)
            if (reservation != INVALID_SOCKET) closesocket(reservation);
        Check(success, "reserve three distinct loopback UDP ports");
        return success;
    }

    void CloseAll(Instance (&instances)[3]) {
        for (Instance& instance : instances) {
            if (!instance.session) continue;
            Context context(instance);
            Ok(ggpo_close_session(instance.session), "close old session");
            instance.session = nullptr;
            Check(instance.saves == instance.frees, "session close releases every owned save buffer");
        }
    }

    bool StartAll(Instance (&instances)[3], const unsigned short (&ports)[3], int delay, int epoch,
        bool retractionCase) {
        // Enforce prepare/ack/start ordering: no new endpoint may coexist with
        // any old endpoint, and baseline data has independent ownership.
        for (Instance& instance : instances) Check(instance.session == nullptr, "all old sessions closed before restart");
        if (failed) return false;
        for (int i = 0; i < 3; ++i) {
            const State retained = instances[i].baseline;
            instances[i] = Instance();
            Instance& instance = instances[i];
            instance.baseline = retained;
            instance.state = retained;
            instance.side = i;
            instance.epoch = epoch;
            instance.delay = delay;
            instance.retractionCase = retractionCase;
            Check(instance.state.frame == 0, "retained baseline begins at frame zero");
            Context context(instance);
            auto callbacks = Callbacks();
            if (i == 2) {
                char host[] = "127.0.0.1";
                if (!Ok(ggpo_start_spectating(&instance.session, &callbacks, "rematch-test",
                    2, sizeof(Input), ports[i], host, ports[0]), "start spectator")) return false;
            } else {
                if (!Ok(ggpo_start_session(&instance.session, &callbacks, "rematch-test",
                    2, sizeof(Input), ports[i]), "start player session")) return false;
                for (int side = 0; side < 2; ++side) {
                    GGPOPlayer player = {};
                    player.size = sizeof(player);
                    player.player_num = side + 1;
                    player.type = side == i ? GGPO_PLAYERTYPE_LOCAL : GGPO_PLAYERTYPE_REMOTE;
                    if (side != i) {
                        std::strcpy(player.u.remote.ip_address, "127.0.0.1");
                        player.u.remote.port = ports[side];
                    }
                    GGPOPlayerHandle handle;
                    if (!Ok(ggpo_add_player(instance.session, &player, &handle), "add player")) return false;
                    if (side == i) {
                        instance.local = handle;
                        if (!Ok(ggpo_set_frame_delay(instance.session, handle, delay), "set input delay")) return false;
                    }
                }
                if (i == 0) {
                    GGPOPlayer spectator = {};
                    spectator.size = sizeof(spectator);
                    spectator.type = GGPO_PLAYERTYPE_SPECTATOR;
                    std::strcpy(spectator.u.remote.ip_address, "127.0.0.1");
                    spectator.u.remote.port = ports[2];
                    GGPOPlayerHandle handle;
                    if (!Ok(ggpo_add_player(instance.session, &spectator, &handle), "add spectator")) return false;
                }
            }
            if (!Ok(ggpo_set_disconnect_timeout(instance.session, 5000), "set disconnect timeout")) return false;
        }
        return !failed;
    }

    void Pump(Instance& instance) {
        Context context(instance);
        Ok(ggpo_idle(instance.session, 0), "pump session");
    }

    void Step(Instance& instance) {
        if (!instance.running || instance.state.frame >= TerminalFrame || instance.state.provisionalKo || failed) return;
        Context context(instance);
        if (instance.side < 2) {
            Input local = MakeInput(instance.epoch, instance.side, instance.state.frame,
                instance.delay, instance.retractionCase);
            const auto result = ggpo_add_local_input(instance.session, instance.local, &local, sizeof(local));
            if (result == GGPO_ERRORCODE_PREDICTION_THRESHOLD || result == GGPO_ERRORCODE_NOT_SYNCHRONIZED) return;
            if (!Ok(result, "submit local input")) return;
        }
        Input input[2] = {};
        int disconnected = 0;
        const auto result = ggpo_synchronize_input(instance.session, input, sizeof(input), &disconnected);
        if (result == GGPO_ERRORCODE_PREDICTION_THRESHOLD || result == GGPO_ERRORCODE_NOT_SYNCHRONIZED) return;
        if (!Ok(result, "synchronize forward input")) return;
        Check(disconnected == 0, "forward simulation cannot disconnect a player");
        Simulate(instance.state, input, instance.epoch, instance.retractionCase);
        Ok(ggpo_advance_frame(instance.session), "advance forward frame");
    }

    int Confirmed(Instance& instance) {
        Context context(instance);
        int frame = -1;
        Ok(ggpo_get_last_confirmed_frame(instance.session, &frame), "query confirmed frame");
        return frame;
    }

    State Expected(int epoch, int delay, bool retractionCase) {
        State state;
        while (state.frame < TerminalFrame) {
            Input input[2] = {};
            if (state.frame >= delay)
                for (int side = 0; side < 2; ++side)
                    input[side] = MakeInput(epoch, side, state.frame - delay, delay, retractionCase);
            Simulate(state, input, epoch, retractionCase);
        }
        return state;
    }
}

int main() {
    WSADATA sockets;
    if (WSAStartup(MAKEWORD(2, 2), &sockets) != 0) return 1;
    Instance instances[3];
    { Context context(instances[0]); ggpo_set_assert_handler(Assertion); }
    unsigned short ports[3] = {};
    if (!ChoosePorts(ports)) { WSACleanup(); return 1; }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(28);
    int totalRollbackFrames = 0, speculativeTerminals = 0, epoch = 0;
    bool retractionPassed = false;

    for (int scenario = 0; scenario < 3 && !failed; ++scenario) {
        const int delay = scenario == 0 ? 1 : 3;
        const bool retractionCase = scenario == 2;
        const int rounds = retractionCase ? 1 : EpochsPerDelay;
        for (int round = 0; round < rounds && !failed; ++round) {
            if (!StartAll(instances, ports, delay, ++epoch, retractionCase)) break;
            bool sawSpeculativeTerminal = false, finalStallStarted = false;
            bool sawProvisionalKo = false, sawKoRetracted = false;
            int loadsAtProvisionalKo = -1;
            int finalStallTicks = 0;
            unsigned iteration = 0;
            while (!failed) {
                if (std::chrono::steady_clock::now() >= deadline) {
                    Check(false, "all rematch epochs complete within 28 seconds");
                    for (auto& instance : instances)
                        std::cerr << "side " << instance.side << ", running " << instance.running
                            << ", frame " << instance.state.frame << ", confirmed " << Confirmed(instance) << '\n';
                    break;
                }
                const bool allRunning = instances[0].running && instances[1].running && instances[2].running;
                if (allRunning && !finalStallStarted &&
                    (std::min)(instances[0].state.frame, instances[1].state.frame) >= 84 &&
                    (std::max)(instances[0].state.frame, instances[1].state.frame) < TerminalFrame) {
                    finalStallStarted = true;
                    finalStallTicks = 18;
                }
                // Pausing the entire second endpoint is intentional:
                // ggpo_advance_frame also pumps, so skipping idle alone would
                // not exercise delayed remote input or terminal draining.
                const bool holdForProvisionalKo = retractionCase && !sawProvisionalKo &&
                    instances[1].state.frame >= ProvisionalKoInputFrame - delay;
                const bool periodicStallAllowed = !retractionCase || sawKoRetracted;
                const bool withholdSecond = allRunning && (holdForProvisionalKo || finalStallTicks > 0 ||
                    (periodicStallAllowed && !finalStallStarted && instances[1].state.frame < 80 && iteration % 24 < 6));
                for (int i = 0; i < 3; ++i) {
                    if (i == 1 && withholdSecond) continue;
                    Pump(instances[i]);
                    if (i == 0 && sawProvisionalKo && !sawKoRetracted && !instances[0].state.provisionalKo) {
                        Check(instances[0].state.frame == ProvisionalKoInputFrame + 1,
                            "rollback retracts knockout at the held frame before forward play resumes");
                        Check(instances[0].loads > loadsAtProvisionalKo,
                            "real GGPO load/replay retracts the predicted knockout");
                        Check(!instances[0].confirmedVote, "retracted knockout never becomes a result vote");
                        sawKoRetracted = true;
                    }
                    Step(instances[i]);
                }
                if (instances[0].state.provisionalKo) {
                    Check(retractionCase && instances[0].state.frame == ProvisionalKoInputFrame + 1,
                        "the intended prediction creates the provisional knockout");
                    Check(Confirmed(instances[0]) < ProvisionalKoInputFrame,
                        "provisional knockout input is still unconfirmed");
                    Check(!instances[0].confirmedVote, "provisional knockout cannot vote");
                    if (!sawProvisionalKo) loadsAtProvisionalKo = instances[0].loads;
                    sawProvisionalKo = true;
                }
                if (finalStallTicks > 0) --finalStallTicks;
                bool playersReady = true;
                for (int i = 0; i < 2; ++i) {
                    Instance& instance = instances[i];
                    const int confirmed = Confirmed(instance);
                    const bool terminal = instance.state.frame == TerminalFrame;
                    const bool settled = terminal && confirmed >= TerminalFrame - 1;
                    if (terminal && !settled) sawSpeculativeTerminal = true;
                    // A terminal screen is not a vote until its last input is
                    // confirmed. F-1 is fixed while ordinary stepping stops.
                    if (settled) instance.confirmedVote = true;
                    Check(!instance.confirmedVote || settled, "no vote may depend on a predicted terminal state");
                    playersReady = playersReady && instance.confirmedVote;
                }
                // The spectator's confirmed getter measures receipt, so its
                // own simulated state must reach the terminal boundary too.
                if (playersReady && instances[2].state.frame == TerminalFrame) break;
                ++iteration;
                Sleep(1);
            }
            if (!failed) {
                const State expected = Expected(epoch, delay, retractionCase);
                for (Instance& instance : instances) {
                    Context context(instance);
                    Check(Same(instance.state, expected), "every endpoint matches independently replayed inputs");
                    totalRollbackFrames += instance.rollbackFrames;
                }
                Check(instances[2].saves == 0 && instances[2].loads == 0,
                    "spectator consumes confirmed inputs without save/load callbacks");
                Check(instances[0].loads + instances[1].loads > 0,
                    "withheld pumping exercises real rollback in every epoch");
                Check(sawSpeculativeTerminal, "terminal barrier is exercised with unconfirmed final input");
                if (retractionCase) {
                    Check(sawProvisionalKo && sawKoRetracted,
                        "networked predicted knockout is held, retracted, and play resumes");
                    retractionPassed = sawProvisionalKo && sawKoRetracted;
                    std::cout << "predicted knockout at input " << ProvisionalKoInputFrame
                        << " retracted through GGPO rollback; no provisional vote, play resumed.\n";
                }
                speculativeTerminals += sawSpeculativeTerminal ? 1 : 0;
                std::cout << "epoch " << epoch << ", delay " << delay << ": players and spectator at frame "
                    << TerminalFrame << ", rollback frames " << instances[0].rollbackFrames + instances[1].rollbackFrames
                    << ", hash " << expected.hash << '\n';
            }
            CloseAll(instances);
            // Restore is deliberately separated from starting the next epoch.
            for (Instance& instance : instances) instance.state = instance.baseline;
        }
    }
    CloseAll(instances);
    WSACleanup();
    if (failed) return 1;
    Check(epoch == 2 * EpochsPerDelay + 1 && speculativeTerminals == epoch && retractionPassed,
        "all delay configurations and epochs ran");
    if (failed) return 1;
    std::cout << "GGPO rematch integration passed: " << epoch << " epochs, "
        << totalRollbackFrames << " rollback frames, reused UDP ports, independent baseline retained.\n";
    return 0;
}
