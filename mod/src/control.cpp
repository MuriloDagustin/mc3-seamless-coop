// The test control channel in the runner (after src/game/control.cpp of the own-build experiment): one text
// command a datagram on 127.0.0.1:MC3COOP_CONTROL, one reply each, on a thread of its own.
//   ping | state | base
//   pad <a,b,x,y,lb,rb,lt,rt,up,down,left,right,start,back,ls,rs> [ms] | hold <names> | release
//   stick <lx> <ly> <rx> <ry> [ms]
//   peek <addr> [n] | peekf <addr> [n] | poke <addr> <hex>... | pokef <addr> <f>...
//   find <f> [tolerance] [from] [to]    floats near f in guest memory
//   findu <hex> [from] [to]             dwords equal to hex
//   log <text>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "mod.h"
#include "overlay.h"

namespace mod::scan {
std::string Run(const std::vector<std::string>& a);
std::string Matrices(const std::vector<std::string>& a);
}  // namespace mod::scan

namespace mod::control {

namespace {

bool g_enabled = false;
SOCKET g_sock = INVALID_SOCKET;

struct Injected {
    uint64_t buttons_until = 0, sticks_until = 0;
    bool held = false;
    WORD buttons = 0;
    BYTE lt = 0, rt = 0;
    SHORT lx = 0, ly = 0, rx = 0, ry = 0;
};
std::mutex g_pad_lock;
Injected g_pad;
DWORD g_packet = 0;

uint64_t NowMs() {
    using namespace std::chrono;
    return uint64_t(duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}

std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(uint8_t(s[i]))) i++;
        size_t j = i;
        while (j < s.size() && !std::isspace(uint8_t(s[j]))) j++;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

bool AddButtons(const std::string& list, Injected& p) {
    static const std::pair<const char*, WORD> kBits[] = {
        {"up", XINPUT_GAMEPAD_DPAD_UP}, {"down", XINPUT_GAMEPAD_DPAD_DOWN}, {"left", XINPUT_GAMEPAD_DPAD_LEFT},
        {"right", XINPUT_GAMEPAD_DPAD_RIGHT}, {"start", XINPUT_GAMEPAD_START}, {"back", XINPUT_GAMEPAD_BACK},
        {"ls", XINPUT_GAMEPAD_LEFT_THUMB}, {"rs", XINPUT_GAMEPAD_RIGHT_THUMB}, {"lb", XINPUT_GAMEPAD_LEFT_SHOULDER},
        {"rb", XINPUT_GAMEPAD_RIGHT_SHOULDER}, {"a", XINPUT_GAMEPAD_A}, {"b", XINPUT_GAMEPAD_B},
        {"x", XINPUT_GAMEPAD_X}, {"y", XINPUT_GAMEPAD_Y}, {"white", XINPUT_GAMEPAD_LEFT_SHOULDER},
        {"black", XINPUT_GAMEPAD_RIGHT_SHOULDER}};
    size_t i = 0;
    while (i <= list.size()) {
        size_t j = list.find(',', i);
        if (j == std::string::npos) j = list.size();
        std::string name = list.substr(i, j - i);
        bool found = false;
        for (auto& [n, bit] : kBits)
            if (name == n) p.buttons |= bit, found = true;
        if (name == "lt") p.lt = 255, found = true;
        if (name == "rt") p.rt = 255, found = true;
        if (!found) return false;
        i = j + 1;
    }
    return true;
}

SHORT Axis(const std::string& s) { return SHORT(std::clamp(float(std::atof(s.c_str())), -1.0f, 1.0f) * 32767); }
uint32_t Hex(const std::string& s) { return uint32_t(std::strtoul(s.c_str(), nullptr, 16)); }

std::string Find(const std::vector<std::string>& a, bool exact) {
    float want = float(std::atof(a[1].c_str()));
    uint32_t want_u = Hex(a[1]);
    float tol = !exact && a.size() > 2 ? float(std::atof(a[2].c_str())) : 0.01f;
    size_t k = exact ? 2 : 3;
    uint32_t from = a.size() > k ? Hex(a[k]) : 0x00010000, to = a.size() > k + 1 ? Hex(a[k + 1]) : 0x08000000;
    std::string out;
    int hits = 0;
    char line[40];
    for (uint32_t page = from & ~0xFFFu; page < to && hits < 300; page += 0x1000) {
        if (!Mapped(page, 0x1000)) continue;
        for (uint32_t at = page; at < page + 0x1000; at += 4) {
            bool hit;
            if (exact) hit = R32(at) == want_u;
            else {
                float f = RF(at);
                hit = std::isfinite(f) && std::fabs(f - want) <= tol;
            }
            if (hit) {
                std::snprintf(line, sizeof line, "%08X=%.4f/%08X\n", at, double(RF(at)), R32(at));
                out += line;
                if (++hits >= 300) break;
            }
        }
    }
    return out.empty() ? "none" : out;
}

std::string Run(const std::string& text) {
    auto a = Split(text);
    if (a.empty()) return "error empty";
    const std::string& c = a[0];
    char buf[256];
    if (c == "ping") return "pong";
    if (c == "base") {
        std::snprintf(buf, sizeof buf, "%p", g_base);
        return buf;
    }
    if (c == "state") return game::State();
    if (c == "shot") {
        std::string path = a.size() > 1 ? text.substr(text.find(a[1])) : "shot.bmp";
        std::wstring wide(path.begin(), path.end());
        return overlay::Active() ? overlay::Shot(wide) : layer::Active() ? layer::Shot(wide) : Shot(wide);
    }
    if (c == "pad" || c == "hold") {
        if (a.size() < 2) return "error pad <names> [ms]";
        Injected p;
        if (!AddButtons(a[1], p)) return "error unknown button in " + a[1];
        std::lock_guard lock(g_pad_lock);
        g_pad.buttons = p.buttons, g_pad.lt = p.lt, g_pad.rt = p.rt;
        g_pad.held = c == "hold";
        g_pad.buttons_until = NowMs() + (a.size() > 2 ? std::atoi(a[2].c_str()) : 150);
        return "ok";
    }
    if (c == "stick") {
        if (a.size() < 5) return "error stick lx ly rx ry [ms]";
        std::lock_guard lock(g_pad_lock);
        g_pad.lx = Axis(a[1]), g_pad.ly = Axis(a[2]), g_pad.rx = Axis(a[3]), g_pad.ry = Axis(a[4]);
        g_pad.sticks_until = NowMs() + (a.size() > 5 ? std::atoi(a[5].c_str()) : 500);
        return "ok";
    }
    if (c == "key" && a.size() > 1) {  // a virtual key: a name (enter, esc, space, up, down, left, right, f1..) or hex
        static const std::pair<const char*, int> kNames[] = {
            {"enter", VK_RETURN}, {"esc", VK_ESCAPE}, {"space", VK_SPACE}, {"up", VK_UP}, {"down", VK_DOWN},
            {"left", VK_LEFT}, {"right", VK_RIGHT}, {"tab", VK_TAB}, {"back", VK_BACK}, {"shift", VK_SHIFT},
            {"ctrl", VK_CONTROL}, {"f1", VK_F1}, {"f2", VK_F2}, {"f10", VK_F10}};
        int vk = 0;
        for (auto& [n, v] : kNames)
            if (a[1] == n) vk = v;
        if (!vk) vk = a[1].size() == 1 ? std::toupper(uint8_t(a[1][0])) : int(Hex(a[1]));
        PressKey(vk, a.size() > 2 ? std::atoi(a[2].c_str()) : 120);
        return "ok";
    }
    if (c == "release") {
        std::lock_guard lock(g_pad_lock);
        g_pad = {};
        return "ok";
    }
    if (!g_base && c != "log") return "error guest image not found yet";
    if ((c == "peek" || c == "peekf") && a.size() > 1) {
        uint32_t at = Hex(a[1]), n = a.size() > 2 ? std::min(std::atoi(a[2].c_str()), 512) : 1;
        if (!Mapped(at, n * 4)) return "error not mapped";
        std::string out;
        for (uint32_t i = 0; i < n; i++) {
            if (c == "peek") std::snprintf(buf, sizeof buf, "%08X ", R32(at + i * 4));
            else std::snprintf(buf, sizeof buf, "%.4f ", double(RF(at + i * 4)));
            out += buf;
            if (i % 8 == 7) out += "\n";
        }
        return out;
    }
    if ((c == "poke" || c == "pokef") && a.size() > 2) {
        uint32_t at = Hex(a[1]);
        if (!Mapped(at, uint32_t(a.size() - 2) * 4)) return "error not mapped";
        for (size_t i = 2; i < a.size(); i++) {
            if (c == "poke") W32(at + uint32_t(i - 2) * 4, Hex(a[i]));
            else G<float>(at + uint32_t(i - 2) * 4) = float(std::atof(a[i].c_str()));
        }
        return "ok";
    }
    if (c == "scan") return scan::Run(a);
    if (c == "mats") return scan::Matrices(a);
    if (c == "find" && a.size() > 1) return Find(a, false);
    if (c == "findu" && a.size() > 1) return Find(a, true);
    if (c == "log") {
        Log("ctl: %s", text.size() > 4 ? text.c_str() + 4 : "");
        return "ok";
    }
    return "error unknown command " + c;
}

void Serve() {
    char in[512];
    for (;;) {
        sockaddr_in from{};
        int len = sizeof from;
        int n = recvfrom(g_sock, in, sizeof in - 1, 0, reinterpret_cast<sockaddr*>(&from), &len);
        if (n <= 0) continue;
        in[n] = 0;
        std::string reply;
        try {
            reply = Run(in);
        } catch (...) {
            reply = "error exception";
        }
        if (reply.size() > 8000) reply = reply.substr(0, 8000) + "\n...cut";
        sendto(g_sock, reply.data(), int(reply.size()), 0, reinterpret_cast<sockaddr*>(&from), len);
    }
}

}  // namespace

void Init() {
    int port = std::atoi(Env("MC3COOP_CONTROL").c_str());
    if (port <= 0 || port > 65535) return;
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(uint16_t(port));
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(g_sock, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
        Log("ctl: bind of 127.0.0.1:%d failed (%d)", port, WSAGetLastError());
        return;
    }
    g_enabled = true;
    std::thread(Serve).detach();
    Log("ctl: control channel on 127.0.0.1:%d", port);
}

bool Enabled() { return g_enabled; }

bool Override(DWORD user, XINPUT_STATE* state) {
    if (!g_enabled) return false;
    if (user != 0) return true;  // the caller reports the others unplugged through the return value below
    std::memset(state, 0, sizeof *state);
    state->dwPacketNumber = ++g_packet;
    uint64_t now = NowMs();
    std::lock_guard lock(g_pad_lock);
    if (g_pad.held || now < g_pad.buttons_until) {
        state->Gamepad.wButtons = g_pad.buttons;
        state->Gamepad.bLeftTrigger = g_pad.lt;
        state->Gamepad.bRightTrigger = g_pad.rt;
    }
    if (now < g_pad.sticks_until) {
        state->Gamepad.sThumbLX = g_pad.lx, state->Gamepad.sThumbLY = g_pad.ly;
        state->Gamepad.sThumbRX = g_pad.rx, state->Gamepad.sThumbRY = g_pad.ry;
    }
    return true;
}

}  // namespace mod::control
