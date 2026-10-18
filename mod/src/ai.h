#pragma once
// Experimental, host-authoritative career opponents. Guest writes only happen in Tick (game thread).
#include <cstdint>
#include <string>
#include <vector>

namespace mod::ai {
void SetRace(uint32_t serial, int city, int race_idx, bool sync_progress = false); // opt-in progress experiment
bool Snapshot(std::vector<std::vector<uint8_t>>& packets, uint64_t now_ms);
void Receive(const uint8_t* data, int bytes); // authenticated host only; session thread
void Tick(bool partner, uint64_t now_ms); // game thread; restores owned fields on stop/stale/teardown
void Stop(); // session thread: queues restoration for the next game-thread Tick
std::string Describe();
} // namespace mod::ai
