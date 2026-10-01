#pragma once

#include <cstdint>

namespace sf4e {
	struct RoomScore {
		uint64_t wins = 0;
		uint64_t losses = 0;
	};

	// The server owns the score and freezes the players at match start. A side
	// number alone is insufficient: winner-stays can swap the seats before a
	// duplicate or delayed result arrives. Member IDs identify one room visit,
	// independent of display name, chosen character and current seat.
	class RoomScoreTracker {
	public:
		uint64_t Begin(uint64_t player0, uint64_t player1) {
			if (_activeMatch != 0 || player0 == 0 || player1 == 0 || player0 == player1) return 0;
			_activeMatch = ++_lastMatch;
			_inconclusive = false;
			_players[0] = player0;
			_players[1] = player1;
			return _activeMatch;
		}

		uint64_t ActiveMatchId() const { return _activeMatch; }
		void MarkInconclusive() { if (_activeMatch != 0) _inconclusive = true; }
		bool IsInconclusive() const { return _inconclusive; }

		bool HasPlayers(uint64_t player0, uint64_t player1) const {
			return _players[0] == player0 && _players[1] == player1;
		}

		void Cancel() {
			_activeMatch = 0;
			_players[0] = _players[1] = 0;
			_inconclusive = false;
		}

		// Only the original P1 reports. -1 ends an inconclusive match without
		// changing scores; all other values except side 0/1 are rejected.
		bool Finish(uint64_t matchId, uint64_t reporter, int loserSide,
			uint64_t player0, uint64_t player1, RoomScore& score0, RoomScore& score1) {
			if (_activeMatch == 0 || matchId != _activeMatch || reporter != _players[0]
				|| !HasPlayers(player0, player1) || loserSide < -1 || loserSide > 1) return false;
			if (!_inconclusive && loserSide == 0) { ++score0.losses; ++score1.wins; }
			if (!_inconclusive && loserSide == 1) { ++score1.losses; ++score0.wins; }
			Cancel();
			return true;
		}

	private:
		uint64_t _lastMatch = 0;
		uint64_t _activeMatch = 0;
		uint64_t _players[2] = { 0, 0 };
		bool _inconclusive = false;
	};
}
