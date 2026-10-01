#include "../src/sf4e/sf4e__LobbyCatalog.hxx"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {
void Require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}
bool Is(const char* actual, const char* expected) {
    return std::strcmp(actual, expected) == 0;
}
}

int main() {
    using namespace sf4e::LobbyCatalog;
    Require(!Find(-1) && !Find(CharacterCount), "invalid character IDs are safe");
    Require(!FindStage(-1) && !FindStage(StageCount), "invalid stage IDs are safe");
    for (int id = 0; id < CharacterCount; ++id) {
        const Fighter* fighter = Find(id);
        Require(fighter && std::strlen(fighter->code) == 3 && *fighter->name, "complete runtime roster");
        for (int u = 0; u < 2; ++u) {
            Require(*fighter->ultra[u].name && *fighter->ultra[u].input, "both Ultra selections have commands");
        }
        for (int other = id + 1; other < CharacterCount; ++other)
            Require(!Is(fighter->code, Find(other)->code), "character code maps to exactly one runtime ID");
    }

    // English UI must not inherit the executable's Japanese boss-name swap.
    Require(Is(Find(8)->name, "Balrog") && Is(Find(8)->code, "BSN"), "boxer uses localized name");
    Require(Is(Find(9)->name, "Vega") && Is(Find(9)->code, "BLR"), "claw uses localized name");
    Require(Is(Find(11)->name, "M. Bison") && Is(Find(11)->code, "VEG"), "dictator uses localized name");
    Require(Is(Find(19)->name, "T. Hawk") && Is(Find(20)->name, "Cammy"), "runtime order differs from text-resource order");
    Require(Is(Find(43)->name, "Decapre"), "final playable slot is represented");

    // These exceptions catch generic quarter-circle placeholders and supers
    // accidentally substituted for the corresponding Ultra.
    Require(Is(Find(17)->ultra[0].input, "LP LP B LK HP"), "Akuma Ultra uses back, super uses forward");
    Require(Is(Find(17)->ultra[1].input, "U U + KKK"), "Demon Armageddon preserves double-up motion");
    Require(Is(Find(5)->ultra[1].input, "AIR 720 + KKK"), "Siberian Blizzard requires the air and two rotations");
    Require(Is(Find(6)->ultra[0].input, "CHARGE DB DF DB UF + KKK"), "Guile preserves charge and diagonal sequence");
    Require(Is(Find(26)->ultra[1].input, "D DF F D DF F + KKK"), "Haoh Gadoken uses three kicks");
    Require(Is(Find(33)->ultra[1].input, "D D D + KKK"), "Hakan preserves triple-down motion");
    Require(Is(Find(12)->ultra[1].input, "AIR D DB B D DB B + KKK"), "Viper preserves aerial double-back motion");
    Require(Is(Find(43)->ultra[1].input, "CHARGE B F B F + KKK"), "Decapre ground DCM has the horizontal charge motion");
    Require(std::strstr(Find(25)->ultra[1].note, "AIR D DF F D DF F + KKK"), "Gen's Crane motion differs from Mantis");
    Require(std::strstr(Find(43)->ultra[1].note, "CHARGE DB DF DB UF + KKK"), "Decapre anti-air trajectory is documented");

    Require(!FindUltra(-1, 0, 14) && !FindUltra(0, 2, 14), "invalid edition-command lookups are safe");
    Require(!FindUltra(0, 1, 13), "original SFIV has no Ultra II");
    Require(!FindUltra(43, 0, 1), "Super cannot select Decapre");
    Require(!FindUltra(35, 0, 1), "Super cannot select Yun");
    Require(!FindUltra(0, 0, 16), "unverified Omega inputs must be explicitly labeled as references");
    Require(FindUltra(0, 0, 13), "original SFIV Ultra I is supported");
    Require(Is(FindUltra(3, 1, 1)->input, "720 + PPP"), "Super Honda uses the original two-rotation command");
    Require(Is(FindUltra(11, 1, 2)->input, "CHARGE B F B F + PPP"), "AE Bison uses charge and punches");
    Require(Is(FindUltra(11, 1, 14)->input, "D DF F D DF F + KKK"), "Ultra Bison uses motion and kicks");
    Require(Is(FindUltra(7, 1, 4)->input, "AIR D DF F D DF F + PPP"), "2012 Dhalsim uses punches");
    Require(Is(FindUltra(38, 1, 4)->input, "B DB D DF F B DB D DF F + PPP"), "2012 Oni preserves forward half-circles");

    int playableStages = 0;
    for (int id = 0; id < StageCount; ++id) {
        const Stage* stage = FindStage(id);
        Require(stage && *stage->code && *stage->name, "complete stage identity");
        if (stage->versus) ++playableStages;
    }
    Require(playableStages == 28, "random versus pool excludes both bonus games");
    Require(!FindStage(22)->versus && !FindStage(23)->versus, "bonus stages cannot enter random pool");
    Require(Is(FindStage(9)->name, "Volcanic Rim"), "internal stage spelling is corrected");
    Require(Is(FindStage(24)->name, "The Pitstop 109"), "Ultra stage ID is not localization's random entry");
    Require(Is(FindStage(29)->code, "JUR"), "last Ultra stage is represented");

    std::puts("Lobby catalog tests passed (44 fighters, 88 Ultras, 28 versus stages).");
}
