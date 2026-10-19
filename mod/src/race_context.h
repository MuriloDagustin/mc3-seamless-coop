#pragma once
// Pointer-free world identity carried with remote state. A received start is not proof that a peer has loaded
// the race: publish kind 2 only after binding the actual local race object to that start's identity.
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace mod::race {

#pragma pack(push, 1)
struct World {
    uint32_t serial = 0;       // host start serial; free roam always uses zero
    int16_t race_idx = -1;     // race database index, not a pointer or this career's record id
    int8_t city = -1;
    uint8_t kind = 0;          // 0 unavailable (menus/loading/garage), 1 cruise, 2 loaded race
};
#pragma pack(pop)

static_assert(sizeof(World) == 8 && alignof(World) == 1);
static_assert(offsetof(World, serial) == 0 && offsetof(World, race_idx) == 4 &&
              offsetof(World, city) == 6 && offsetof(World, kind) == 7);
static_assert(std::is_trivially_copyable_v<World>);

// Unavailable and noncanonical combinations fail closed. In particular, merely receiving a nonzero serial
// while still in free roam must not make a peer count as a participant in the host's race.
constexpr bool ValidWorld(const World& world) {
    if (world.city < 0 || world.city > 3) return false;
    if (world.kind == 1) return world.serial == 0 && world.race_idx == -1;
    if (world.kind == 2) return world.serial != 0 && world.race_idx >= 0 && world.race_idx < 160;
    return false;
}

// Matching is exact, not serial ordering: an old start, another race in the same city, or unavailable data
// never authorizes remote pose/result application. The caller must also authenticate the current session.
constexpr bool MatchesWorld(const World& local, const World& peer) {
    if (!ValidWorld(local) || !ValidWorld(peer) || local.city != peer.city || local.kind != peer.kind) return false;
    return local.kind == 1 || (local.serial == peer.serial && local.race_idx == peer.race_idx);
}

}  // namespace mod::race
