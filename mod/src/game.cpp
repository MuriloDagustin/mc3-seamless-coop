// The game side of the mod: once the guest image is found, the co-op session ticks on its own thread (~60 Hz).
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>

#include "car.h"
#include "mod.h"
#include "net/session.h"
#include "overlay.h"
#include "race.h"
#include "traffic.h"
#include "ai.h"
#include "settings.h"

extern std::atomic<uint32_t> g_xinput_calls, g_xinput_guest;
extern std::atomic<uintptr_t> g_xinput_ret;
char kImageSha1[41] = {};  // the game identity copies must share (see session.cpp)

namespace mod::game {

namespace {
std::atomic<uint32_t> g_ticks{0};
std::atomic<uint64_t> g_fiber_at{0};  // the last tick on the game thread (the xinput hook)
// the game thread's time in each part of the tick, microseconds: the last second's total and worst single tick
struct Cost { uint64_t total = 0, worst = 0; };
Cost g_cost[6], g_cost_shown[6];
uint64_t g_cost_since = 0;
uint64_t Micros() {
    static LARGE_INTEGER f = [] { LARGE_INTEGER q; QueryPerformanceFrequency(&q); return q; }();
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return uint64_t(c.QuadPart) * 1000000 / uint64_t(f.QuadPart);
}

// the XBE's title id, certificate version and image size, from the header in the guest image
void Identity() {
    uint32_t cert = R32(0x10118);
    std::snprintf(kImageSha1, sizeof kImageSha1, "mc3-%08X-%08X-%08X-coop1          ", R32(cert + 8), R32(cert + 0xAC),
                  R32(0x1010C));
    kImageSha1[40] = 0;
}

void Loop() {
    using namespace std::chrono;
    auto next = steady_clock::now();
    bool warned_no_hook = false;
    for (;;) {
        next += milliseconds(16);
        try {
            coop::Frame();
            // Race counter/payment stores must never race with the guest's own finish callback.
            // Without a verified input hook, wait instead of doing that read/modify/write on this OS thread.
            bool waiting = coop::Networked() && GetTickCount64() - g_fiber_at > 250;
            if (waiting && !warned_no_hook) Log("game: waiting for guest xinput hook; car/race/traffic writes suspended");
            warned_no_hook = waiting;
        } catch (...) {
            Log("game: session tick threw");
        }
        g_ticks++;
        std::this_thread::sleep_until(next);
        if (steady_clock::now() - next > milliseconds(200)) next = steady_clock::now();
    }
}
}  // namespace

void Start() {
    LoadSettings();
    Identity();
    coop::Init();
    std::thread(Loop).detach();
    Log("game: session started, identity %s", kImageSha1);
}

void Pad(DWORD, const XINPUT_STATE*) {}

// the runner's XInputGetState for the guest runs on the game fiber, inside guest code: data written here never tears
// against the game (docs/RE-CORRIDA.txt, native ABI)
void FiberTick() {
    static bool busy = false;
    static uint64_t last = 0;
    uint64_t now = GetTickCount64();
    if (busy || !g_base || now - last < 10) return;
    busy = true;
    last = now;
    g_fiber_at = now;
    uint64_t t[6];
    t[0] = Micros();
    try {
        car::Tick(now);
        t[1] = Micros();
        race::Tick(coop::Hosting(), coop::PlayerCount() > 1 && coop::Networked());
        t[2] = Micros();
        traffic::Tick(coop::Networked() && !coop::Hosting(), now);
        t[3] = Micros();
        if (coop::Networked() && coop::Hosting()) traffic::Capture(now);
        t[4] = Micros();
        ai::Tick(coop::Networked() && !coop::Hosting(), now);
        t[5] = Micros();
        for (int i = 0; i < 5; i++) {
            uint64_t d = t[i + 1] - t[i];
            g_cost[i].total += d, g_cost[i].worst = std::max(g_cost[i].worst, d);
        }
        g_cost[5].total++;
        if (now - g_cost_since >= 1000) {
            std::memcpy(g_cost_shown, g_cost, sizeof g_cost);
            std::memset(g_cost, 0, sizeof g_cost);
            g_cost_since = now;
        }
    } catch (...) {
    }
    busy = false;
}

std::string State() {
    char s[768];
    car::Pose p;
    bool have = car::Local(p);
    float speed = std::sqrt(p.vel[0] * p.vel[0] + p.vel[1] * p.vel[1] + p.vel[2] * p.vel[2]);
    std::snprintf(s, sizeof s,
                  "ticks=%u xinput=%u presents=%d coop=\"%s\" city=%d car=%d pos=%.2f,%.2f,%.2f fwd=%.2f,%.2f,%.2f "
                  "speed=%.2f throttle=%.2f",
                  g_ticks.load(), g_xinput_calls.load(), dxgi::Presents(), coop::Status(), car::City(), have,
                  double(p.m[3][0]), double(p.m[3][1]), double(p.m[3][2]), double(-p.m[2][0]), double(-p.m[2][1]),
                  double(-p.m[2][2]), double(speed), double(p.throttle));
    char hook[320];
    const Cost* c = g_cost_shown;
    std::snprintf(hook, sizeof hook,
                  " xinput_guest=%u xinput_ret=%llX tick_us(total/worst, %llu ticks) car=%llu/%llu race=%llu/%llu "
                  "traffic=%llu/%llu capture=%llu/%llu ai=%llu/%llu",
                  g_xinput_guest.load(), (unsigned long long)g_xinput_ret.load(), (unsigned long long)c[5].total,
                  (unsigned long long)c[0].total, (unsigned long long)c[0].worst, (unsigned long long)c[1].total,
                  (unsigned long long)c[1].worst, (unsigned long long)c[2].total, (unsigned long long)c[2].worst,
                  (unsigned long long)c[3].total, (unsigned long long)c[3].worst, (unsigned long long)c[4].total,
                  (unsigned long long)c[4].worst);
    return std::string(s) + hook + " " + car::Describe() + " " + race::Describe() + " " + traffic::Describe() + " " + ai::Describe();
}

}  // namespace mod::game
