// Pure wire identity checks. No guest RAM, Fixture, network or game process is involved.
#include "../../mod/src/race_context.h"
#include <cassert>
#include <cstring>
#include <initializer_list>

void RaceContextTests() {
    using mod::race::World;
    using mod::race::ValidWorld;
    using mod::race::MatchesWorld;
    constexpr World unavailable{};
    constexpr World cruise{0, -1, 0, 1};
    constexpr World race{7, 42, 0, 2};
    static_assert(!ValidWorld(unavailable));
    static_assert(ValidWorld(cruise) && ValidWorld(race));
    static_assert(MatchesWorld(cruise, cruise) && MatchesWorld(race, race));
    static_assert(!MatchesWorld(unavailable, unavailable) && !MatchesWorld(cruise, race));

    // All cities, database boundary entries and the complete nonzero uint32 serial range are representable.
    for (int city = 0; city <= 3; ++city) {
        World street{0, -1, int8_t(city), 1};
        assert(ValidWorld(street) && MatchesWorld(street, street));
        for (int16_t index : {int16_t(0), int16_t(1), int16_t(159)}) {
            for (uint32_t serial : {1u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu}) {
                World loaded{serial, index, int8_t(city), 2};
                assert(ValidWorld(loaded) && MatchesWorld(loaded, loaded));
                assert(!MatchesWorld(loaded, street) && !MatchesWorld(street, loaded));
                assert(!MatchesWorld(loaded, unavailable) && !MatchesWorld(unavailable, loaded));
                World stale = loaded;
                stale.serial = serial == 1 ? 2 : serial - 1;
                assert(!MatchesWorld(loaded, stale) && !MatchesWorld(stale, loaded));
                World other = loaded;
                other.race_idx = index == 0 ? 1 : 0;
                assert(!MatchesWorld(loaded, other) && !MatchesWorld(other, loaded));
                other = loaded;
                other.city = int8_t((city + 1) % 4);
                assert(!MatchesWorld(loaded, other) && !MatchesWorld(other, loaded));
            }
        }
        World other_street{0, -1, int8_t((city + 1) % 4), 1};
        assert(!MatchesWorld(street, other_street) && !MatchesWorld(other_street, street));
    }

    // Unknown kinds and unavailable data fail closed even when all identity bytes happen to match.
    for (int kind = 0; kind <= 255; ++kind) {
        if (kind == 1 || kind == 2) continue;
        World invalid{7, 42, 0, uint8_t(kind)};
        assert(!ValidWorld(invalid) && !MatchesWorld(invalid, invalid));
        assert(!MatchesWorld(race, invalid) && !MatchesWorld(invalid, race));
    }
    for (int8_t city : {int8_t(-128), int8_t(-1), int8_t(4), int8_t(127)}) {
        for (World invalid : {World{0, -1, city, 1}, World{7, 42, city, 2}}) {
            assert(!ValidWorld(invalid) && !MatchesWorld(invalid, invalid));
        }
    }
    for (World invalid : {World{1, -1, 0, 1}, World{0, 0, 0, 1}, World{0, 159, 0, 1},
                          World{0, 42, 0, 2}, World{7, -1, 0, 2}, World{7, -32768, 0, 2},
                          World{7, 160, 0, 2}, World{7, 32767, 0, 2}}) {
        assert(!ValidWorld(invalid) && !MatchesWorld(invalid, invalid));
        assert(!MatchesWorld(race, invalid) && !MatchesWorld(invalid, race));
        assert(!MatchesWorld(cruise, invalid) && !MatchesWorld(invalid, cruise));
    }

    // Packed bytes survive a copy without a process-specific pointer or hidden padding.
    const World wire{0xFEDCBA98u, 159, 3, 2};
    const unsigned char expected[8]{0x98, 0xBA, 0xDC, 0xFE, 0x9F, 0x00, 0x03, 0x02};
    assert(std::memcmp(&wire, expected, sizeof wire) == 0);
    World decoded{};
    std::memcpy(&decoded, expected, sizeof decoded);
    assert(ValidWorld(decoded) && MatchesWorld(wire, decoded));
}
