#pragma once

#include <cstdint>
#include <ggponet.h>

namespace sf4e {

// The game can still be behind its loading barrier while GGPO is waiting for
// its first peer packet. This deadline belongs to the application pump, not
// simulation frames or the game's battle-update permission.
class NetplayStartupWatchdog {
public:
    void Arm(uint64_t now) { startedAt_ = now; armed_ = true; running_ = false; }
    void Reset() { armed_ = false; running_ = false; }
    void MarkRunning() { running_ = true; }
    bool Running() const { return running_; }
    bool Expired(uint64_t now) const {
        return armed_ && !running_ && now - startedAt_ >= 30000;
    }

private:
    uint64_t startedAt_ = 0;
    bool armed_ = false;
    bool running_ = false;
};

// Network games require one local side and one remote side. Two local slots
// are only valid for the separate GGPO sync-test entry point.
inline bool ValidNetplayPlayerSlots(const GGPOPlayer* players, int count) {
    if (!players || count < 2 || players[0].player_num != 1 || players[1].player_num != 2)
        return false;
    return (players[0].type == GGPO_PLAYERTYPE_LOCAL && players[1].type == GGPO_PLAYERTYPE_REMOTE)
        || (players[0].type == GGPO_PLAYERTYPE_REMOTE && players[1].type == GGPO_PLAYERTYPE_LOCAL);
}

}
