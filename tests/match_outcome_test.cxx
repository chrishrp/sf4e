#include "../src/session/sf4e__MatchOutcome.hxx"

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <limits>

// Keep checks active in Release/RelWithDebInfo builds as well.
#define CHECK(expression) do { if (!(expression)) { std::fprintf(stderr, "FAIL: %s\n", #expression); std::exit(1); } } while (false)

int main() {
    using sf4e::MatchOutcome::WinnerFromVitality;

    // Seth wins this time-out despite having fewer raw health points.
    CHECK(WinnerFromVitality(500, 1100, 450, 750) == 1);
    CHECK(WinnerFromVitality(450, 750, 500, 1100) == 0);
    CHECK(WinnerFromVitality(200, 1000, 176, 800) == 1);
    CHECK(WinnerFromVitality(500, 1000, 375, 750) == -1);
    CHECK(WinnerFromVitality(1100, 1100, 750, 750) == -1);

    CHECK(WinnerFromVitality(0, 1000, 1, 750) == 1);
    CHECK(WinnerFromVitality(1, 1000, 0, 750) == 0);
    CHECK(WinnerFromVitality(0, 1000, 0, 750) == -1);

    CHECK(WinnerFromVitality(0, 0, 750, 750) == -1);
    CHECK(WinnerFromVitality(1000, 1000, 0, 0) == -1);
    CHECK(WinnerFromVitality(0, -1, 750, 750) == -1);
    CHECK(WinnerFromVitality(1000, 1000, 0, -1) == -1);
    CHECK(WinnerFromVitality(-1, 1000, 750, 750) == -1);
    CHECK(WinnerFromVitality(1000, 1000, -1, 750) == -1);
    CHECK(WinnerFromVitality(1001, 1000, 750, 750) == -1);
    CHECK(WinnerFromVitality(1000, 1000, 751, 750) == -1);

    const std::int32_t large = (std::numeric_limits<std::int32_t>::max)();
    CHECK(WinnerFromVitality(large, large, large, large) == -1);
    CHECK(WinnerFromVitality(large - 1, large, large - 2, large) == 0);
    CHECK(WinnerFromVitality(large - 2, large, large - 1, large) == 1);
    CHECK(WinnerFromVitality(large - 1, large, large - 2, large - 1) == 0);
    CHECK(WinnerFromVitality((std::numeric_limits<std::int32_t>::min)(), large, 1, large) == -1);
}
