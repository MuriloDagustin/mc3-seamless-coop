#pragma once
// Co-op sessions over UDP (LAN / direct IP) or EOS rooms: the host routes, up to eight players.
// Everything here but Init and the Request* calls runs on the mod's tick thread (session Frame, ~60 Hz).
#include <cstdint>
#include <string>
#include <vector>


namespace coop {

constexpr int kMaxPlayers = 8;
constexpr int kNicknameBytes = 64, kNicknameCharacters = 20;
constexpr uint16_t kDefaultPort = 45410;
std::string CleanNickname(const std::string& name);
void RequestNickname(const std::string& name);  // save locally, sent with the next state
int LocalPlayer();
bool Connected(int player);
int PlayerCount();
bool Hosting();
std::string Players();  // the test channel's "players"

enum class Role : uint32_t { Off = 0, Host = 1, Join = 2 };

struct Server {
    std::string address, name;
    uint16_t port = kDefaultPort;
    bool busy = false;
    uint32_t session = 0;
};
struct Session {
    struct Member { int player; std::string nickname; bool local, host; };
    std::string nickname = "Player";
    std::vector<Member> members;
    Role role = Role::Off;
    bool running = false, pending = false, searching = false;
    bool online = false, online_available = false;
    uint16_t port = kDefaultPort;
    std::string status = "COOP OFF", error;
    int players = 0, capacity = kMaxPlayers;
    std::string room_code;
    std::vector<Server> servers;
};
Session GetSession();  // thread-safe copy for F10
void RequestStart(Role role, const std::string& host, uint16_t port);
void RequestOnline(Role role, const std::string& code = {});
void RequestStop();
void RequestSearch(uint16_t port);
void RequestRescue();  // F8: this copy's car taken next to the partner's, at the next frame

void Init();        // the settings and MC3COOP_* environment; once the guest image is found
bool Networked();   // a role and an open socket
void Frame();       // the tick: queued UI commands, packets in, the partner's car, packets out
const char* Status();         // 31 characters at most

// the partner's car as last heard, for the overlay and the test channel; false when there is none in play
struct Remote {
    int player = -1;
    std::string nickname;
    float pos[3]{}, vel[3]{};
    uint64_t age_ms = 0;
    bool in_play = false;
};
bool PartnerCar(Remote& out);

}  // namespace coop
