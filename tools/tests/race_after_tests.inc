// Real race-flow protocol/host publication against synthetic RAM; no native execution.
// Include after race_next_tests.inc, whose database fixture is shared here.
namespace race_after_test {
namespace rr=mod::race;
namespace nt=race_next_test;

void Fixture() {
    nt::Fixture();rr::ResetNetwork();rr::BeginRace(101);
    rr::g_bound_ran=rr::g_bound_ended=false;
    test_now=5000;
}

rr::After Decision(uint32_t serial=100,uint32_t revision=1,rr::AfterAction action=rr::AfterAction::Wait) {
    rr::After a{};a.serial=serial;a.revision=revision;a.city=nt::city;a.action=action;
    if(action==rr::AfterAction::Next) { a.next=nt::HostStart();a.next.serial=serial+1; }
    return a;
}

void Valid(const rr::After& a,bool expected) {
    auto before=ram;auto input=a;
    assert(rr::ValidAfter(a)==expected && ram==before && !std::memcmp(&a,&input,sizeof a));
}

void Receive(const rr::After& a) {
    auto before=ram;auto active=rr::g_active_start;auto serial=rr::g_race_serial;
    bool ready=rr::g_active_ready,have=rr::g_have_start;
    rr::OnAfter(a);
    assert(ram==before && rr::g_race_serial==serial && rr::g_active_ready==ready && rr::g_have_start==have);
    assert(!std::memcmp(&active,&rr::g_active_start,sizeof active));
}

void Send(rr::After& a,bool expected) {
    auto before=ram;auto old=a;
    assert(rr::TakeAfter(a)==expected && ram==before);
    if(!expected)assert(!std::memcmp(&a,&old,sizeof a));
    else assert(rr::ValidAfter(a));
}

void Host() {
    Fixture();W32(0x53F2D4,2);W32(machine+4,4);
    rr::g_serial=70;rr::BeginRace(70);
    auto active=nt::HostStart();active.serial=70;
    rr::g_active_start=active;rr::g_active_ready=true;
    rr::g_bound_race=race;rr::g_bound_ran=true;
    rr::PublishHostWaiting(active);
    W32(next_setup+0x3D4,0xFFFFFF1F);W32(next_setup+0x40,3);
    W32(next_setup+0x274,69);G<uint8_t>(next_setup+0x278)=1;
    std::strcpy(static_cast<char*>(mod::HostPtr(next_setup+0x279)),"vp_d_scion_tc_05_7");
}

void Exit(bool should_publish) {
    auto before=ram;auto old=rr::g_after_out;uint32_t revision=rr::g_after_revision;
    rr::ObserveHostExit();assert(ram==before);
    if(!should_publish) {
        assert(rr::g_after_revision==revision && !std::memcmp(old.data(),rr::g_after_out.data(),sizeof old));
        return;
    }
    const auto& decision=rr::g_after_out[70%8];
    assert(decision.serial==70 && decision.action==rr::AfterAction::Street && decision.city==nt::city);
    assert(decision.revision==revision+1 && rr::ValidAfter(decision));
}
}

void RaceAfterTests() {
    namespace at=race_after_test;
    namespace rr=mod::race;
    at::Fixture();
    auto a=at::Decision();at::Valid(a,true);
    a.action=rr::AfterAction::Street;at::Valid(a,true);
    for(int field=0;field<7;field++) {
        a=at::Decision();
        switch(field) {
            case 0:a.serial=0;break;
            case 1:a.revision=0;break;
            case 2:a.reserved=1;break;
            case 3:a.city=-1;break;
            case 4:a.city=4;break;
            case 5:a.action=static_cast<rr::AfterAction>(3);break;
            case 6:a.next.serial=1;break;
        }
        at::Valid(a,false);
    }
    a=at::Decision(100,1,rr::AfterAction::Next);at::Valid(a,true);
    for(int field=0;field<14;field++) {
        a=at::Decision(100,1,rr::AfterAction::Next);
        switch(field) {
            case 0:a.next.serial=100;break;
            case 1:a.next.serial=99;break;
            case 2:a.next.serial=0;break;
            case 3:a.next.serial=100+0x80000000u;break;
            case 4:a.next.pad=1;break;
            case 5:a.next.city=1;break;
            case 6:a.next.race_idx=-1;break;
            case 7:a.next.race_idx=160;break;
            case 8:std::memset(a.next.record,0,4);break;
            case 9:std::memset(a.next.record+4,0,2);break;
            case 10:std::memset(a.next.record+6,0,2);break;
            case 11:a.next.record[8]=0;break;
            case 12:std::memset(a.next.name,'x',sizeof a.next.name);break;
            case 13:a.reserved=0x8000;break;
        }
        at::Valid(a,false);
    }
    // Serial rollover uses signed distance, skipping zero; it is a newer start after FFFFFFFF.
    a=at::Decision(0xFFFFFFFF,9,rr::AfterAction::Next);a.next.serial=1;at::Valid(a,true);

    // Receiving decisions retains eight serial slots without activating a race or touching guest memory.
    at::Fixture();
    for(uint32_t i=0;i<8;i++)at::Receive(at::Decision(201+i,1+i));
    for(uint32_t i=0;i<8;i++)assert(rr::DecisionFor(201+i) && rr::HostControls(201+i));
    a=at::Decision(202,20,rr::AfterAction::Street);at::Receive(a);
    assert(rr::DecisionFor(202)->revision==20 && rr::DecisionFor(202)->action==rr::AfterAction::Street);
    auto in=rr::g_after_in;
    for(uint32_t revision:{19u,20u}) {
        at::Receive(at::Decision(202,revision));assert(!std::memcmp(in.data(),rr::g_after_in.data(),sizeof in));
    }
    // Ring collision and reordered old datagram: newer global host revision wins the slot.
    at::Receive(at::Decision(210,21));assert(!rr::DecisionFor(202) && rr::DecisionFor(210));
    in=rr::g_after_in;at::Receive(at::Decision(202,20));
    assert(!std::memcmp(in.data(),rr::g_after_in.data(),sizeof in));
    a=at::Decision(211,22,rr::AfterAction::Next);at::Receive(a);
    assert(rr::HostControls(211) && rr::HostControls(212) && !rr::DecisionFor(212));
    in=rr::g_after_in;a.reserved=1;at::Receive(a);
    assert(!std::memcmp(in.data(),rr::g_after_in.data(),sizeof in));
    auto before=ram;rr::ResetNetwork();assert(ram==before && rr::g_race_serial==0 && rr::g_serial==0);
    for(const auto& d:rr::g_after_in)assert(!d.serial);
    for(const auto& d:rr::g_after_out)assert(!d.serial);
    assert(!rr::DecisionFor(211) && !rr::HostControls(212));
    at::Receive(at::Decision(1,1));assert(rr::DecisionFor(1)->revision==1); // new session may restart both counters

    // Repeated sends rotate through retained history, with a100ms interval rather than consuming a decision.
    at::Fixture();rr::After sent{};at::Send(sent,false);
    for(uint32_t serial=101;serial<=108;serial++) {
        before=ram;rr::PublishAfter(serial,rr::AfterAction::Wait,at::nt::city);assert(ram==before);
    }
    bool seen[8]{};
    for(int i=0;i<8;i++) {
        at::Send(sent,true);assert(sent.serial>=101 && sent.serial<=108);
        assert(!seen[sent.serial-101]);seen[sent.serial-101]=true;
        at::Send(sent,false);test_now+=99;at::Send(sent,false);test_now+=1;
    }
    for(bool value:seen)assert(value);
    at::Send(sent,true);assert(sent.serial==108); // wrapped history repeats
    rr::PublishAfter(109,rr::AfterAction::Street,at::nt::city);at::Send(sent,true);
    assert(sent.serial==109 && sent.action==rr::AfterAction::Street); // newer publication wakes sending immediately
    for(const auto& d:rr::g_after_out)assert(d.serial!=101);
    rr::g_after_revision=UINT32_MAX;rr::PublishAfter(110,rr::AfterAction::Wait,at::nt::city);
    assert(rr::g_after_out[110%8].revision==1); // zero is not a valid revision

    // A natively loaded next stage receives a fresh serial and an atomic decision linking its old parent.
    at::Host();before=ram;uint32_t revision=rr::g_after_revision;
    assert(rr::PublishHostSeries() && ram==before);
    assert(rr::g_serial==71 && rr::g_race_serial==71 && rr::g_active_start.serial==71 && rr::g_active_ready);
    assert(rr::g_start_out.serial==71 && !rr::g_out_start && !rr::g_bound_race);
    const auto& decision=rr::g_after_out[70%8];
    assert(decision.serial==70 && decision.revision==revision+1 && decision.action==rr::AfterAction::Next);
    assert(decision.next.serial==71 && decision.next.city==at::nt::city && decision.next.race_idx==at::nt::index);
    assert(rr::ValidAfter(decision));
    assert(rr::g_setup_out.serial==71 && rr::g_setup_out.revision==1 && rr::g_setup_out.opponent_mask==0xFFFFFF1F);
    assert(rr::g_setup_out.opponent_limit==3 && rr::g_setup_out.opponents[0][4]==1);
    uint32_t model=0;std::memcpy(&model,rr::g_setup_out.opponents[0],4);assert(model==69);
    at::Send(sent,true);assert(sent.serial==70 && sent.next.serial==71);
    at::Host();rr::g_serial=UINT32_MAX;rr::BeginRace(UINT32_MAX);before=ram;
    assert(rr::PublishHostSeries() && ram==before && rr::g_serial==1 && rr::g_race_serial==1);
    assert(rr::g_after_out[UINT32_MAX%8].serial==UINT32_MAX && rr::g_after_out[UINT32_MAX%8].next.serial==1);
    for(uint32_t mode:{0u,3u,5u}) {
        at::Host();W32(0x53F2D4,mode);before=ram;auto history=rr::g_after_out;
        assert(!rr::PublishHostSeries() && ram==before && rr::g_serial==70 && rr::g_race_serial==70);
        assert(!std::memcmp(history.data(),rr::g_after_out.data(),sizeof history));
    }
    for(uint32_t state:{0u,1u,7u,12u}) {
        at::Host();W32(machine+4,state);before=ram;
        assert(!rr::PublishHostSeries() && ram==before && rr::g_serial==70);
    }
    at::Host();G<uint16_t>(data+0x58)=123;before=ram;
    assert(!rr::PublishHostSeries() && ram==before && rr::g_serial==70);

    // A running/terminal non-cruise world has not left; garage/front end or the cruise publish Street once.
    at::Host();at::Exit(false);W32(machine+4,12);at::Exit(false);
    W32(0x53F2D4,5);at::Exit(true);at::Exit(false);
    at::Host();W32(0x53F2D4,3);at::Exit(true);
    at::Host();W32(race,0x46A7D8);at::Exit(true);
    at::Host();W32(0x53F2D4,5);rr::g_active_ready=false;at::Exit(false);
    at::Host();W32(0x53F2D4,5);rr::g_bound_ran=false;at::Exit(false);
    at::Host();W32(0x53F2D4,5);rr::g_active_start.serial=0;at::Exit(false);
    rr::ResetNetwork();::Fixture();test_now=1000;
    puts("PASS: After validation/history/revisions/retransmit/reset, native-series serial+setup and host exit; no guest RAM writes.");
}
