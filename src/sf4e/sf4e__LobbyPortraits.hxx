#pragma once

#include <imgui.h>

struct IDirect3DDevice9;

namespace sf4e {
	namespace LobbyPortraits {
		// Read official portraits and roster icons from the installed game.
		// The default root is the host executable's folder (SSFIV.exe); the
		// standalone preview can pass --game-dir explicitly. Returns true only
		// when all 44 portraits and 44 icons load. Partial loads remain usable.
		bool Load(IDirect3DDevice9* device, const wchar_t* gameDirectory = nullptr);
		int LoadedCount();
		int IconCount();

		// Call on the render thread when the overlay shuts down. Managed D3D9
		// textures survive Reset, so no reset-time reload is needed.
		void Release();

		// IDs 0..43 use actual game codes. The roster uses the game's small
		// face icons; player cards use the original painted select-screen art.
		// Returns false without drawing when unavailable, allowing a text or
		// silhouette fallback. The texture must outlive the submitted draw list.
		bool Draw(ImDrawList* drawList, int id, ImVec2 min, ImVec2 max,
			bool rosterIcon = false, ImU32 tint = IM_COL32(255, 255, 255, 255));
	}
}
