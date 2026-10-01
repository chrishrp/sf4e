#pragma once
#include <string>
#include <imgui.h>

namespace sf4e { namespace LobbyView {
// A pure rendering model, shared by the game and the standalone visual check.
struct Player {
    std::string name = "Waiting for opponent";
    int character = -1;
    int ultra = 0;
    bool present = false, ready = false, local = false;
    unsigned long long wins = 0, losses = 0;
};
struct Model {
    Player players[2];
    std::string code, watchers;
    std::string spectatorStatus = "WAITING FOR THE PLAYERS TO READY UP";
    bool publicRoom = false, spectator = false, ready = false, scoresAvailable = false;
    int character = 0, ultra = 0, costume = 0, color = 0;
    std::string edition = "ULTRA";
    int editionId = 14;
    int stage = 0, proposedStage = 0, side = 0;
    int focusRow = 0, characterCursor = 0, optionCursor = 0, actionCursor = 0;
};
struct Fonts { ImFont* title; ImFont* head; ImFont* body; ImFont* caption; };
struct Hit { int row = -1; int index = -1; bool activate = false; };
// Draw at a fixed 1600x1000 canvas; the caller scales vertices and mouse coords.
Hit Draw(ImDrawList* dl, const Fonts& fonts, const Model& model, ImVec2 mouse, bool click);
} }
