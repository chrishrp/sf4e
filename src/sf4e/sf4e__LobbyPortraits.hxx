#pragma once

#include <imgui.h>

struct IDirect3DDevice9;

namespace sf4e {
	namespace LobbyPortraits {
		// Call on the render thread once when the overlay initializes. The atlas
		// is loaded from assets/lobby/portraits.png beside Sidecar.dll. Returns
		// false on missing/invalid assets or a device that cannot load them.
		bool Load(IDirect3DDevice9* device);

		// Call on the render thread when the overlay shuts down. Managed D3D9
		// textures survive Reset, so no reset-time reload is needed.
		void Release();

		// IDs 0..43 map to an eight-column, six-row atlas. The helper maps the
		// generated atlas's Abel/Viper/Rufus/El Fuerte order to game IDs 12..15.
		// Returns false without drawing when unavailable, allowing a text or
		// silhouette fallback. The texture must outlive the submitted draw list.
		bool Draw(ImDrawList* drawList, int id, ImVec2 min, ImVec2 max,
			ImU32 tint = IM_COL32(255, 255, 255, 255));
	}
}
