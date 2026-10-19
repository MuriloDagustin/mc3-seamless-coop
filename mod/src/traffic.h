#pragma once
// Host-authoritative ambient traffic (docs/RE-TRAFEGO.txt). The host's own traffic already lives around both
// players (one traffic region per human: the partner's car is player 1 there). The host sends the pool's active
// cars; the partner's game keeps spawning into its pool. Legacy puppets have road links cleared. Experimental
// MC3COOP_TRAFFIC_PHYSICS=1 keeps native relocation, copies articulation and corrects existing crash bodies.
#include <cstdint>
#include <string>
#include <vector>

namespace mod::traffic {

#pragma pack(push, 1)
struct Car {
    uint16_t slot;
    uint8_t flags;  // kActive/kTrailer/kWreck/kTrailerBody
    uint8_t colour;
    float pos[3];
    int16_t fwd[3], up[3];  // unit vectors * 32767
    float speed;
    // Protocol 5: no guest pointers. Body centre and trailer are in world coordinates.
    float velocity[3], angular[3], centre[3];
    float trailer[12];
    float trailer_velocity[3], trailer_angular[3], trailer_centre[3];
    uint8_t damage, disabled;
    uint8_t reserved[2];
};
enum : uint8_t { kActive = 1, kTrailer = 2, kWreck = 4, kTrailerBody = 8 };
struct Header {
    uint32_t layout;  // the pool's layout hash: the copies must have the same
    uint32_t world;   // complete setup filename, city and time of day
    uint32_t epoch;   // incremented when the host leaves/rebuilds its traffic world
    uint32_t cycle;   // one snapshot of the host's pool, in several packets
    uint16_t count;   // the pool's size
    uint8_t chunk, chunks;
    uint8_t cars;     // in this packet
    uint8_t version, pad[2];
};
#pragma pack(pop)
constexpr int kCarsPerPacket = 8;
static_assert(sizeof(Car) == 156);
static_assert(sizeof(Header) == 24);
static_assert(20 + sizeof(Header) + kCarsPerPacket * sizeof(Car) <= 1400);

// host, game thread: capture one coherent frame. Session thread only sends this immutable copy.
void Capture(uint64_t now_ms);
// host, session thread: next captured snapshot, at most every 50 ms
bool Snapshot(std::vector<std::vector<uint8_t>>& packets, uint64_t now_ms);
// partner, session thread: a packet from the host
void Receive(const uint8_t* data, int bytes);
// partner, game thread (the xinput hook): the puppets written
void Tick(bool partner, uint64_t now_ms);
void Stop();  // session thread: next game-thread Tick restores the puppets' road links
std::string Describe();

}  // namespace mod::traffic
