#pragma once

// windows.h is not actually used by anything in this header, and the Linux
// build of the lobby server needs FixedPoint. Keep it on Windows so existing
// translation units that leaned on the transitive include still compile.
#ifdef _WIN32
#include <windows.h>
#endif
#include <nlohmann/json.hpp>

namespace Dimps {
	namespace Math {
		struct FixedPoint {
			unsigned short fractional;
			short integral; // Might be unsigned? Not entirely sure
		};

		struct Vec4F {
			float x;
			float y;
			float z;
			float w;
		};

		struct Matrix4x4 {
			float mat[16];
		};

		float FixedToFloat(FixedPoint* fp);

		NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(FixedPoint, fractional, integral);
	}
}