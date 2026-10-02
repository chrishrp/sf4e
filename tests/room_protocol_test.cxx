#include "../src/session/sf4e__SessionProtocol.hxx"

#include <cstdlib>
#include <iostream>

namespace {
	void Require(bool ok, const char* description) {
		if (!ok) {
			std::cerr << "FAIL: " << description << '\n';
			std::exit(1);
		}
	}
}

int main() {
	namespace P = sf4e::SessionProtocol;
	P::SessionDataUpdate update;
	update.lobbyData = P::LobbyData::NULL_LOBBY;
	update.lobbyData.roomScoresAvailable = true;
	update.lobbyData.instantRematchAvailable = true;
	P::MemberData alice = {};
	alice.connId = { "test", "alice" };
	alice.name = "Alice";
	alice.roomMemberId = 17;
	alice.roomScore.wins = 3;
	alice.roomScore.losses = 2;
	update.lobbyData.members.push_back(alice);
	update.matchData.matchId = 43;
	update.matchData.charaMemberId[0] = 17;
	update.matchData.chara[0].charaID = 0; // A real, unready Ryu selection.

	nlohmann::json wire = update;
	P::SessionDataUpdate read = wire.get<P::SessionDataUpdate>();
	Require(read.lobbyData.roomScoresAvailable, "server score capability survives broadcast");
	Require(read.lobbyData.instantRematchAvailable, "instant rematch capability survives broadcast");
	Require(read.lobbyData.members[0].roomMemberId == 17, "member identity survives broadcast");
	Require(read.lobbyData.members[0].roomScore.wins == 3 && read.lobbyData.members[0].roomScore.losses == 2,
		"spectator/player update contains identical room totals");
	Require(read.matchData.matchId == 43, "active match ID survives broadcast");
	Require(read.matchData.charaMemberId[0] == 17 && read.matchData.chara[0].charaID == 0,
		"Ryu's live pick is distinguishable from no selection");

	// Strip only the newly introduced fields, as an upstream server would.
	wire["lobbyData"].erase("roomScoresAvailable");
	wire["lobbyData"].erase("instantRematchAvailable");
	wire["lobbyData"]["members"][0].erase("roomMemberId");
	wire["lobbyData"]["members"][0].erase("roomScore");
	wire["matchData"].erase("matchId");
	wire["matchData"].erase("charaMemberId");
	wire.get_to(read);
	Require(!read.lobbyData.roomScoresAvailable, "upstream server does not pretend to provide scores");
	Require(!read.lobbyData.instantRematchAvailable, "older servers cannot accidentally enable instant rematch");
	Require(read.lobbyData.members[0].roomMemberId == 0 && read.lobbyData.members[0].roomScore.wins == 0,
		"old member data resets new identity and score fields");
	Require(read.matchData.matchId == 0 && read.matchData.charaMemberId[0] == 0,
		"old match data clears any retained match identity");

	P::LobbyReportResults result;
	result.matchId = 43;
	result.loserSide = -1;
	P::LobbyReportResults resultRead = nlohmann::json(result).get<P::LobbyReportResults>();
	Require(resultRead.matchId == 43 && resultRead.loserSide == -1, "draw report preserves match identity");
	resultRead = nlohmann::json{ {"type", "lobby_reportresults"}, {"loserSide", 1} }.get<P::LobbyReportResults>();
	Require(resultRead.matchId == 0, "legacy report has no authority over an identified match");

	P::DesyncReport desync;
	desync.matchId = 43;
	desync.gameplay = true;
	Require(nlohmann::json(desync).get<P::DesyncReport>().matchId == 43, "desync is tied to its match");
	P::InstantRematchRequest request;
	request.matchId = 43; request.resultFrame = 900; request.loserSide = 0;
	const auto requestRead = nlohmann::json(request).get<P::InstantRematchRequest>();
	Require(requestRead.matchId == 43 && requestRead.resultFrame == 900 && requestRead.loserSide == 0,
		"rematch request carries epoch and confirmed outcome");
	P::InstantRematchEvent event;
	event.action = P::IR_START; event.previousMatchId = 43; event.nextMatchId = 44;
	event.resultFrame = 900; event.loserSide = 0;
	const auto eventRead = nlohmann::json(event).get<P::InstantRematchEvent>();
	Require(eventRead.action == P::IR_START && eventRead.previousMatchId == 43 && eventRead.nextMatchId == 44,
		"start announcement scopes both old and new epochs");
	P::InstantRematchCancel cancel; cancel.matchId = 43; cancel.restartPending = true;
	Require(nlohmann::json(cancel).get<P::InstantRematchCancel>().restartPending,
		"startup cancellation is distinct from declining a later rematch");
	P::BattleLoaded loaded; loaded.matchId = 44;
	Require(nlohmann::json(loaded).get<P::BattleLoaded>().matchId == 44, "loaded barrier is epoch scoped");
	Require(nlohmann::json{ {"type", "battle_loaded"} }.get<P::BattleLoaded>().matchId == 0,
		"old loaded message cannot masquerade as new epoch");
	P::BattleSnapshot snapshot = {};
	snapshot.matchId = 44;
	Require(nlohmann::json(snapshot).get<P::BattleSnapshot>().matchId == 44, "diagnostic snapshots are epoch scoped");
	update.matchData.Clear();
	Require(update.matchData.matchId == 0 && update.matchData.charaMemberId[0] == 0,
		"room reset clears active match and selection identities");
	std::cout << "Room protocol tests passed: scores, identities, legacy defaults and inconclusive results.\n";
	return 0;
}
