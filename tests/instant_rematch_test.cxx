#include "../src/session/sf4e__InstantRematch.hxx"
#include "../src/session/sf4e__RoomScore.hxx"

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {
void Require(bool ok, const char* description) {
    if (!ok) { std::cerr << "FAIL: " << description << '\n'; std::exit(1); }
}
}

int main() {
    using R = sf4e::InstantRematch;
    R rematch;
    const std::vector<uint64_t> group = { 10, 20, 30 }; // P1, P2, retained spectator.
    Require(!rematch.Begin(0, group), "zero epoch cannot restart");
    Require(!rematch.Begin(1, {10, 10}), "participants must have distinct identities");
    Require(!rematch.Begin(1, {10}), "two players required");
    Require(rematch.Begin(41, group), "freeze original participant identities");
    Require(rematch.Request(40, 10, 500, 1, 100) == R::None, "stale request ignored");
    Require(rematch.Request(41, 40, 500, 1, 100) == R::None, "late spectator cannot vote");
    Require(rematch.Request(41, 10, -1, 1, 100) == R::None, "unconfirmed frame rejected");
    Require(rematch.Request(41, 10, 500, -2, 100) == R::None, "invalid result rejected");
    Require(rematch.Ack(41, 10, 100) == R::None, "ack before prepare cannot skip voting");
    Require(rematch.Request(41, 30, 500, 1, 100) == R::None, "spectator announces readiness");
    Require(rematch.Tick(100000) == R::None, "spectator cannot time out players before a vote");
    Require(rematch.Request(41, 10, 500, 1, 100000) == R::None, "first player alone cannot restart");
    Require(rematch.Request(41, 10, 500, 1, 100001) == R::None, "duplicate vote cannot stand in for opponent");
    Require(rematch.Request(41, 20, 500, 1, 100002) == R::Prepare, "matching votes plus spectator ready prepare once");
    Require(rematch.Request(41, 20, 500, 1, 100003) == R::None, "duplicate request cannot prepare twice");
    Require(rematch.Ack(40, 20, 100003) == R::None, "old acknowledgment ignored");
    Require(rematch.Ack(41, 40, 100003) == R::None, "late spectator acknowledgment ignored");
    Require(rematch.Ack(41, 10, 100003) == R::None, "one restored player cannot start");
    Require(rematch.Ack(41, 10, 100004) == R::None, "duplicate acknowledgment cannot satisfy another participant");
    Require(rematch.Ack(41, 20, 100005) == R::None, "retained spectator must restore too");
    Require(rematch.Ack(41, 30, 100006) == R::Start, "all retained acknowledgments start once");
    Require(rematch.Ack(41, 30, 100007) == R::None, "duplicate final acknowledgment cannot start twice");

    // Publication is deferred until Start. Keeping the players fixed permits
    // local baseline reuse; the scoreboard lives outside restored game memory.
    sf4e::RoomScoreTracker scores;
    sf4e::RoomScore alice, bob;
    const uint64_t first = scores.Begin(10, 20);
    Require(rematch.Begin(first, {10, 20}), "first match coordinator");
    Require(rematch.Request(first, 10, 700, 0, 0) == R::None, "first confirmed vote");
    Require(alice.losses == 0 && bob.wins == 0, "pending votes do not change score");
    Require(rematch.Request(first, 20, 700, 0, 1) == R::Prepare, "both players prepare");
    Require(rematch.Ack(first, 10, 2) == R::None && rematch.Ack(first, 20, 3) == R::Start, "start barrier complete");
    Require(scores.Finish(first, 10, rematch.LoserSide(), 10, 20, alice, bob), "old result scores exactly once");
    Require(alice.losses == 1 && bob.wins == 1, "P2 win counted with unchanged seats");
    const uint64_t next = scores.Begin(10, 20);
    Require(next > first && rematch.Begin(next, {10, 20}), "next epoch preserves players and has new identity");
    Require(rematch.Cancel(first, 10) == R::None, "late old cancel cannot stop new game");
    Require(!scores.Finish(first, 10, 0, 10, 20, alice, bob), "late normal teardown cannot count old game twice");
    Require(scores.Finish(next, 10, -1, 10, 20, alice, bob), "failed restart cancels new game without a loss");
    Require(alice.losses == 1 && bob.wins == 1, "failed new epoch preserves previous result");

    Require(rematch.Begin(50, group), "reset for incomplete spectator");
    Require(rematch.Request(50, 10, 800, -1, 0) == R::None && rematch.Request(50, 20, 800, -1, 1) == R::None,
        "players wait for retained spectator reaching result");
    Require(rematch.Tick(59999) == R::None && rematch.Tick(60000) == R::Abort, "vote/readiness deadline bounded");
    Require(std::strcmp(rematch.Reason(), "vote_timeout") == 0, "timeout reason retained");
    Require(rematch.Request(50, 30, 800, -1, 60001) == R::None, "late spectator cannot revive aborted epoch");
    Require(rematch.Begin(51, {10, 20}), "reset for result disagreement");
    rematch.Request(51, 10, 900, 0, 0);
    Require(rematch.Request(51, 20, 901, 0, 1) == R::Abort, "different final frames cannot restart");
    Require(rematch.Begin(52, {10, 20}), "reset for winner disagreement");
    rematch.Request(52, 10, 900, 0, 0);
    Require(rematch.Request(52, 20, 900, 1, 1) == R::Abort, "different results cannot restart");
    Require(rematch.Begin(53, {10, 20}), "reset for prepare timeout");
    rematch.Request(53, 10, 900, -1, 0);
    Require(rematch.Request(53, 20, 900, -1, 1) == R::Prepare, "draw can rematch");
    Require(rematch.Ack(53, 10, 2) == R::None && rematch.Ack(53, 20, 15001) == R::Abort,
        "ack arriving on deadline cannot race timeout");
    Require(rematch.Begin(54, group), "reset for cancellation before vote");
    Require(rematch.Cancel(54, 40) == R::None, "late spectator cannot cancel");
    Require(rematch.Cancel(54, 20) == R::Abort, "change-character releases whole group before anyone votes");
    Require(rematch.Cancel(54, 10) == R::None, "abort is idempotent");
    Require(rematch.Begin(55, group), "reset for lost participant");
    Require(rematch.Fail("participant_left") == R::Abort, "retained participant leaving cancels restoration");

    sf4e::InstantRematchCommit committed;
    committed.Begin(60, 61, group, 100);
    Require(committed.CanCancel(60, 10, true, 101), "startup cancellation can cross committed Start using old ID");
    Require(committed.CanCancel(61, 20, true, 101), "startup failure after consuming Start uses new ID");
    Require(committed.CanCancel(61, 30, true, 101), "retained spectator startup failure releases whole group");
    Require(!committed.CanCancel(60, 40, true, 101), "late spectator cannot abort committed restart");
    Require(!committed.CanCancel(59, 10, true, 101), "older transaction cannot abort new game");
    Require(!committed.CanCancel(61, 10, false, 101), "later ordinary result exit retains scoring entitlement");
    Require(!committed.CanCancel(60, 10, true, 45100), "cross-epoch cancellation window is bounded");
    committed.Reset();
    Require(!committed.CanCancel(61, 10, true, 101), "closed transaction cannot cancel another game");
    std::cout << "Instant rematch tests passed: consensus, epochs, spectators, deadlines, cancellation and scores.\n";
}
