#include "../src/session/sf4e__RoomScore.hxx"

#include <cstdlib>
#include <iostream>

namespace {
	void Require(bool ok, const char* description) {
		if (!ok) {
			std::cerr << "FAIL: " << description << '\n';
			std::exit(1);
		}
	}

	void CheckScore(const sf4e::RoomScore& score, uint64_t wins, uint64_t losses) {
		Require(score.wins == wins && score.losses == losses, "score belongs to the member, not the seat");
	}
}

int main() {
	sf4e::RoomScoreTracker room;
	sf4e::RoomScore alice, bob, carol;
	const uint64_t aliceId = 101, bobId = 202, carolId = 303;

	Require(!room.Finish(0, aliceId, 1, aliceId, bobId, alice, bob), "cannot report before a match");
	Require(room.Begin(0, bobId) == 0, "empty seat cannot start a match");
	Require(room.Begin(aliceId, aliceId) == 0, "one member cannot fill both seats");
	const uint64_t first = room.Begin(aliceId, bobId);
	Require(first != 0, "match has a server ID");
	Require(room.Begin(aliceId, bobId) == 0, "duplicate ready does not replace active match");
	Require(!room.Finish(first, bobId, 1, aliceId, bobId, alice, bob), "P2 cannot report a second result");
	Require(!room.Finish(first, carolId, 1, aliceId, bobId, alice, bob), "spectator cannot report");
	Require(!room.Finish(first, aliceId, 2, aliceId, bobId, alice, bob), "invalid loser side rejected");
	Require(!room.Finish(first, aliceId, -2, aliceId, bobId, alice, bob), "invalid negative side rejected");
	Require(!room.Finish(first, aliceId, 1, bobId, aliceId, bob, alice), "seat changes cannot rewrite result identity");
	Require(!room.Finish(first, aliceId, 1, aliceId, carolId, alice, carol), "replacement opponent cannot inherit a loss");
	CheckScore(alice, 0, 0);
	CheckScore(bob, 0, 0);

	// Bob wins from P2. The server will move him to P1 with his member record.
	Require(room.Finish(first, aliceId, 0, aliceId, bobId, alice, bob), "original P1 reports Bob's win");
	CheckScore(alice, 0, 1);
	CheckScore(bob, 1, 0);
	Require(!room.Finish(first, aliceId, 0, aliceId, bobId, alice, bob), "duplicate result is idempotent");

	const uint64_t second = room.Begin(bobId, aliceId);
	Require(second > first, "rematch has a new ID");
	Require(!room.Finish(first, bobId, 1, bobId, aliceId, bob, alice), "delayed result cannot end rematch");
	Require(room.ActiveMatchId() == second, "stale result preserves active rematch");
	Require(room.Finish(second, bobId, 1, bobId, aliceId, bob, alice), "winner can win again from P1");
	CheckScore(alice, 0, 2);
	CheckScore(bob, 2, 0);

	const uint64_t draw = room.Begin(bobId, aliceId);
	Require(room.Finish(draw, bobId, -1, bobId, aliceId, bob, alice), "draw clears readiness entitlement");
	CheckScore(alice, 0, 2);
	CheckScore(bob, 2, 0);

	const uint64_t forked = room.Begin(bobId, aliceId);
	room.MarkInconclusive();
	Require(room.IsInconclusive(), "desync flags the current match");
	Require(room.Finish(forked, bobId, 1, bobId, aliceId, bob, alice), "desync closes without awarding reported win");
	CheckScore(alice, 0, 2);
	CheckScore(bob, 2, 0);

	const uint64_t abandoned = room.Begin(bobId, aliceId);
	room.Cancel();
	Require(!room.Finish(abandoned, bobId, 1, bobId, aliceId, bob, alice), "disconnect result ignored");
	const uint64_t replacement = room.Begin(bobId, carolId);
	Require(replacement > abandoned, "new member match has a fresh ID");
	Require(!room.Finish(abandoned, bobId, 1, bobId, carolId, bob, carol), "old result cannot count against new member");
	Require(room.Finish(replacement, bobId, 0, bobId, carolId, bob, carol), "new opponent's valid win counts");
	CheckScore(bob, 2, 1);
	CheckScore(carol, 1, 0);
	CheckScore(alice, 0, 2);

	// A room reset clears member totals separately, but IDs are never reused.
	room.Cancel();
	alice = sf4e::RoomScore();
	bob = sf4e::RoomScore();
	const uint64_t reset = room.Begin(aliceId, bobId);
	Require(reset > replacement, "room reuse does not recycle match IDs");
	Require(!room.Finish(first, aliceId, 1, aliceId, bobId, alice, bob), "previous room result cannot enter reset room");
	CheckScore(alice, 0, 0);
	CheckScore(bob, 0, 0);
	std::cout << "Room score tests passed: authority, rotation, rematch, draw, desync, disconnect, stale and duplicate reports.\n";
	return 0;
}
