// The test control channel. One command a datagram, one reply each, on a thread of its own: copies on a hidden
// desktop are driven and read without the game thread's help, so a copy stuck in a load still answers.
//   ping                          pong, the frame count
//   state                         key=value: frame, coop status, city, in play, the car's position and speed
//   players                       the co-op roster
//   coop host [port] | join <host> [port] | online host | online join <code> | search [port] | stop
//   nick <name>                   this copy's nickname
//   rescue                        "take me to my partner"
//   pad <a,b,x,y,black,white,lt,rt,up,down,left,right,start,back,ls,rs> [ms]   press for ms (150)
//   hold <names> / release        pressed until release / the pad at rest
//   stick <lx> <ly> <rx> <ry> [ms]  -1..1, for ms (500)
//   peek <addr> [n] / peekf <addr> [n]   n dwords (hex) or floats; addr hex
//   poke <addr> <hex>... / pokef <addr> <f>...
//   find <f> [tolerance] [from] [to]     guest addresses holding a float near f (memory search)
//   shot                          the next frame saved under XBE_CAPTURE_DIR (final.bmp)
//   log <text>                    a line in this copy's log
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
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
#include <windows.h>

#include "app/app.h"
#include "core/diag.h"
#include "core/input.h"
#include "core/log.h"
#include "game/car.h"
#include "game/control.h"
#include "game/coop.h"
#include "game/coop_net.h"
#include "gpu/backend.h"
#include "kernel/memory.h"

using namespace kernel;

namespace control {

namespace {

bool g_enabled = false;
std::atomic<uint32_t> g_frames{0};

// the pad, from the channel's thread to the pad thread
struct Injected {
    uint64_t buttons_until = 0, sticks_until = 0;
    bool held = false;
    uint16_t buttons = 0;
    uint8_t analog[8] = {};
    int16_t lx = 0, ly = 0, rx = 0, ry = 0;
};
std::mutex g_pad_lock;
Injected g_pad;

// read on the game thread each frame, for "state"
std::mutex g_state_lock;
std::string g_state = "frame=0";

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

// names to the pad: buttons bits and analog indices
bool AddButtons(const std::string& list, uint16_t& buttons, uint8_t analog[8]) {
    static const char* kAnalog[] = {"a", "b", "x", "y", "black", "white", "lt", "rt"};
    static const char* kBits[] = {"up", "down", "left", "right", "start", "back", "ls", "rs"};
    size_t i = 0;
    while (i <= list.size()) {
        size_t j = list.find(',', i);
        if (j == std::string::npos) j = list.size();
        std::string name = list.substr(i, j - i);
        bool found = false;
        for (int k = 0; k < 8; k++)
            if (name == kAnalog[k]) analog[k] = 0xFF, found = true;
        for (int k = 0; k < 8; k++)
            if (name == kBits[k]) buttons |= uint16_t(1 << k), found = true;
        if (!found) return false;
        i = j + 1;
    }
    return true;
}

int16_t Axis(const std::string& s) {
    float f = std::clamp(float(std::atof(s.c_str())), -1.0f, 1.0f);
    return int16_t(f * 32767);
}

uint32_t Hex(const std::string& s) { return uint32_t(std::strtoul(s.c_str(), nullptr, 16)); }

bool Mapped(uint32_t at, uint32_t bytes) { return bytes && mem::IsCommitted(at) && mem::IsCommitted(at + bytes - 1); }

std::string Find(const std::vector<std::string>& a) {
    float want = float(std::atof(a[1].c_str()));
    float tol = a.size() > 2 ? float(std::atof(a[2].c_str())) : 0.01f;
    uint32_t from = a.size() > 3 ? Hex(a[3]) : 0x00010000, to = a.size() > 4 ? Hex(a[4]) : 0x14000000;
    std::string out;
    int hits = 0;
    char line[32];
    for (uint32_t page = from & ~0xFFFu; page < to && hits < 200; page += 0x1000) {
        if (!mem::IsCommitted(page)) continue;
        for (uint32_t at = page; at < page + 0x1000; at += 4) {
            float f = G<float>(at);
            if (std::isfinite(f) && std::fabs(f - want) <= tol) {
                std::snprintf(line, sizeof line, "%08X=%.4f\n", at, double(f));
                out += line;
                if (++hits >= 200) break;
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
    if (c == "ping") {
        std::snprintf(buf, sizeof buf, "pong frame=%u", g_frames.load());
        return buf;
    }
    if (c == "state") {
        std::lock_guard lock(g_state_lock);
        return g_state;
    }
    if (c == "players") return coop::Players();
    if (c == "nick" && a.size() > 1) {
        coop::RequestNickname(text.substr(text.find(a[1])));
        return "ok";
    }
    if (c == "rescue") {
        coop::RequestRescue();
        return "ok";
    }
    if (c == "coop") {
        if (a.size() == 1) return coop::GetSession().status;
        if (a[1] == "host") coop::RequestStart(coop::Role::Host, {}, uint16_t(a.size() > 2 ? std::atoi(a[2].c_str()) : coop::kDefaultPort));
        else if (a[1] == "join" && a.size() > 2)
            coop::RequestStart(coop::Role::Join, a[2], uint16_t(a.size() > 3 ? std::atoi(a[3].c_str()) : coop::kDefaultPort));
        else if (a[1] == "online" && a.size() > 2 && a[2] == "host") coop::RequestOnline(coop::Role::Host);
        else if (a[1] == "online" && a.size() > 3 && a[2] == "join") coop::RequestOnline(coop::Role::Join, a[3]);
        else if (a[1] == "search") coop::RequestSearch(uint16_t(a.size() > 2 ? std::atoi(a[2].c_str()) : coop::kDefaultPort));
        else if (a[1] == "stop") coop::RequestStop();
        else if (a[1] == "session") {
            coop::Session s = coop::GetSession();
            std::string out = "status=" + s.status + " running=" + std::to_string(s.running) + " online=" +
                              std::to_string(s.online) + " code=" + s.room_code + " players=" + std::to_string(s.players) +
                              " error=" + s.error + "\n";
            for (auto& srv : s.servers) out += "server " + srv.address + ":" + std::to_string(srv.port) + " " + srv.name + "\n";
            return out;
        } else return "error coop: host [port] | join <host> [port] | online host | online join <code> | search | stop | session";
        return "ok";
    }
    if (c == "pad" || c == "hold") {
        if (a.size() < 2) return "error pad <names> [ms]";
        Injected p;
        if (!AddButtons(a[1], p.buttons, p.analog)) return "error unknown button in " + a[1];
        std::lock_guard lock(g_pad_lock);
        g_pad.buttons = p.buttons;
        std::memcpy(g_pad.analog, p.analog, sizeof p.analog);
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
    if (c == "release") {
        std::lock_guard lock(g_pad_lock);
        g_pad = {};
        return "ok";
    }
    if ((c == "peek" || c == "peekf") && a.size() > 1) {
        uint32_t at = Hex(a[1]), n = a.size() > 2 ? std::min(std::atoi(a[2].c_str()), 256) : 1;
        if (!Mapped(at, n * 4)) return "error not mapped";
        std::string out;
        for (uint32_t i = 0; i < n; i++) {
            if (c == "peek") std::snprintf(buf, sizeof buf, "%08X ", R32(at + i * 4));
            else std::snprintf(buf, sizeof buf, "%.4f ", double(G<float>(at + i * 4)));
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
    if (c == "find" && a.size() > 1) return Find(a);
    if (c == "shot") {
        if (diag::CaptureDir().empty()) return "error set XBE_CAPTURE_DIR";
        gpu::g_capture = true;
        return "ok " + (app::ExeDir() / diag::CaptureDir()).string();
    }
    if (c == "log") {
        LOGR("ctl: %s", text.size() > 4 ? text.c_str() + 4 : "");
        return "ok";
    }
    return "error unknown command " + c;
}

void Serve() {
    char in[512];
    coop::net::Peer from;
    for (;;) {
        int n = coop::net::ReceiveControl(from, in, sizeof in - 1);
        if (n < 0) {
            Sleep(2);
            continue;
        }
        if (n == 0) continue;
        in[n] = 0;
        std::string reply;
        try {
            reply = Run(in);
        } catch (...) {
            reply = "error exception";
        }
        constexpr size_t kReplyMax = 8000;
        if (reply.size() > kReplyMax) reply = reply.substr(0, kReplyMax) + "\n...cut";
        coop::net::SendControl(from, reply.data(), int(reply.size()));
    }
}

}  // namespace

void Init() {
    std::string v;
    if (!coop::net::Arg("--coop-control", v)) return;
    int port = std::atoi(v.c_str());
    if (!coop::net::OpenControl(uint16_t(port > 0 ? port : 45500 + app::Instance()))) return;
    g_enabled = true;
    std::thread(Serve).detach();
    LOGR("ctl: control channel on 127.0.0.1:%d", port > 0 ? port : 45500 + app::Instance());
}

bool Enabled() { return g_enabled; }
uint32_t Frames() { return g_frames; }

void Tick(X86Context&) {
    uint32_t frame = ++g_frames;
    if (!g_enabled) return;
    char s[512];
    mc3::car::Pose p;
    bool car = mc3::car::Local(p);
    float speed = std::sqrt(p.vel[0] * p.vel[0] + p.vel[1] * p.vel[1] + p.vel[2] * p.vel[2]);
    std::snprintf(s, sizeof s, "frame=%u coop=\"%s\" city=%d play=%d car=%d pos=%.2f,%.2f,%.2f fwd=%.2f,%.2f,%.2f speed=%.2f",
                  frame, coop::Status(), mc3::car::City(), mc3::car::InPlay(), car, double(p.m[3][0]), double(p.m[3][1]),
                  double(p.m[3][2]), double(p.m[2][0]), double(p.m[2][1]), double(p.m[2][2]), double(speed));
    std::lock_guard lock(g_state_lock);
    g_state = s;
}

bool Override(int port, input::Pad& pad) {
    if (!g_enabled) return false;
    pad = input::Pad{};
    if (port != 0) return true;  // the others unplugged
    pad.connected = true;
    uint64_t now = NowMs();
    std::lock_guard<std::mutex> lock(g_pad_lock);
    if (g_pad.held || now < g_pad.buttons_until) {
        pad.buttons = g_pad.buttons;
        std::memcpy(pad.analog, g_pad.analog, sizeof pad.analog);
    }
    if (now < g_pad.sticks_until) pad.lx = g_pad.lx, pad.ly = g_pad.ly, pad.rx = g_pad.rx, pad.ry = g_pad.ry;
    return true;
}

}  // namespace control
