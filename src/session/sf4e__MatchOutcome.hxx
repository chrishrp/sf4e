#pragma once

#include <cstdint>

namespace sf4e {
namespace MatchOutcome {

// Percentage-based fallback until the game's authoritative winner is mapped.
// Values must use the same units (whole health or fixed-point) on both sides.
// Return side 0/1, or -1 for a tie or invalid health data. This does not infer
// match outcomes from round history or interpret the game's result enum.
inline int WinnerFromVitality(std::int32_t health0, std::int32_t max0,
    std::int32_t health1, std::int32_t max1) {
    if (max0 <= 0 || max1 <= 0 || health0 < 0 || health1 < 0 ||
        health0 > max0 || health1 > max1) return -1;

    // Two nonnegative int32 values multiply safely into a signed int64.
    // Cross multiplication preserves equal ratios without rounding.
    const std::int64_t left = static_cast<std::int64_t>(health0) * max1;
    const std::int64_t right = static_cast<std::int64_t>(health1) * max0;
    return left > right ? 0 : right > left ? 1 : -1;
}

} // namespace MatchOutcome
} // namespace sf4e
