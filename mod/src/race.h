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
};
#pragma pack(pop)

// Game thread only (the verified xinput hook); includes a human-finish counter read/modify/write.
void Tick(bool hosting, bool connected);
// Only merge into a successful, verified guest poll of controller 0, after reading the real/test pad.
bool ChallengeInput();

// session thread: what came from the others, what goes to them
void OnStart(const Start& s);
void OnPartnerWon(uint32_t serial, int32_t prize);  // prize 0: not paid yet
bool TakeStart(Start& out);       // the host's start to send (once per start)
bool TakeWon(uint32_t& serial, int32_t& prize);  // this copy's player won (again with the prize once paid)
void OnPartnerFinished(const Finished& finished);
bool TakeFinished(Finished& out); // periodically retransmitted, including losses (no ACK dependency)
std::string Describe();           // for the test channel
std::string GoToOpenRace(int index);  // test: the local car into that open race's start marker (no index: the list)

}  // namespace mod::race
