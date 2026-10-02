#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>

namespace sf4e {

// A fresh GGPO epoch may reuse loaded game resources only after every retained
// participant reaches the same confirmed result, then restores its local start
// state. The server coordinates this barrier; it never handles game snapshots.
class InstantRematch {
public:
    enum Action { None, Prepare, Start, Abort };
    enum Phase { Disabled, Waiting, Collecting, Preparing, Started, Aborted };

    void Reset() { *this = InstantRematch(); }

    bool Begin(uint64_t matchId, const std::vector<uint64_t>& participants) {
        Reset();
        if (!matchId || participants.size() < 2 || participants.size() > 16) return false;
        for (std::size_t i = 0; i < participants.size(); ++i) {
            if (!participants[i]) return false;
            for (std::size_t j = 0; j < i; ++j) if (participants[i] == participants[j]) return false;
        }
        _matchId = matchId;
        _participants = participants;
        _ready.assign(participants.size(), false);
        _acked.assign(participants.size(), false);
        _phase = Waiting;
        return true;
    }

    // Players submit only after choosing Rematch. Spectators submit as soon as
    // they reach the confirmed result. A spectator alone starts no deadline.
    Action Request(uint64_t matchId, uint64_t memberId, int resultFrame, int loserSide, uint64_t nowMs) {
        const std::size_t i = Index(memberId);
        if (matchId != _matchId || i == _participants.size() || resultFrame < 0
            || loserSide < -1 || loserSide > 1 || (_phase != Waiting && _phase != Collecting)) return None;
        if (_phase == Collecting && nowMs >= _deadline) return Fail("vote_timeout");
        if (_resultFrame >= 0 && (_resultFrame != resultFrame || _loserSide != loserSide))
            return Fail("result_mismatch");
        _resultFrame = resultFrame;
        _loserSide = loserSide;
        _ready[i] = true;
        if (i < 2 && _phase == Waiting) { _phase = Collecting; _deadline = nowMs + 60000; }
        for (bool ready : _ready) if (!ready) return None;
        _phase = Preparing;
        _deadline = nowMs + 15000;
        return Prepare;
    }

    Action Ack(uint64_t matchId, uint64_t memberId, uint64_t nowMs) {
        if (_phase != Preparing || matchId != _matchId) return None;
        const std::size_t i = Index(memberId);
        if (i == _participants.size()) return None;
        if (nowMs >= _deadline) return Fail("prepare_timeout");
        _acked[i] = true;
        for (bool acked : _acked) if (!acked) return None;
        _phase = Started;
        _deadline = 0;
        return Start;
    }

    Action Cancel(uint64_t matchId, uint64_t memberId) {
        if (matchId != _matchId || !Contains(memberId)) return None;
        return Fail("cancelled");
    }

    Action Tick(uint64_t nowMs) {
        if ((_phase == Collecting || _phase == Preparing) && nowMs >= _deadline)
            return Fail(_phase == Preparing ? "prepare_timeout" : "vote_timeout");
        return None;
    }

    Action Fail(const char* reason) {
        if (_phase == Disabled || _phase == Aborted || _phase == Started) return None;
        _phase = Aborted;
        _reason = reason;
        _deadline = 0;
        return Abort;
    }

    bool Contains(uint64_t memberId) const { return Index(memberId) != _participants.size(); }
    Phase State() const { return _phase; }
    uint64_t MatchId() const { return _matchId; }
    int ResultFrame() const { return _resultFrame; }
    int LoserSide() const { return _loserSide; }
    const char* Reason() const { return _reason; }
    const std::vector<uint64_t>& Participants() const { return _participants; }

private:
    std::size_t Index(uint64_t memberId) const {
        for (std::size_t i = 0; i < _participants.size(); ++i) if (_participants[i] == memberId) return i;
        return _participants.size();
    }
    Phase _phase = Disabled;
    uint64_t _matchId = 0;
    uint64_t _deadline = 0;
    int _resultFrame = -1;
    int _loserSide = -2;
    const char* _reason = "";
    std::vector<uint64_t> _participants;
    std::vector<bool> _ready, _acked;
};

// ACK and Start cross in flight with a startup failure. Keep only the most
// recent committed transition, briefly, so its old ID can still cancel the
// newly allocated but unplayed epoch. The caller explicitly distinguishes a
// startup failure from declining a rematch after a later completed game.
class InstantRematchCommit {
public:
    void Reset() { *this = InstantRematchCommit(); }
    void Begin(uint64_t previous, uint64_t next, const std::vector<uint64_t>& participants, uint64_t nowMs) {
        _previous = previous; _next = next; _participants = participants; _deadline = nowMs + 45000;
    }
    bool CanCancel(uint64_t matchId, uint64_t memberId, bool startupFailure, uint64_t nowMs) const {
        if (!startupFailure || !_previous || _next <= _previous || nowMs >= _deadline
            || (matchId != _previous && matchId != _next)) return false;
        for (uint64_t participant : _participants) if (participant == memberId) return true;
        return false;
    }
    uint64_t Previous() const { return _previous; }
    uint64_t Next() const { return _next; }
    const std::vector<uint64_t>& Participants() const { return _participants; }
private:
    uint64_t _previous = 0, _next = 0, _deadline = 0;
    std::vector<uint64_t> _participants;
};
}
