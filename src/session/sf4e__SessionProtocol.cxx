#include <cstdio>
#include <cstring>
#include <string>

// The chara-conditions type comes from the header, which aliases the game's
// own type on Windows and declares a layout-compatible copy elsewhere. Pulling
// Dimps__GameEvents.hxx in here as well would drag <windows.h> (and, through
// Dimps__Platform.hxx, <d3d9.h>) into the Linux server build.
#include "sf4e__SessionProtocol.hxx"

namespace sf4e {
	namespace SessionProtocol {
		bool ConnectionID::operator==(const ConnectionID& rhs) {
			return this->host == rhs.host && this->user == rhs.user;
		}

		bool LobbyID::operator==(const LobbyID& rhs) {
			if (this->host == "" || this->key == "") {
				return rhs.host == "" || rhs.key == "";
			}

			return this->host == rhs.host && this->key == rhs.key;
		}

		const LobbyID LobbyID::NULL_LOBBY_ID = { "", "" };
		const LobbyData LobbyData::NULL_LOBBY = {
			LobbyID::NULL_LOBBY_ID,
			false,
			0,
			{0, 0},
			{}
		};

		bool CharaConditionsValid(const CharaConditions& c) {
			return c.charaID < CHARA_COUNT
				&& c.costume < COSTUME_COUNT
				&& c.color < COLOR_COUNT
				&& c.ultraCombo < ULTRA_COUNT
				&& c.handicap == 0;
		}

		bool StageIDValid(int64_t stageID) {
			return stageID >= 0 && stageID < STAGE_COUNT;
		}

		bool SanitizeCharaConditions(CharaConditions& c) {
			bool changed = false;
			if (c.charaID >= CHARA_COUNT) { c.charaID = 0; changed = true; }
			if (c.costume >= COSTUME_COUNT) { c.costume = 0; changed = true; }
			if (c.color >= COLOR_COUNT) { c.color = 0; changed = true; }
			if (c.ultraCombo >= ULTRA_COUNT) { c.ultraCombo = 0; changed = true; }
			if (c.handicap != 0) { c.handicap = 0; changed = true; }
			return changed;
		}

		MatchData::MatchData()
		{
			inputDelay[0] = inputDelay[1] = -1;
			Clear();
		}

		void MatchData::Clear() {
			inputDelay[0] = inputDelay[1] = -1;
			readyMessageNum[0] = -1;
			readyMessageNum[1] = -1;
			stageID = -1;
			rngSeed = 0xffffffff;
			// sizeof the array itself: the element type is the game type on Windows and
			// a layout-compatible copy elsewhere, so naming it here would not port.
			memset(chara, 0, sizeof(chara));
		}

		bool MatchData::IsAllReady() {
			return (
				readyMessageNum[0] != -1 &&
				readyMessageNum[1] != -1
			);
		}
		static std::string FormatFP(const FixedPoint& v) {
			char b[24];
			snprintf(b, sizeof(b), "%d+%u/65536", (int)v.integral, (unsigned)v.fractional);
			return b;
		}

		std::string DescribeSnapshotDiff(const StateSnapshot& mine, const StateSnapshot& theirs) {
			std::string out;
			if (mine.battleFlow != theirs.battleFlow) {
				out += "battleFlow a=" + std::to_string(mine.battleFlow) + " b=" + std::to_string(theirs.battleFlow);
			}
			if (mine.battleFlowSubstate != theirs.battleFlowSubstate) {
				if (!out.empty()) out += ", ";
				out += "battleFlowSubstate a=" + std::to_string(mine.battleFlowSubstate) + " b=" + std::to_string(theirs.battleFlowSubstate);
			}
			for (int i = 0; i < 2; i++) {
				const StateSnapshot::CharaStateSnapshot& m = mine.chara[i];
				const StateSnapshot::CharaStateSnapshot& r = theirs.chara[i];
				auto add = [&](const char* name, const std::string& mv, const std::string& rv) {
					if (mv == rv) return;
					if (!out.empty()) out += ", ";
					out += "P" + std::to_string(i + 1) + "." + name + " a=" + mv + " b=" + rv;
				};
				add("status", std::to_string(m.status), std::to_string(r.status));
				add("side", std::to_string(m.side), std::to_string(r.side));
				for (int k = 0; k < 4; k++) {
					if (m.rootPos[k] != r.rootPos[k]) {
						char nm[16], mv[32], rv[32];
						snprintf(nm, sizeof(nm), "rootPos[%d]", k);
						snprintf(mv, sizeof(mv), "%.6f", m.rootPos[k]);
						snprintf(rv, sizeof(rv), "%.6f", r.rootPos[k]);
						add(nm, mv, rv);
					}
				}
				#define FPF(x) add(#x, FormatFP(m.x), FormatFP(r.x))
				FPF(vit); FPF(vitmax); FPF(revenge); FPF(revengemax);
				FPF(recoverable); FPF(recoverablemax); FPF(super); FPF(supermax);
				FPF(sctimeamt); FPF(sctimemax); FPF(uctime); FPF(uctimemax);
				FPF(damage); FPF(combodamage);
				FPF(actionFrame); FPF(timeScale);
				add("action", std::to_string(m.action), std::to_string(r.action));
				add("posture", std::to_string(m.posture), std::to_string(r.posture));
				#undef FPF
			}
			// Soak-test diagnostic: which part of the GameManager forked. An
			// offset >= GM_SAVED_BYTES means the diverging round/match state
			// lives PAST what the save state copies, i.e. it is never restored
			// on a rollback -- the suspected cause of the round-end desync.
			{
				std::string gm;
				int shown = 0;
				for (size_t c = 0; c < GM_MAX_CHUNKS; c++) {
					if (mine.gmChunks[c] == theirs.gmChunks[c]) continue;
					if (shown++ >= 8) { gm += ", ..."; break; }
					size_t off = c * GM_CHUNK_BYTES;
					if (!gm.empty()) gm += ", ";
					gm += "+" + std::to_string(off);
					if (off >= GM_SAVED_BYTES) gm += "(UNSAVED!)";
				}
				if (!gm.empty()) {
					if (!out.empty()) out += ", ";
					out += "GameManager chunks differ at " + gm;
				}
			}
			if (out.empty()) {
				out = "(the compared fields all match; the drift is in state the snapshot does not cover)";
			}
			return out;
		}

		// Did the two machines disagree about WHERE IN THE MATCH they are,
		// rather than about what happened in it? This never ends a match on its
		// own -- only gameplay state does that -- but it separates the two
		// causes in the log, and they need completely different fixes.
		//
		// A peer running the game at a different frame-rate setting advances
		// the battle flow by a different amount per frame, so it diverges here
		// first while both characters still agree. That is the single most
		// common desync players hit, and until now the report could not say so.
		bool SnapshotFlowDiffers(const StateSnapshot& a, const StateSnapshot& b) {
			return a.battleFlow != b.battleFlow
				|| a.battleFlowSubstate != b.battleFlowSubstate;
		}

		bool SnapshotGameplayDiffers(const StateSnapshot& a, const StateSnapshot& b) {
			for (int i = 0; i < 2; i++) {
				const StateSnapshot::CharaStateSnapshot& m = a.chara[i];
				const StateSnapshot::CharaStateSnapshot& r = b.chara[i];
				if (m.status != r.status) return true;
				if (m.side != r.side) return true;
				#define FPD(x) if (m.x.integral != r.x.integral || m.x.fractional != r.x.fractional) return true
				FPD(vit); FPD(vitmax); FPD(revenge); FPD(revengemax);
				FPD(recoverable); FPD(recoverablemax); FPD(super); FPD(supermax);
				FPD(sctimeamt); FPD(sctimemax); FPD(uctime); FPD(uctimemax);
				FPD(damage); FPD(combodamage);
				FPD(actionFrame); FPD(timeScale);
				#undef FPD
				if (m.action != r.action) return true;
				if (m.posture != r.posture) return true;
				#define FPD(x)
				#undef FPD
				// rootPos is intentionally not checked: a derived render float.
			}
			return false;
		}
	}
}
