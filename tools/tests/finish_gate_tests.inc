// Real race.cpp gate against synthetic RAM. The two native-event helpers below MODEL the documented
// counter/result contract; they do not call guest functions and cannot replace the isolated live test.
namespace finish_gate_test {
constexpr uint32_t serial=700, session=0x10C000, game=0x10D000, hud=0x10E000;
constexpr uint32_t vehicle=0x140000, car=0x141000, inst=0x142000, collider=0x143000, body=0x144000, cps=0x150000;
unsigned reports=0, payments=0;

void Fixture(unsigned checkpoints=7) {
    ::Fixture();
    test_now=1000;
    _putenv_s("MC3COOP_FINISH_GATE", "1");
    mod::race::BeginRace(serial);
    reports=payments=0;
    W32(0x557EFC,session);W32(session+0x10,0xFFFFFFFF);
    W32(0x557ED0,game);W32(game+4,4);W32(machine+4,5);
    W32(0x10A000+0x58,hud);W32(0x10A000+0x14,vehicle);
    W32(vehicle+8,car);W32(vehicle+0x1C,inst);W32(car+0x68,collider);W32(collider+0x10,body);
    for(unsigned j=0;j<3;j++)G<float>(inst+0x10+(j*3+j)*4)=1;
    W32(race+0x24,checkpoints);W32(race+0x28,cps);
    for(unsigned i=0;i<checkpoints;i++) {
        G<float>(cps+i*0x120+0xFC)=1000.f+float(i)*10.f;
        G<float>(cps+i*0x120+0x100)=10.f;
        G<float>(cps+i*0x120+0x104)=2000.f+float(i)*10.f;
    }
    G<uint8_t>(local+7)=1;
    G<float>(machine+0x0C)=120.f;
    G<uint16_t>(data+0xA78)=2;
}

void Arm() {
    mod::race::Tick(true,true);
    assert(mod::race::g_gate==mod::race::GatePhase::Guarding);
    assert(G<uint8_t>(remote+4)==1 && G<uint8_t>(remote+6)==1 && R32(remote+0x18)==0);
    assert(R32(race+0x1250)==0 && R32(setup+0x154)==0xFFFFFFFF && R32(setup+0x50)==1);
}

void FirstNativeFinish(bool won) {
    assert(R32(race+0x1250)==0 && !G<uint8_t>(local+4));
    G<uint8_t>(local+4)=1;G<uint8_t>(local+0x5D)=uint8_t(won);
    // Mirror the induced-loss live case: arrested, still eligible, not DNF. Shared preset wins before this test.
    G<uint8_t>(local+5)=uint8_t(!won);
    W32(local+0x18,0);G<float>(local+0x10)=120;G<float>(local+0x1C)=120;
    G<float>(local+0x24)=40;G<float>(local+0x28)=32;
    G<uint8_t>(local+0x48)=uint8_t(R32(race+0x24));
    W32(race+0x1250,1);W32(machine+4,6);
}

void ReplayNativeFinish(int paid=0) {
    if(G<uint8_t>(local+4))return; // native 1E0744 finished guard: duplicate callback cannot count/pay twice
    assert(R32(local+0x18)==1 && R32(race+0x1250)==1 && G<uint8_t>(remote+4));
    G<float>(local+0x24)=G<float>(machine+0x0C)-G<float>(local+0x1C);
    if(G<float>(local+0x28)<=0 || G<float>(local+0x24)<G<float>(local+0x28))
        G<float>(local+0x28)=G<float>(local+0x24);
    G<float>(local+0x10)=G<float>(machine+0x0C);W32(local+0x18,0);G<uint8_t>(local+4)=1;
    W32(race+0x1250,2);W32(machine+4,7);
    if(!G<uint8_t>(race+0x0D)) {
        reports++;
        if(G<uint8_t>(local+0x5D))G<uint16_t>(data+0xA78)++;
        G<uint8_t>(race+0x0D)=1;
    }
    if(!G<uint8_t>(race+0x0E)) {
        payments++;W32(rewards+0xBC,uint32_t(paid));W32(data+0xACC,R32(data+0xACC)+uint32_t(paid));
        G<uint8_t>(race+0x0E)=1;
    }
}
} // namespace finish_gate_test

void FinishGateTests() {
    namespace gate=finish_gate_test;
    using Phase=mod::race::GatePhase;
    mod::race::Finished terminal{};
    gate::Fixture();gate::Arm();gate::FirstNativeFinish(false);mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Waiting && R32(machine+4)==6 && R32(race+0x1250)==1);
    assert(!G<uint8_t>(race+0x0D) && !G<uint8_t>(race+0x0E) && R32(data+0xACC)==1000);
    assert(mod::race::TakeFinished(terminal) && terminal.serial==gate::serial && !terminal.won && !terminal.dnf);
    assert(!mod::race::TakeFinished(terminal));test_now+=499;assert(!mod::race::TakeFinished(terminal));
    test_now++;assert(mod::race::TakeFinished(terminal) && !terminal.won); // losses retransmit independently
    mod::race::OnPartnerFinished({gate::serial-1,1,0});mod::race::OnPartnerFinished({gate::serial,2,0});
    mod::race::OnPartnerFinished({gate::serial,1,1});mod::race::Tick(true,true);
    assert(!mod::race::g_peer_finished && mod::race::g_gate==Phase::Waiting);
    for(unsigned i=0;i<5;i++)mod::race::Tick(true,true);
    assert(R32(race+0x1250)==1 && G<uint8_t>(local+4));
    test_now+=1500;G<float>(machine+0x0C)=122;
    mod::race::OnPartnerFinished({gate::serial,1,0});mod::race::OnPartnerWon(gate::serial,800);
    mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Replay && !G<uint8_t>(local+4) && G<uint8_t>(local+0x5D));
    assert(R32(race+0x1250)==1 && G<uint8_t>(remote+4) && !G<uint8_t>(race+0x0D) && !G<uint8_t>(race+0x0E));
    assert(G<float>(local+0x1C)==80 && G<float>(local+0x28)==32 && R32(local+0x18)==1);
    assert(G<uint16_t>(local+0x34)==6 && G<uint8_t>(local+0x48)==6 && G<uint8_t>(local+0x49)==6);
    assert(R32(local+0x4C)==0x3F && R32(local+0x50)==0 && R32(local+0x54)==0x3F && R32(local+0x58)==0);
    assert(G<float>(gate::inst+0x34)==1060 && G<float>(gate::inst+0x38)==10.5f && G<float>(gate::inst+0x3C)==2060);
    auto rearmed=ram;
    for(unsigned i=0;i<5;i++) {
        mod::race::OnPartnerFinished({gate::serial,1,0});mod::race::OnPartnerWon(gate::serial,800);
        mod::race::Tick(true,true);
    }
    assert(ram==rearmed); // no repeated rearm/move/counter update while awaiting the native event
    gate::ReplayNativeFinish();mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Done && R32(race+0x1250)==2 && G<uint8_t>(local+0x5D));
    assert(G<uint16_t>(data+0xA78)==3 && R32(data+0xACC)==1800 && R32(rewards+0xBC)==800);
    assert(G<float>(local+0x28)==32 && G<float>(local+0x24)==42 && gate::reports==1 && gate::payments==1);
    gate::ReplayNativeFinish();mod::race::OnPartnerWon(gate::serial,800);mod::race::Tick(true,true);
    assert(G<uint16_t>(data+0xA78)==3 && R32(data+0xACC)==1800 && gate::reports==1 && gate::payments==1);

    gate::Fixture();gate::Arm();gate::FirstNativeFinish(false);mod::race::Tick(true,true);
    mod::race::OnPartnerFinished({gate::serial,0,0});mod::race::OnPartnerFinished({gate::serial,1,0});
    mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Replay && !G<uint8_t>(local+0x5D));
    gate::ReplayNativeFinish(100);mod::race::Tick(true,true);
    assert(G<uint16_t>(data+0xA78)==2 && R32(data+0xACC)==1100); // two losses resolve without deadlock or victory

    gate::Fixture();gate::Arm();mod::race::OnPartnerFinished({gate::serial,1,0});mod::race::Tick(true,true);
    assert(!G<uint8_t>(local+0x5D)); // do not disguise the genuine local result with a premature shared-win preset
    gate::FirstNativeFinish(false);mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Replay && mod::race::TakeFinished(terminal) && !terminal.won);

    gate::Fixture();gate::Arm();gate::FirstNativeFinish(true);mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Replay && mod::race::TakeFinished(terminal) && terminal.won);
    assert(!mod::race::g_peer_finished); // an actual local winner need not wait for the peer

    gate::Fixture();gate::Arm();gate::FirstNativeFinish(false);mod::race::Tick(true,true);
    test_now+=mod::race::kFinishWaitMs-1;mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Waiting);
    test_now++;mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Replay && !G<uint8_t>(local+0x5D));
    assert(std::string(mod::race::g_gate_reason)=="replay_timeout_local_result");
    gate::ReplayNativeFinish();mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Done && G<uint16_t>(data+0xA78)==2 && R32(race+0x1250)==2);
    test_now+=20000; // peer metadata arrives well AFTER timeout's native defeat report/payment
    mod::race::Finished late{gate::serial,1,0,0,0,90.5f,24.75f,12};
    mod::race::OnPartnerFinished(late);mod::race::OnPartnerWon(gate::serial,800);mod::race::Tick(true,true);
    assert(!mod::race::g_partner_won && mod::race::g_peer_finished && !mod::race::g_partner_prize);
    assert(!G<uint8_t>(remote+6) && G<float>(remote+0x10)==90.5f && G<float>(remote+0x3C)==24.75f && R32(remote+0x44)==12);
    assert(mod::race::g_gate==Phase::Done && G<uint8_t>(local+4) && !G<uint8_t>(local+0x5D));
    assert(R32(race+0x1250)==2 && G<uint16_t>(data+0xA78)==2 && R32(data+0xACC)==1000 && R32(rewards+0xBC)==0);
    assert(G<uint8_t>(race+0x0D) && G<uint8_t>(race+0x0E) && gate::reports==1 && gate::payments==1);
    auto timed_out_final=ram;
    late.score=9999;mod::race::OnPartnerFinished(late);mod::race::OnPartnerWon(gate::serial,800);
    mod::race::Tick(true,true);assert(ram==timed_out_final); // first valid metadata wins; no repeated credit/rearm

    gate::Fixture();gate::Arm();gate::FirstNativeFinish(false);mod::race::Tick(true,true);
    mod::race::Tick(true,false);assert(mod::race::g_gate==Phase::Replay && R32(race+0x1250)==1);
    assert(std::string(mod::race::g_gate_reason)=="replay_disconnected");
    assert(R32(setup+0x4C)==2 && R32(next_setup+0x4C)==2 && !G<uint8_t>(local+0x5D));

    // Boundary masks preserve preceding checkpoints and leave only the real final checkpoint unvisited.
    for(unsigned count:{1u,32u,64u}) {
        gate::Fixture(count);gate::Arm();gate::FirstNativeFinish(true);mod::race::Tick(true,true);
        uint64_t preceding=count==1?0:((uint64_t(1)<<(count-1))-1);
        assert(R32(local+0x4C)==uint32_t(preceding) && R32(local+0x50)==uint32_t(preceding>>32));
        assert(R32(local+0x54)==uint32_t(preceding) && R32(local+0x58)==uint32_t(preceding>>32));
        assert(G<uint16_t>(local+0x34)==count-1 && R32(race+0x1250)==1);
    }

    gate::Fixture();W32(race,0x46ABD8);mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Off && R32(race+0x1250)==1); // other common vtables retain legacy completion
    gate::Fixture();G<uint8_t>(race+0x6F4)=1;mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Off);
    gate::Fixture();G<uint8_t>(setup+0x108)=1;mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Off);
    gate::Fixture(65);mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Off);
    gate::Fixture();W32(players+0x28,1);mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Off);
    gate::Fixture();G<uint8_t>(race+0x0D)=1;mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Off);
    gate::Fixture();G<uint8_t>(race+0x0E)=1;mod::race::Tick(true,true);assert(mod::race::g_gate==Phase::Off);
    for(uint32_t guard:{0x0Du,0x0Eu}) {
        gate::Fixture();gate::Arm();gate::FirstNativeFinish(false);mod::race::Tick(true,true);
        G<uint8_t>(race+guard)=1;
        mod::race::OnPartnerFinished({gate::serial,1,0});mod::race::ApplyPeerTerminal(race);auto already_reported=ram;
        mod::race::Tick(true,true);
        assert(mod::race::g_gate==Phase::Blocked && ram==already_reported);
        assert(R32(race+0x1250)==1 && G<uint8_t>(local+4)); // never reopen a permanent result/payment guard
    }
    gate::Fixture();gate::Arm();gate::FirstNativeFinish(true);mod::race::Tick(true,true);
    test_now+=mod::race::kReplayWaitMs;mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Blocked && G<uint8_t>(race+0x6F4)==1 && !G<uint8_t>(local+0x5D));
    assert(!G<uint8_t>(local+4) && R32(race+0x1250)==1 && !G<uint8_t>(race+0x0D) && !G<uint8_t>(race+0x0E));
    assert(std::string(mod::race::g_gate_reason)=="replay_failed_local_forfeit");

    gate::Fixture();gate::Arm();gate::FirstNativeFinish(false);mod::race::Tick(true,true);
    mod::race::BeginRace(gate::serial+1);mod::race::OnPartnerFinished({gate::serial,1,0});
    assert(!mod::race::g_peer_finished && !mod::race::g_local_finished && !mod::race::TakeFinished(terminal));
    gate::Fixture();_putenv_s("MC3COOP_FINISH_GATE", "");mod::race::BeginRace(gate::serial);
    assert(mod::race::g_gate_requested);gate::Arm(); // enabled by default; explicit zero above is the opt-out
    ::Fixture();assert(!mod::race::g_gate_requested); // explicit opt-out remains available after default-on promotion
    puts("PASS: finish gate loss retransmission, late win, both losses, local win, timeout/disconnect, stale/duplicate packets, masks/best-lap reconstruction, native-credit idempotence and unsupported modes.");
}

// Compare the exact human inputs consumed by native pairwise ranking, excluding pointer/cosmetic fields.
std::array<uint32_t,8> HumanRankContext(uint32_t entry) {
    return {G<uint8_t>(entry+4),G<uint8_t>(entry+5),G<uint8_t>(entry+6),R32(entry+0x10),
            R32(entry+0x18),R32(entry+0x3C),R32(entry+0x44),G<uint8_t>(entry+0x5D)};
}

void FinishTerminalRankTests() {
    namespace gate=finish_gate_test;
    using Phase=mod::race::GatePhase;
    static_assert(sizeof(mod::race::Finished)==20);
    mod::race::Finished peer{gate::serial,1,0,0,0,90.5f,64.25f,12}, outgoing{};

    gate::Fixture();gate::Arm();auto no_terminal=ram;
    mod::race::ApplyPeerTerminal(race);assert(ram==no_terminal);
    std::vector<mod::race::Finished> invalid(10,peer);
    invalid[0].serial--;
    invalid[1].arrested=2;
    invalid[2].reserved=1;
    invalid[3].finish_time=-1;
    invalid[4].finish_time=std::numeric_limits<float>::quiet_NaN();
    invalid[5].finish_time=86401;
    invalid[6].progress=std::numeric_limits<float>::infinity();
    invalid[7].progress=1.0e10f;
    invalid[8].dnf=1; // contradictory won + DNF
    invalid[9].won=2;
    for(const auto& packet:invalid) {
        mod::race::OnPartnerFinished(packet);mod::race::ApplyPeerTerminal(race);
        assert(!mod::race::g_peer_finished && ram==no_terminal);
    }

    // World A: this actual local finisher lost/arrested, the real peer won earlier. The remote DNF marker must
    // become that peer's actual rank input without invoking its HUD callback or contributing another count.
    G<float>(local+0x3C)=42.5f;W32(local+0x44,77);
    gate::FirstNativeFinish(false);mod::race::Tick(true,true);
    assert(mod::race::TakeFinished(outgoing) && !outgoing.won && !outgoing.dnf && outgoing.arrested==1);
    assert(outgoing.reserved==0 && outgoing.finish_time==120 && outgoing.progress==42.5f && outgoing.score==77);
    mod::race::OnPartnerFinished(peer);mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Replay && R32(race+0x1250)==1);
    assert(G<uint8_t>(remote+4)==1 && !G<uint8_t>(remote+5) && !G<uint8_t>(remote+6));
    assert(G<float>(remote+0x10)==90.5f && G<float>(remote+0x3C)==64.25f && R32(remote+0x44)==12);
    assert(R32(remote+0x18)==0 && G<uint16_t>(remote+0x34)==0xFFFF && R32(remote+0x4C)==0xFFFFFFFF);
    assert(!G<uint8_t>(remote+0x5D)); // ranking input is imported; remote win/HUD/counter callbacks remain disabled
    G<float>(machine+0x0C)=122;gate::ReplayNativeFinish();
    assert(G<float>(local+0x10)==122);
    mod::race::RestoreLocalTerminalTime(race,7);assert(G<float>(local+0x10)==122); // not GatePhase::Done yet
    mod::race::Tick(true,true);
    assert(mod::race::g_gate==Phase::Done && G<float>(local+0x10)==120 && R32(race+0x1250)==2);
    assert(G<float>(local+0x28)==32 && G<float>(local+0x24)==42 && G<float>(machine+0x0C)==122);
    assert(G<uint8_t>(race+0x0D) && G<uint8_t>(race+0x0E) && G<uint16_t>(data+0xA78)==3);
    auto a_local=HumanRankContext(local), a_remote=HumanRankContext(remote);
    auto settled=ram;
    for(unsigned i=0;i<5;i++) {
        mod::race::OnPartnerFinished(peer);mod::race::ApplyPeerTerminal(race);
        mod::race::RestoreLocalTerminalTime(race,7);
    }
    assert(ram==settled && R32(race+0x1250)==2 && gate::reports==1 && gate::payments==1);
    test_now+=500;assert(mod::race::TakeFinished(outgoing) && outgoing.finish_time==120 && !outgoing.won);

    // World B swaps the same two genuine humans. Native ranking now sees matching contexts modulo slot order.
    gate::Fixture();gate::Arm();G<float>(local+0x3C)=64.25f;W32(local+0x44,12);
    gate::FirstNativeFinish(true);G<float>(local+0x10)=G<float>(local+0x1C)=90.5f;
    mod::race::Tick(true,true);
    mod::race::Finished other{gate::serial,0,0,1,0,120.f,42.5f,77};
    mod::race::OnPartnerFinished(other);mod::race::Tick(true,true);
    G<float>(machine+0x0C)=92.5f;gate::ReplayNativeFinish(800);mod::race::Tick(true,true);
    auto b_local=HumanRankContext(local), b_remote=HumanRankContext(remote);
    // Remote win flags intentionally stay zero: only local shared credit uses +5D. The rank comparator's
    // finish/arrest/DNF/time/laps/metric/score inputs must otherwise match exactly between the two copies.
    a_local[7]=a_remote[7]=b_local[7]=b_remote[7]=0;
    assert(a_local==b_remote && a_remote==b_local && R32(race+0x1250)==2);

    // Do not transplant terminal context into another race/mode/player layout, or an unprotected remote.
    for(unsigned guard=0;guard<7;guard++) {
        gate::Fixture();gate::Arm();mod::race::OnPartnerFinished(peer);
        switch(guard) {
            case 0:W32(race,0x46ABD8);break;
            case 1:W32(setup+0x4C,3);break;
            case 2:W32(players+0x28,1);break;
            case 3:W32(setup+0x154,1);break;
            case 4:G<uint8_t>(remote+4)=0;break;
            case 5:mod::race::g_bound_race=race+0x2000;break;
            case 6:G<uint8_t>(setup+0x108)=1;break;
        }
        auto guarded=ram;mod::race::ApplyPeerTerminal(race);assert(ram==guarded);
    }
    // Original local time can only be restored after its native permanent report has completed.
    gate::Fixture();gate::Arm();gate::FirstNativeFinish(true);mod::race::Tick(true,true);
    G<float>(machine+0x0C)=122;gate::ReplayNativeFinish();mod::race::AdvanceGate(race,7,test_now,true);
    G<uint8_t>(race+0x0D)=0;auto not_reported=ram;
    mod::race::RestoreLocalTerminalTime(race,7);assert(ram==not_reported);
    G<uint8_t>(race+0x0D)=1;mod::race::RestoreLocalTerminalTime(race,7);assert(G<float>(local+0x10)==120);
    ::Fixture();
    puts("PASS: terminal rank context swaps both humans consistently; validated payloads; original local ranking time restored after report only; remote remains guarded and counters idempotent.");
}
