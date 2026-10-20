#pragma once
// The career race flow shared by the copies (docs/RE-CORRIDA.txt), by data writes on the game thread:
// - the host's start: seen when its cruise starts an open race (mcPgCareer +0x2C == 4), sent as (City, RaceIdx) with
//   the host's open-race record;
// - the partner's start: that race put in its open list if missing, the marker filter passed, its car taken into the
//   race's start marker, so its own game starts the same race;
// - a win for both: a copy whose player won tells the others, who mark their local racer won before the race ends;
// - remote racers have no HUD/camera and are completed locally before the local player finishes.
//   Their real victory/prize arrives over the network; disconnects never count them a second time.
#include <cstdint>
#include <string>
#include "race_context.h"

namespace mod::race {

#pragma pack(push, 1)
struct Start {
    uint32_t serial;  // the host's count of starts: repeats are the same start
    int8_t city;
    uint8_t pad;
    int16_t race_idx;
    uint8_t record[0x4C];  // the host's open-race record (prize, opponents, difficulty...)
    char name[32];
};
struct Finished {
    uint32_t serial;
    uint8_t won, dnf;  // terminal result BEFORE any shared-win override or payment
    uint8_t arrested = 0, reserved = 0;
    float finish_time = 0, progress = 0;
    int32_t score = 0;
};
struct Setup {
    uint32_t serial, revision;
    uint32_t opponent_mask, opponent_limit;
    int8_t city;
    uint8_t reserved;
    int16_t race_idx;
    // Eight 2C-byte setup opponent records: model index, enabled, inline name and variant; no pointers.
    uint8_t opponents[8][0x2C];
};
enum class AfterAction : uint8_t { Wait, Street, Next };
struct After {
    uint32_t serial, revision; // finished race and host decision generation
    AfterAction action;
    int8_t city;
    uint16_t reserved;
    Start next;               // Next only; its identity is resolved against the receiver's local task records
};
#pragma pack(pop)
static_assert(sizeof(After) == 128);

// Game thread only (the verified xinput hook); includes a human-finish counter read/modify/write.
void Tick(bool hosting, bool connected);
// Only merge into a successful, verified guest poll of controller 0, after reading the real/test pad.
bool ChallengeInput();
bool ControlInput(uint16_t& buttons); // verified guest controller0 only; true also clears analog inputs
std::string Notice();  // short player-facing message, safe for the render thread
World Context();      // strict loaded world identity; pending starts are not loaded races
void ResetNetwork();  // new transport/host generation; native loans are released on the next game hook
void OnAfter(const After& after);
bool TakeAfter(After& after);

// session thread: what came from the others, what goes to them
void OnStart(const Start& s);
void OnPartnerWon(uint32_t serial, int32_t prize);  // prize 0: not paid yet
bool TakeStart(Start& out);       // the host's start to send (once per start)
bool TakeSetup(Setup& out);
void OnSetup(const Setup& setup);
bool TakeWon(uint32_t& serial, int32_t& prize);  // this copy's player won (again with the prize once paid)
void OnPartnerFinished(const Finished& finished);
bool TakeFinished(Finished& out); // periodically retransmitted, including losses (no ACK dependency)
std::string Describe();           // for the test channel
std::string GoToOpenRace(int index);  // test: the local car into that open race's start marker (no index: the list)

}  // namespace mod::race
