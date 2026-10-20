// Native bridge probe: proves the documented call contract on a real, isolated game copy.
// It calls only two pure getters from the game-thread XInput hook, with the runner's full-save wrapper. It never
// builds, destroys or publishes a vehicle. Opt-in: MC3COOP_NATIVE_PROBE=1. See docs/RE-PONTE-NATIVA-ABI.md and
// docs/PLANO-TROCA-CARRO-NATIVA.md. Unknown builds/contexts are refused rather than guessed.
#include "native.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "car.h"
#include "mod.h"
#include "race.h"

namespace mod::native {

namespace {

// All VAs are RVAs of the runner image (docs/RE-PONTE-NATIVA-ABI.md, build 6AD54432).
constexpr uint32_t kTimestamp = 0x6AD54432;
constexpr uint32_t kPreservingCall = 0x3AD270;
constexpr uint32_t kCallback = 0x31B690;
constexpr uint32_t kInputHle = 0x300D00;
constexpr uint32_t kInputPoll = 0x306BB0;
constexpr uint32_t kInputEntry = 0x3068D0;

// guest register/context globals (host image offsets)
constexpr uint32_t kGuestBaseGlobal = 0x2C667B8;
constexpr uint32_t kCurrentRecord = 0x2C6E160;
constexpr uint32_t kEax = 0x28E7B20, kEbx = 0x28E7B24, kEcx = 0x28E7B28, kEdx = 0x28E7B30;
constexpr uint32_t kEsi = 0x28E7B34, kEsp = 0x28E7B38, kEdi = 0x28E7B2C, kEbp = 0x28E7C70;
constexpr uint32_t kX87Bank = 0x28E7B40, kX87Top = 0x28E7B80, kXmm = 0x28E7C78;
constexpr uint32_t kLazyA = 0x28E7B14, kLazyB = 0x28E7B18, kLazyC = 0x27F55D0, kLazyD = 0x28E7B1C, kLazyE = 0x27F55D4;
constexpr uint32_t kMxcsr = 0x27F55D8, kFcw = 0x27F55DC;
constexpr uint32_t kKpcrGlobal = 0x28E7B84;

// world/player structures (guest VAs, same map as car.cpp)
constexpr uint32_t kPlayerManager = 0x558814, kSetupCurrent = 0x53F2E0, kSetupNext = 0x53F2E4;
constexpr uint32_t kSlotBase = 0x114, kSlotSize = 0x2C, kSlotCar = 0x0C;
constexpr uint32_t kRaceHolder = 0x533E24, kRaceVtable = 0x46A7D8;

// targets: pure getters, no arguments or one read-only index (docs/RE-*)
constexpr uint32_t kGetVehicleManager = 0x3308C0;  // mov eax,[5638E4]; ret
constexpr uint32_t kGetPlayer = 0x2BD910;          // eax = [esp+4]; eax = [ecx+eax*4+8]; ret 4

constexpr uint8_t kPreservingPrefix[] = {0x41,0x57,0x41,0x56,0x56,0x57,0x53,0x48,0x81,0xEC,0x80,0x01,0x00,0x00,0x31,0xC0};
constexpr uint8_t kCallbackPrefix[] = {0x56,0x57,0x53,0x48,0x83,0xEC,0x70,0x48,0xBF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
constexpr uint8_t kInputHlePrefix[] = {0x41,0x57,0x41,0x56,0x41,0x55,0x41,0x54,0x56,0x57,0x55,0x53,0x48,0x83,0xEC,0x78};
constexpr uint8_t kInputPollPrefix[] = {0x56,0x57,0x53,0x48,0x81,0xEC,0x90,0x00,0x00,0x00,0x48,0x89,0xD6,0x83,0xF9,0x04};
constexpr uint8_t kInputEntryPrefix[] = {0x56,0x57,0x53,0x48,0x83,0xEC,0x30,0x89,0xC8,0x83,0xF9,0x03};

struct Regs {
    uint32_t eax=0, ebx=0, ecx=0, edx=0, esi=0, edi=0, ebp=0, esp=0;
    uint32_t x87top=0, mxcsr=0, fcw=0, lazy[5]{};
    uint8_t x87[64]{}, xmm[128]{};
};

std::string g_state = "native: probe off";
bool g_on = false, g_swap = false, g_busy = false, g_done = false;

uint8_t* Image() { return reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr)); }
bool Same(uint32_t rva, const uint8_t* bytes, size_t n) {
    uint8_t* image = Image();
    return image && std::memcmp(image + rva, bytes, n) == 0;
}
uint32_t Host32(uint32_t rva) { uint32_t v = 0; std::memcpy(&v, Image() + rva, 4); return v; }
uint64_t Host64(uint32_t rva) { uint64_t v = 0; std::memcpy(&v, Image() + rva, 8); return v; }

bool Readable(const void* p, size_t n) {
    if (!p || n > 0x10000000) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof mbi) || mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    const uint8_t* end = static_cast<const uint8_t*>(p) + n;
    return end <= static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
}

bool BuildAndContext(std::string& why) {
    uint8_t* image = Image();
    if (!image) { why = "no runner image"; return false; }
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) { why = "bad PE"; return false; }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.TimeDateStamp != kTimestamp) {
        char text[64]; std::snprintf(text, sizeof text, "build %08X not probed", unsigned(nt->FileHeader.TimeDateStamp));
        why = text; return false;
    }
    if (!Same(kPreservingCall, kPreservingPrefix, sizeof kPreservingPrefix) ||
        !Same(kCallback, kCallbackPrefix, sizeof kCallbackPrefix) ||
        !Same(kInputHle, kInputHlePrefix, sizeof kInputHlePrefix) ||
        !Same(kInputPoll, kInputPollPrefix, sizeof kInputPollPrefix) ||
        !Same(kInputEntry, kInputEntryPrefix, sizeof kInputEntryPrefix)) {
        why = "code signatures changed"; return false;
    }
    if (Host64(kGuestBaseGlobal) != reinterpret_cast<uintptr_t>(g_base)) { why = "guest base mismatch"; return false; }
    uintptr_t record = Host64(kCurrentRecord);
    if (!Readable(reinterpret_cast<void*>(record), 0x198)) { why = "no readable guest record"; return false; }
    uint32_t* rec = reinterpret_cast<uint32_t*>(record);
    if (rec[0x168 / 4] != 1) { why = "guest record not executing"; return false; }
    uint32_t kpcr = Host32(kKpcrGlobal), rec_kpcr = rec[0x20 / 4];
    if (!kpcr || kpcr != rec_kpcr) { why = "KPCR mismatch"; return false; }
    if (!Mapped(kpcr + 0x28, 4)) { why = "KPCR memory unmapped"; return false; }
    uint32_t kthread = R32(kpcr + 0x28);
    if (!kthread || !Mapped(kthread + 0x20, 4)) { why = "KTHREAD unmapped"; return false; }
    uint32_t top = R32(kthread + 0x1C), bottom = R32(kthread + 0x20), esp = Host32(kEsp);
    if (!(bottom < esp && esp <= top && esp - bottom > 0x800)) {
        char text[96]; std::snprintf(text, sizeof text, "stack range bad (%08X..%08X esp %08X)", bottom, top, esp);
        why = text; return false;
    }
    return true;
}

void Snapshot(Regs& r) {
    r.eax = Host32(kEax); r.ebx = Host32(kEbx); r.ecx = Host32(kEcx); r.edx = Host32(kEdx);
    r.esi = Host32(kEsi); r.edi = Host32(kEdi); r.ebp = Host32(kEbp); r.esp = Host32(kEsp);
    r.x87top = Host32(kX87Top); r.mxcsr = Host32(kMxcsr); r.fcw = Host32(kFcw);
    r.lazy[0] = Host32(kLazyA); r.lazy[1] = Host32(kLazyB); r.lazy[2] = Host32(kLazyC);
    r.lazy[3] = Host32(kLazyD); r.lazy[4] = Host32(kLazyE);
    std::memcpy(r.x87, Image() + kX87Bank, sizeof r.x87);
    std::memcpy(r.xmm, Image() + kXmm, sizeof r.xmm);
}

uint32_t Mismatch(const Regs& a, const Regs& b, bool ecx_expected_modified, uint32_t expected_ecx) {
    if (a.eax != b.eax || a.ebx != b.ebx || a.edx != b.edx || a.esi != b.esi || a.edi != b.edi ||
        a.ebp != b.ebp || a.esp != b.esp || a.x87top != b.x87top || a.mxcsr != b.mxcsr || a.fcw != b.fcw) return 1;
    if (ecx_expected_modified ? (b.ecx != expected_ecx) : (a.ecx != b.ecx)) return 2;
    for (int i = 0; i < 5; i++) if (a.lazy[i] != b.lazy[i]) return 3;
    if (std::memcmp(a.x87, b.x87, sizeof a.x87) || std::memcmp(a.xmm, b.xmm, sizeof a.xmm)) return 4;
    return 0;
}


// Silent call through the runner's full-save wrapper. The caller supplies guest ECX (0 keeps the current one).
bool WorldStable(std::string& why) {
    race::World world = race::Context();
    if (world.kind != 1) { why = "not in free roam"; return false; }
    uint32_t holder = R32(kRaceHolder), race = holder ? R32(holder + 4) : 0;
    if (!race || R32(race) != kRaceVtable) { why = "cruise object changed"; return false; }
    return true;
}

bool Call(uint32_t guest, const uint32_t* args, uint32_t count, uint32_t ecx, Regs* before) {
    if (!BuildAndContext(g_state)) return false;
    Regs saved; Snapshot(saved); if (before) *before = saved;
    if (ecx) std::memcpy(Image() + kEcx, &ecx, 4);
    using Fn = int (*)(uint32_t, const uint32_t*, uint32_t);
    auto call = reinterpret_cast<Fn>(Image() + kPreservingCall);
    int ok = call(guest, args, count);
    Regs after; Snapshot(after);
    uint32_t mismatch = Mismatch(saved, after, ecx != 0, ecx);
    // the wrapper restores the ECX present at entry; that was our temporary value, so put the original back
    if (ecx) std::memcpy(Image() + kEcx, &saved.ecx, 4);
    char text[256];
    std::snprintf(text, sizeof text, "native: call guest=%08X argc=%u result=%d mismatch=%u esp=%08X",
                  guest, count, ok, mismatch, after.esp);
    Log("%s", text);
    g_state = text;
    return ok == 1 && mismatch == 0;
}

}  // namespace

void Init() {
    g_on = Env("MC3COOP_NATIVE_PROBE") == "1";
    g_swap = Env("MC3COOP_CAR_SWAP") == "1";
    g_state = g_on ? "native: probe armed (waiting for a loaded world)" : "native: probe off";
    if (g_on) Log("native: probe armed");
    if (g_swap) Log("native: car swap armed (experimental)");
}

bool Enabled() { return g_on; }
bool SwapEnabled() { return g_swap; }

void Tick() {
    if (!g_on || g_done || g_busy || !car::InPlay()) return;
    g_busy = true;
    bool ok = Call(kGetVehicleManager, nullptr, 0, 0, nullptr);
    if (ok) {
        uint32_t manager = R32(kPlayerManager), arg = 0;
        if (!manager || !Mapped(manager, 0x0C)) {
            g_state = "native: player manager not ready";
            ok = false;
        } else {
            ok = Call(kGetPlayer, &arg, 1, manager, nullptr);
        }
    }
    if (ok) {
        // repeated calls in the same hook context, the same way the swap queue will run
        for (int i = 0; i < 3 && ok; i++) ok = Call(kGetVehicleManager, nullptr, 0, 0, nullptr);
    }
    if (!ok) Log("native: probe REFUSED/FAILED: %s", g_state.c_str());
    else Log("native: probe PASS");
    g_state = ok ? "native: probe PASS" : "native: probe FAILED";
    g_done = true;
    g_busy = false;
}

std::string Describe() { return g_state; }

bool SwapRemote(int slot) {
    // Result of the live investigation (docs/TESTE-ISOLADO-2026-10-18.md, 20/10): the release game refuses to create
    // a car resource at runtime. 2CB2D0's 105B80 takes the free-entry path into 105AA0, which logs "Cannot make car
    // resource on RELEASE ... why they are not in the assets!" and returns null; 2CBC40 then continues with a null
    // product and hits unresolved vtable targets (observed ICE/exit). A loaded world can only reuse the resource it
    // was loaded with, so a different model cannot be built here. The safe path is the existing NEXT staging, applied
    // by the next normal load (garage/race) without reconnecting.
    if (!g_swap || slot < 1 || slot > 7) return false;
    std::string why;
    if (!BuildAndContext(why)) {
        Log("native: hot swap unavailable: %s", why.c_str());
        return false;
    }
    if (!WorldStable(why)) {
        Log("native: hot swap deferred: %s", why.c_str());
        return false;
    }
    uint32_t manager = R32(kPlayerManager), count = manager ? R32(manager + 0x28) : 0;
    if (!manager || count <= uint32_t(slot) || !Mapped(manager + 8 + uint32_t(slot) * 4, 4)) return false;
    uint32_t player = R32(manager + 8 + uint32_t(slot) * 4), vehicle = player ? R32(player + 0x14) : 0;
    uint32_t next = R32(kSetupNext);
    if (!vehicle || !next || !Mapped(vehicle, 0x6C) || !Mapped(next + kSlotBase + uint32_t(slot) * kSlotSize, 4)) return false;
    uint32_t liveCfg = R32(vehicle + 0x64), nextCfg = R32(next + kSlotBase + uint32_t(slot) * kSlotSize + kSlotCar);
    if (!liveCfg || !nextCfg || liveCfg > UINT32_MAX - car::kCarCfgBytes || nextCfg > UINT32_MAX - car::kCarCfgBytes ||
        !Mapped(liveCfg, car::kCarCfgBytes) || !Mapped(nextCfg, car::kCarCfgBytes)) return false;
    if (std::memcmp(HostPtr(liveCfg + 0xE3), HostPtr(nextCfg + 0xE3), 0x16) == 0) return false;
    Log("native: slot %d model %.22s cannot be built in a loaded release world; staged for the next normal load",
        slot, reinterpret_cast<const char*>(HostPtr(nextCfg + 0xE3)));
    return false;
}
}  // namespace mod::native
