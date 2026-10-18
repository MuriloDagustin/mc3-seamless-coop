// The game side of the mod: once the guest image is found, the co-op session ticks on its own thread (~60 Hz).
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <thread>

#include "car.h"
#include "mod.h"
#include "net/session.h"
#include "overlay.h"
#include "settings.h"

extern std::atomic<uint32_t> g_xinput_calls;
char kImageSha1[41] = {};  // the game identity copies must share (see session.cpp)

namespace mod::game {

namespace {
std::atomic<uint32_t> g_ticks{0};

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
    for (;;) {
        next += milliseconds(16);
        try {
            coop::Frame();
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

std::string State() {
    char s[512];
    car::Pose p;
    bool have = car::Local(p);
    float speed = std::sqrt(p.vel[0] * p.vel[0] + p.vel[1] * p.vel[1] + p.vel[2] * p.vel[2]);
    std::snprintf(s, sizeof s,
                  "ticks=%u xinput=%u presents=%d coop=\"%s\" city=%d car=%d pos=%.2f,%.2f,%.2f fwd=%.2f,%.2f,%.2f "
                  "speed=%.2f throttle=%.2f",
                  g_ticks.load(), g_xinput_calls.load(), dxgi::Presents(), coop::Status(), car::City(), have,
                  double(p.m[3][0]), double(p.m[3][1]), double(p.m[3][2]), double(-p.m[2][0]), double(-p.m[2][1]),
                  double(-p.m[2][2]), double(speed), double(p.throttle));
    return s;
}

}  // namespace mod::game
