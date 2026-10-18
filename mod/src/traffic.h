#pragma once
// Host-authoritative ambient traffic (docs/RE-TRAFEGO.txt). The host's own traffic already lives around both
// players (one traffic region per human: the partner's car is player 1 there). The host sends the pool's active
// cars; the partner's game keeps spawning into its pool, and every local car of it becomes a puppet (its road link
// cleared, so the local logic neither moves nor recycles it) placed where the host has that slot, or hidden.
#include <cstdint>
#include <string>
#include <vector>

namespace mod::traffic {

#pragma pack(push, 1)
struct Car {
    uint16_t slot;
    uint8_t flags;  // 1 active
    uint8_t colour;
    float pos[3];
    int16_t fwd[3], up[3];  // unit vectors * 32767
    float speed;
};
struct Header {
    uint32_t layout;  // the pool's layout hash: the copies must have the same
    uint32_t cycle;   // one snapshot of the host's pool, in several packets
    uint16_t count;   // the pool's size
    uint8_t chunk, chunks;
    uint8_t cars;     // in this packet
    uint8_t pad[3];
};
#pragma pack(pop)
constexpr int kCarsPerPacket = 34;

// host, session thread: the next snapshot's packets (payloads of Header + Car[cars]), at most every 50 ms
bool Snapshot(std::vector<std::vector<uint8_t>>& packets, uint64_t now_ms);
// partner, session thread: a packet from the host
void Receive(const uint8_t* data, int bytes);
// partner, game thread (the xinput hook): the puppets written
void Tick(bool partner, uint64_t now_ms);
void Stop();  // session thread: next game-thread Tick restores the puppets' road links
std::string Describe();

}  // namespace mod::traffic
