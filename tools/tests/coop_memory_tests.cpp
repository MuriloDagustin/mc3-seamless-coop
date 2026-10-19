// Regression tests against the real mod sources and a synthetic Xbox address space. No runner/game is used.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cassert>
#include <climits>
#include <cstdarg>
#include <cstdio>
#include <vector>
#include <string>
#include "../../mod/src/mod.h"
#include "../../mod/src/guest_poll.h"
static uint64_t test_now = 1000;
static ULONGLONG TestClock() { return test_now; }
#define GetTickCount64 TestClock
#include "../../mod/src/car.cpp"
#include "../../mod/src/race.cpp"
#include "../../mod/src/traffic.cpp"
#include "../../mod/src/ai.cpp"
#undef GetTickCount64

static std::vector<uint8_t> ram(0x600000);
namespace mod {
uint8_t* g_base = ram.data();
bool Mapped(uint32_t va, uint32_t bytes) { return va >= 0x10000 && va < ram.size() && bytes <= ram.size() - va; }
void Log(const char*, ...) {}
}
using mod::W32; using mod::R32; using mod::G;
constexpr uint32_t setup=0x100000,next_setup=0x101000,career=0x102000,data=0x103000,rewards=0x104000;
constexpr uint32_t holder=0x105000,race=0x106000,machine=0x108000,players=0x109000;
constexpr uint32_t local=race+0x98,remote=local+0xB8;
void Fixture() {
    _putenv_s("MC3COOP_FINISH_GATE", "0"); // legacy tests explicitly exercise the opt-out path
    std::fill(ram.begin(),ram.end(),0);
    mod::car::g_remote = {};
    mod::race::g_have_start=false; mod::race::g_phase=0;
    mod::race::BeginRace(101);
    W32(0x53F2E0,setup); W32(0x53F2E4,next_setup); W32(0x557F0C,career);
    W32(career+0x0C,data); W32(career+0x14,rewards);
    W32(0x533E24,holder); W32(holder+4,race); W32(race,0x46B2D8);
    W32(0x557EEC,machine); W32(machine+4,4); W32(0x558814,players);
    W32(players+0x28,2); W32(players+8,0x10A000); W32(players+12,0x10B000);
    W32(setup+0x4C,2);W32(next_setup+0x4C,2);W32(setup+0x50,1);W32(next_setup+0x50,1);
    W32(setup+0x140+0x14,0xFFFFFFFF);W32(next_setup+0x140+0x14,0xFFFFFFFF);
    W32(local+0x18,2);W32(remote+0x18,2);W32(race+0x24,12);
    W32(data+0xACC,1000);
}
void NativeLocalFinish(bool won,int paid) {
    G<uint8_t>(local+4)=1;G<uint8_t>(local+0x5D)=won;
    W32(race+0x1250,R32(race+0x1250)+1);W32(machine+4,7);
    G<uint8_t>(race+0x0E)=1;W32(rewards+0xBC,paid);
}
void SetupAuthorityTests() {
    Fixture();
    constexpr uint32_t db=0x150000,names=0x151000,name=0x152000;
    W32(0x557EE4,db);W32(db+0x18,names);W32(names+7*4,name);
    const char full[]="circuit\\easy\\same_prefix_but_complete_race_name";
    std::memcpy(mod::HostPtr(name),full,sizeof full);
    for(uint32_t s:{setup,next_setup})std::memcpy(mod::HostPtr(s+0x68),full,sizeof full);
    mod::race::Start start{};start.serial=501;start.city=0;start.race_idx=7;
    mod::race::OnStart(start);
    mod::race::Setup authority{501,2,0xFFFFFF1F,100,0,0,7,{}};
    int32_t model=69;std::memcpy(authority.opponents[0],&model,4);authority.opponents[0][4]=1;
    std::memcpy(authority.opponents[0]+5,"vp_d_scion_tc_05_7",18);
    mod::race::OnSetup(authority);
    W32(race,0x46A7D8);mod::race::ApplyHostSetup();
    assert(R32(setup+0x3D4)==0xFFFFFF1F && R32(next_setup+0x3D4)==0xFFFFFF1F);
    assert(R32(setup+0x40)==100 && R32(next_setup+0x40)==100);
    assert(R32(setup+0x274)==69 && R32(next_setup+0x274)==69);
    auto stale=authority;stale.revision=1;stale.opponent_mask=0xF;
    mod::race::OnSetup(stale);mod::race::ApplyHostSetup();assert(R32(setup+0x3D4)==0xFFFFFF1F);
    authority.revision=3;authority.serial=500;mod::race::OnSetup(authority);
    assert(mod::race::g_setup_in.serial==501);
    // A setup for another filename must not be touched; already allocated opponents must not be resized.
    G<uint8_t>(next_setup+0x68)='x';W32(next_setup+0x3D4,0xABC);
    mod::race::ApplyHostSetup();assert(R32(next_setup+0x3D4)==0xABC);
    W32(race,0x46B2D8);W32(setup+0x3D4,0xDEF);mod::race::ApplyHostSetup();
    assert(R32(setup+0x3D4)==0xDEF);
    // The host only sends a complete NEXT setup with the exact current race identity.
    mod::race::BeginRace(502);mod::race::g_start_out=start;mod::race::g_start_out.serial=502;
    std::memcpy(mod::HostPtr(next_setup+0x68),full,sizeof full);
    W32(next_setup+0x3D4,0xFFFFFF1F);W32(next_setup+0x40,100);
    mod::race::CaptureHostSetup();mod::race::Setup sent{};
    assert(mod::race::TakeSetup(sent) && sent.serial==502 && sent.opponent_mask==0xFFFFFF1F);
    assert(!mod::race::TakeSetup(sent));
    puts("PASS: authoritative setup mask before allocation; full filename, generation and revision guards.");
}
#include "ai_memory_tests.inc"
#include "finish_gate_tests.inc"
#include "traffic_memory_tests.inc"
#include "car_config_tests.inc"
int main() {
    SetupAuthorityTests();
    AiMemoryTests();
    FinishGateTests();
    FinishTerminalRankTests();
    // the guest pad poll found in each known runner build, mapped as an image only (never run); the staged test
    // runner follows xrevive's updates (0x305CB4, then 0x306984): any one `call [rip+iat]` site there
    struct Build { const wchar_t* path; uintptr_t rva; };
    for (const Build& b : {Build{L"D:/dev/mc3-coop-data/xrun/MC3Runner.exe", 0},
                           Build{L"D:/dev/mc3-coop-data/testgame/Midnight Club 3 DUB Edition Remix/"
                                 L".xrevive-8c22479443f74b1c5a543f0c5b0f4a7966472812e1caf0f3bb0729d8c3009221.exe", 0x4E35C4}}) {
        HMODULE m = LoadLibraryExW(b.path, nullptr, DONT_RESOLVE_DLL_REFERENCES);
        if (!m) continue;  // that build is not on this PC
        uintptr_t r = mod::game::FindGuestInputReturn(reinterpret_cast<const uint8_t*>(m));
        if (b.rva) assert(r == reinterpret_cast<uintptr_t>(m) + b.rva);
        else assert(r && reinterpret_cast<const uint8_t*>(r)[-6] == 0xFF && reinterpret_cast<const uint8_t*>(r)[-5] == 0x15);
        FreeLibrary(m);
    }
    Fixture();
    uint8_t cfg[mod::car::kCarCfgBytes]{};cfg[0xE3]='x';
    mod::car::SetPartnerCar(1,cfg);mod::car::Tick(test_now);
    assert(R32(setup+0x154)==0xFFFFFFFF);assert(R32(next_setup+0x154)==0xFFFFFFFF);
    assert(R32(setup+0x50)==1 && R32(setup+0x4C)==2);
    mod::race::Tick(true,true);
    assert(G<uint8_t>(remote+4)==1 && G<uint8_t>(remote+6)==1);
    assert(G<uint8_t>(remote+0x5D)==0 && R32(remote+0x18)==0);
    assert(G<uint16_t>(remote+0x34)==0xFFFF && G<uint8_t>(remote+0x48)==12);
    assert(R32(remote+0x4C)==0xFFFFFFFF && R32(race+0x1250)==1 && R32(race+0x1258)==2);
    for(int i=0;i<20;i++)mod::race::Tick(true,true);
    assert(R32(race+0x1250)==1); // frame repetition must not count a second finish
    mod::car::Remove(1);mod::car::Tick(test_now);mod::race::Tick(true,false);
    assert(R32(race+0x1250)==1 && R32(setup+0x4C)==2 && R32(next_setup+0x4C)==2);
    assert(G<uint8_t>(race+0x6F4)==0); // no disconnect timer forfeit anymore
    // Out of a race, only the career's worlds (mode 2) get the partner: the garage (5) and front end (3) build
    // their players from these setups, and a second human there ends the runner.
    Fixture();W32(holder+4,0);
    mod::car::SetPartnerCar(1,cfg);
    for(uint32_t mode:{5u,3u}) {
        W32(0x53F2D4,mode);mod::car::Tick(test_now);
        assert(R32(setup+0x4C)==1 && R32(next_setup+0x4C)==1);
    }
    W32(0x53F2D4,2);mod::car::Tick(test_now);
    assert(R32(setup+0x4C)==2 && R32(next_setup+0x4C)==2);
    mod::car::Remove(1);
    W32(holder+4,race);W32(0x53F2D4,0);
    // A series loads its next race natively (same race address reused): the serial's shared win must not carry over.
    Fixture();mod::race::BeginRace(9);
    W32(machine+4,5);mod::race::Tick(false,true);
    mod::race::g_partner_won=true;
    W32(machine+4,7);mod::race::Tick(false,true);
    W32(machine+4,4);mod::race::Tick(false,true);
    assert(mod::race::g_race_serial==0 && !mod::race::g_partner_won && mod::race::g_gate==mod::race::GatePhase::Off);
    G<uint8_t>(local+0x5D)=0;W32(machine+4,5);mod::race::Tick(false,true);
    assert(G<uint8_t>(local+0x5D)==0);

    Fixture();mod::race::Tick(true,true);NativeLocalFinish(true,700);
    mod::race::Tick(true,true);
    uint32_t serial=0;int32_t prize=0;
    assert(mod::race::TakeWon(serial,prize) && serial==101 && prize==700); // state 5 -> 7 cannot lose a win
    mod::race::Tick(true,true);assert(!mod::race::TakeWon(serial,prize));

    Fixture();mod::race::Tick(false,true);W32(machine+4,5);
    mod::race::OnPartnerWon(100,99999);mod::race::OnPartnerWon(101,-1);
    mod::race::Tick(false,true);assert(!G<uint8_t>(local+0x5D));
    mod::race::OnPartnerWon(101,700);mod::race::Tick(false,true);assert(G<uint8_t>(local+0x5D));
    NativeLocalFinish(true,200);mod::race::Tick(false,true);
    assert(R32(data+0xACC)==1500 && R32(rewards+0xBC)==700 && G<uint8_t>(career+0x20)==1);
    for(int i=0;i<20;i++){mod::race::OnPartnerWon(101,700);mod::race::Tick(false,true);}
    assert(R32(data+0xACC)==1500 && !mod::race::TakeWon(serial,prize));
    mod::race::OnPartnerWon(101,800);mod::race::Tick(false,true);
    assert(R32(data+0xACC)==1600 && R32(rewards+0xBC)==800);
    mod::race::OnPartnerWon(102,9000);mod::race::Tick(false,true);assert(R32(data+0xACC)==1600);
    W32(holder+4,race+0x2000);W32(race+0x2000,0x46B2D8);mod::race::Tick(false,false);
    assert(!mod::race::g_partner_won && mod::race::g_race_serial==0); // no prize leaks into a later solo world

    Fixture();mod::race::OnPartnerWon(101,100);W32(machine+4,7);G<uint8_t>(race+0x0E)=1;
    W32(data+0xACC,INT_MAX-1);W32(rewards+0xBC,0);mod::race::Tick(false,true);
    assert(R32(data+0xACC)==INT_MAX-1 && R32(rewards+0xBC)==0);
    Fixture();W32(race,0x469378);W32(race+0x1250,0x12345678);mod::race::Tick(true,true);
    assert(R32(race+0x1250)==0x12345678 && !G<uint8_t>(remote+4)); // other modes do not have a dword finish counter
    Fixture();G<uint8_t>(local+4)=1;mod::race::Tick(true,true);assert(R32(race+0x1250)==0);
    Fixture();W32(players+0x28,1);mod::race::Tick(true,true);assert(R32(race+0x1250)==0);
    Fixture();mod::race::g_lights_until=test_now+200;mod::race::Tick(true,true);
    assert(mod::race::ChallengeInput());
    mod::race::Tick(true,false);assert(!mod::race::ChallengeInput());
    mod::race::g_lights_until=0;

    Fixture();
    constexpr uint32_t root=0x130000,tm=0x131000,slots=0x132000,types=0x133000;
    W32(0x50152C,root);W32(root+8,tm);W32(tm,0x44CE90);G<int16_t>(tm+0x12C)=2;
    W32(tm+0x130,slots);W32(slots,0x44D1CC);W32(slots+0x21C,0x44D1CC);W32(tm+0x13C,1);
    W32(tm+0x30,types);G<uint16_t>(tm+0x34)=1;W32(types,0x134000);
    for(uint32_t s:{slots,slots+0x21C}){G<uint16_t>(s+0x2C)=0xFFFF;W32(s+0x138,0x134000);}
    W32(tm+0x68,48);W32(tm+0x6C,0);G<uint8_t>(0x4AFCCC)=1;
    auto before=ram;auto desc=mod::traffic::Describe();
    assert(ram==before);assert(desc.find("spawned=0")!=desc.npos && desc.find("target=48")!=desc.npos);
    W32(setup+0x50,2);desc=mod::traffic::Describe();assert(desc.find("target=0")!=desc.npos);
    G<uint8_t>(setup+0x108)=1;W32(tm+0x78,21);desc=mod::traffic::Describe();assert(desc.find("target=21")!=desc.npos);
    // Menus must not take over/free-list traffic. Gameplay takeover must restore roads before result/reset.
    G<uint8_t>(setup+0x108)=0;W32(setup+0x50,1);W32(machine+4,5);
    constexpr uint32_t vehicle=0x140000,car=0x141000,inst=0x142000,collider=0x143000,body=0x144000;
    W32(0x10A000+0x14,vehicle);W32(vehicle+8,car);W32(vehicle+0x1C,inst);
    W32(car+0x68,collider);W32(collider+0x10,body);
    for(uint32_t s:{slots,slots+0x21C}) {
        G<uint16_t>(s+0x2C)=10;W32(s+0xFC,0x135000);W32(s+0x100,0x135014);
        G<float>(s+0x34)=1;G<float>(s+0x40+4)=1;G<float>(s+0x4C+8)=1;G<float>(s+0x58)=123;
    }
    std::vector<std::vector<uint8_t>> packets;
    mod::traffic::Capture(test_now);
    assert(mod::traffic::Snapshot(packets,test_now));
    mod::traffic::Receive(packets[0].data(),int(packets[0].size()));
    W32(machine+4,0);mod::traffic::Tick(true,test_now);
    mod::traffic::Capture(test_now+100);
    assert(R32(slots+0xFC)==0x135000 && !mod::traffic::Snapshot(packets,test_now+100));
    W32(machine+4,5);mod::traffic::Tick(true,test_now);
    assert(R32(slots+0xFC)==0 && R32(slots+0x100)==0);
    W32(machine+4,7);mod::traffic::Tick(true,test_now);
    assert(R32(slots+0xFC)==0x135000 && R32(slots+0x100)==0x135014);
    W32(machine+4,5);mod::traffic::Tick(true,test_now);
    assert(R32(slots+0xFC)==0);
    mod::traffic::Stop();mod::traffic::Tick(false,test_now);
    assert(R32(slots+0xFC)==0x135000 && R32(slots+0x100)==0x135014);
    std::puts("PASS: remote without HUD; once-only completion/disconnect; 5->7 win; shared prize/duplicates/stale IDs/overflow; mode guards; read-only traffic diagnostics indexed by views.");
    TrafficMemoryTests();
    CarConfigTests();
}
