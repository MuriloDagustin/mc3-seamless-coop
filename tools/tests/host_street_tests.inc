// Host-only street initiation against real race.cpp and synthetic guest RAM. No native executable is run.
namespace host_street_test {
namespace rr=mod::race;

void Fixture() {
    ::Fixture();
    rr::g_street_guard=rr::g_street_setup=rr::g_street_career=0;
    rr::g_street_data=rr::g_street_vehicle=rr::g_street_machine=0;rr::g_street_city=-1;
    rr::g_street_saved=0;
    rr::g_have_start=false;rr::g_start_done=false;rr::g_start_in={};rr::g_done_serial=0;
    rr::g_phase=0;rr::g_phase_at=0;rr::g_lights_until=0;rr::g_fields_lent=false;
    rr::g_cancel_marker=false;rr::g_fields_data=0;rr::g_fields_id=0;
    rr::g_phase_cruise=rr::g_phase_setup=rr::g_phase_data=rr::g_phase_serial=0;
    rr::g_phase_city=rr::g_phase_index=-1;
    rr::g_notice.clear();
    W32(0x53F2D4,2);W32(setup+0x18,1);W32(next_setup+0x18,1);
    W32(race,0x46A7D8);W32(machine+4,6);W32(career+0x24,3);W32(career+0x2C,0);
}

mod::race::Start Start(uint32_t serial=301) {
    mod::race::Start out{};out.serial=serial;out.city=0;out.race_idx=7;
    std::strcpy(out.name,"circuit\\easy\\host_authorized");return out;
}

void AssertFreshNoWrite(bool hosting=false,bool connected=true) {
    auto before=ram;
    rr::StreetGuard(hosting,connected);
    assert(ram==before && rr::g_street_guard==0);
}

void AssertAbandonedWithoutWrite() {
    auto before=ram;
    rr::StreetGuard(false,false);
    assert(ram==before && rr::g_street_guard==0 && rr::g_street_setup==0 && rr::g_street_career==0);
    assert(rr::g_street_data==0 && rr::g_street_vehicle==0 && rr::g_street_machine==0 && rr::g_street_city==-1);
}

void ReplaceIdentityPart(int part) {
    switch(part) {
        case 0:W32(career+0x0C,0x183000);break;
        case 1:G<int8_t>(setup)=1;break;
        case 2:W32(0x557EEC,0x181000);W32(0x181000+4,6);break;
        case 3:W32(0x10A000+0x14,0x182000);break;
    }
}
}

void HostStreetTests() {
    namespace st=host_street_test;
    namespace rr=mod::race;

    // The borrowed byte is the only RAM modification: no setup/AI/player/career rewriting.
    st::Fixture();G<uint8_t>(race+0x0C)=0x31;G<uint8_t>(race+0x0E)=0x42;
    W32(race+0x1250,0x55667788);auto before=ram;
    rr::StreetGuard(false,true);
    assert(rr::g_street_guard==race && rr::g_street_setup==setup && rr::g_street_career==career);
    assert(G<uint8_t>(race+0x0D)==1 && rr::g_street_saved==0);
    before[race+0x0D]=1;assert(ram==before);
    for(int i=0;i<20;i++)rr::StreetGuard(false,true);
    assert(ram==before); // repeated hooks are idempotent
    G<uint8_t>(race+0x0D)=0;rr::StreetGuard(false,true);
    assert(ram==before); // renew after a native cruise reset without losing the saved original
    assert(rr::Describe().find("street_hold=1")!=std::string::npos);

    // Normal release restores only the value owned by this guard, including role/disconnect changes.
    rr::StreetGuard(false,false);before[race+0x0D]=0;
    assert(ram==before && rr::g_street_guard==0);
    rr::StreetGuard(false,true);rr::StreetGuard(true,true);
    assert(ram==before && rr::g_street_guard==0);
    st::Fixture();st::AssertFreshNoWrite(true,true);
    st::Fixture();st::AssertFreshNoWrite(false,false);

    // A pre-existing native result is never borrowed or cleared later.
    st::Fixture();G<uint8_t>(race+0x0D)=1;st::AssertFreshNoWrite();
    st::AssertAbandonedWithoutWrite();
    for(uint32_t state:{0u,1u,7u,8u,11u,12u}) {
        st::Fixture();W32(machine+4,state);st::AssertFreshNoWrite();
    }
    for(uint32_t mode:{3u,5u}) {
        st::Fixture();W32(0x53F2D4,mode);st::AssertFreshNoWrite();
    }
    st::Fixture();W32(race,0x46B2D8);st::AssertFreshNoWrite();
    st::Fixture();W32(setup+0x18,4);st::AssertFreshNoWrite();
    st::Fixture();G<uint8_t>(setup+0x108)=1;st::AssertFreshNoWrite();
    st::Fixture();W32(career+0x2C,4);G<uint16_t>(career+0x34)=21;st::AssertFreshNoWrite();

    // The host's pending authorization releases BEFORE PartnerTick would reject cruise+0D.
    st::Fixture();rr::StreetGuard(false,true);auto start=st::Start();rr::OnStart(start);
    assert(rr::g_have_start && rr::g_start_in.serial==start.serial && G<uint8_t>(race+0x0D)==1);
    rr::StreetGuard(false,true);
    assert(rr::g_street_guard==0 && G<uint8_t>(race+0x0D)==0);
    // g_have_start remains set after consumption: an old serial must not leave street starts authorized.
    rr::g_done_serial=start.serial;rr::StreetGuard(false,true);
    assert(rr::g_have_start && rr::g_street_guard==race && G<uint8_t>(race+0x0D)==1);
    rr::g_phase=1;rr::StreetGuard(false,true);
    assert(rr::g_street_guard==0 && G<uint8_t>(race+0x0D)==0);
    rr::g_phase=0;

    // Never restore a later native result or an object which now denotes another race.
    st::Fixture();rr::StreetGuard(false,true);W32(machine+4,7);st::AssertAbandonedWithoutWrite();
    st::Fixture();rr::StreetGuard(false,true);W32(race,0x46B2D8);st::AssertAbandonedWithoutWrite();
    st::Fixture();rr::StreetGuard(false,true);W32(setup+0x18,4);st::AssertAbandonedWithoutWrite();
    // Menus may retain the exact old world. Suspend ownership without writes so it remains restorable on return.
    for(uint32_t mode:{3u,5u}) {
        st::Fixture();rr::StreetGuard(false,true);W32(0x53F2D4,mode);before=ram;
        for(int i=0;i<4;i++)rr::StreetGuard(false,true);
        assert(ram==before && rr::g_street_guard==race && rr::g_street_saved==0);
        // A host start arriving there must not destroy this suspended lease or write to stale game objects.
        start=st::Start(500+mode);rr::OnStart(start);rr::Tick(false,true);
        assert(ram==before && rr::g_street_guard==race && rr::g_done_serial!=start.serial);
        assert(rr::Notice().find("O host iniciou uma corrida")!=std::string::npos);
        W32(0x53F2D4,2);rr::StreetGuard(false,true);
        assert(rr::g_street_guard==0 && G<uint8_t>(race+0x0D)==0); // released before PartnerTick can arm
        // Offline while in the garage must still defer restoration until this exact street world returns.
        st::Fixture();rr::StreetGuard(false,true);W32(0x53F2D4,mode);before=ram;
        rr::Tick(false,false);rr::Tick(false,false);
        assert(ram==before && rr::g_street_guard==race);
        W32(0x53F2D4,2);rr::Tick(false,false);
        assert(rr::g_street_guard==0 && G<uint8_t>(race+0x0D)==0);
        // Returning connected without a new authorization keeps the existing street hold until release.
        st::Fixture();rr::StreetGuard(false,true);W32(0x53F2D4,mode);rr::StreetGuard(false,true);
        W32(0x53F2D4,2);rr::StreetGuard(false,true);
        assert(rr::g_street_guard==race && G<uint8_t>(race+0x0D)==1);
        rr::StreetGuard(true,true);assert(rr::g_street_guard==0 && G<uint8_t>(race+0x0D)==0);
        // A suspended lease is never retained once one part of the world identity changes in menus.
        for(int part=0;part<4;part++) {
            st::Fixture();rr::StreetGuard(false,true);W32(0x53F2D4,mode);rr::StreetGuard(false,true);
            st::ReplaceIdentityPart(part);st::AssertAbandonedWithoutWrite();
        }
    }
    st::Fixture();rr::StreetGuard(false,true);G<uint8_t>(setup+0x108)=1;st::AssertAbandonedWithoutWrite();
    st::Fixture();rr::StreetGuard(false,true);W32(career+0x2C,4);st::AssertAbandonedWithoutWrite();

    // Pointer identity protects both setup and career replacements, even while a stale cruise stays mapped.
    st::Fixture();rr::StreetGuard(false,true);W32(0x53F2E0,next_setup);st::AssertAbandonedWithoutWrite();
    st::Fixture();rr::StreetGuard(false,true);constexpr uint32_t other_career=0x180000;
    W32(other_career+0x0C,data);W32(0x557F0C,other_career);st::AssertAbandonedWithoutWrite();
    // Reused top-level pointers are insufficient: new save data, city, machine or local vehicle changes ownership.
    for(int part=0;part<4;part++) {
        st::Fixture();rr::StreetGuard(false,true);st::ReplaceIdentityPart(part);
        st::AssertAbandonedWithoutWrite();
        // The connected renewal path must abandon too, and must not borrow a pre-existing 1 in the new context.
        st::Fixture();rr::StreetGuard(false,true);st::ReplaceIdentityPart(part);before=ram;
        rr::StreetGuard(false,true);
        assert(ram==before && rr::g_street_guard==0);
        // When the new context has reset its byte to 0, it can acquire a fresh lease with its new identity.
        G<uint8_t>(race+0x0D)=0;rr::StreetGuard(false,true);
        assert(G<uint8_t>(race+0x0D)==1 && rr::g_street_guard==race);
        assert(rr::g_street_data==R32(career+0x0C) && rr::g_street_city==G<int8_t>(setup) &&
               rr::g_street_machine==R32(0x557EEC) && rr::g_street_vehicle==R32(0x10A000+0x14));
        rr::StreetGuard(false,false);assert(G<uint8_t>(race+0x0D)==0 && rr::g_street_guard==0);
    }
    // The game may change the borrowed byte itself; only our exact value (1) is restorable.
    st::Fixture();rr::StreetGuard(false,true);G<uint8_t>(race+0x0D)=2;st::AssertAbandonedWithoutWrite();

    // A host start received in the garage/front end remains pending; no marker, teleport or setup is touched.
    for(uint32_t mode:{5u,3u}) {
        st::Fixture();W32(0x53F2D4,mode);start=st::Start(400+mode);rr::OnStart(start);before=ram;
        rr::Tick(false,true);
        assert(ram==before && rr::g_phase==0 && rr::g_start_in.serial==start.serial &&
               rr::g_done_serial!=start.serial && rr::g_street_guard==0);
        assert(rr::Notice().find("O host iniciou uma corrida")!=std::string::npos);
        // Returning to the street clears the notice; a consumed authorization cannot disable the next hold.
        W32(0x53F2D4,2);rr::g_done_serial=start.serial;rr::Tick(false,true);
        assert(rr::Notice().empty() && G<uint8_t>(race+0x0D)==1);
        rr::Tick(false,false);assert(rr::Notice().empty() && G<uint8_t>(race+0x0D)==0);
    }
    st::Fixture();W32(0x53F2D4,5);rr::Tick(false,true);
    assert(rr::Notice().empty()); // no false warning merely for visiting one's own garage

    // Leave the common fixture/global state clean for the existing memory suites.
    st::Fixture();W32(0x53F2D4,0);W32(race,0x46B2D8);W32(setup+0x18,0);
    std::puts("PASS: host-only street hold lifecycle; role/offline/native/menu/result/context guards; pending/consumed authorization; garage warning without writes.");
}

namespace host_marker_test {
namespace rr=mod::race;
constexpr uint32_t record=0x184000,vehicle=0x185000,car=0x186000,inst=0x187000;
constexpr uint32_t collider=0x188000,body=0x189000,model=0x18A000;
constexpr uint16_t record_id=77;
using Record=std::array<uint8_t,0x4C>;
Record ReadRecord() {Record r;std::memcpy(r.data(),mod::HostPtr(record),r.size());return r;}
Record Arm(uint32_t serial=501) {
    host_street_test::Fixture();
    auto start=host_street_test::Start(serial);rr::OnStart(start);
    W32(data+0xA5C,record);W32(data+0xA60,record+0x4C);W32(data+0xA64,record+0x4C); // no append room
    std::memset(mod::HostPtr(record),0x22,0x4C);W32(record,0x473E74);
    G<uint16_t>(record+4)=record_id;G<int16_t>(record+6)=7;G<int8_t>(record+8)=0;
    G<uint8_t>(record+9)=3;G<uint8_t>(record+10)=2;
    Record original=ReadRecord();
    std::memcpy(rr::g_saved_fields,original.data(),original.size());
    std::memset(mod::HostPtr(record+0x0B),0xA5,0x4C-0x0B);
    G<uint8_t>(record+9)=G<uint8_t>(record+10)=0xFF;
    rr::g_saved_hookman=3;rr::g_saved_side=2;
    rr::g_fields_lent=true;rr::g_fields_id=record_id;rr::g_fields_data=data;
    rr::g_record_id=record_id;rr::g_phase=1;rr::g_phase_at=test_now-500;
    rr::g_phase_serial=serial;rr::g_phase_cruise=race;rr::g_phase_setup=setup;rr::g_phase_data=data;
    rr::g_phase_city=0;rr::g_phase_index=7;
    for(int i=0;i<3;i++)rr::g_target[i]=9999.f+float(i);
    // A valid physical chain makes unintended stale teleports observable, rather than a no-op car::Place.
    W32(0x10A000+0x14,vehicle);W32(vehicle+8,car);W32(vehicle+0x0C,model);W32(vehicle+0x1C,inst);
    W32(car+0x68,collider);W32(collider+0x10,body);
    G<float>(inst+0x34)=41.f;G<float>(inst+0x38)=42.f;G<float>(inst+0x3C)=43.f;
    G<float>(body+0x80)=41.f;G<float>(body+0x84)=42.57f;G<float>(body+0x88)=43.f;
    G<uint8_t>(model+0x132)=0;
    return original;
}
void AssertPoseUnchanged() {
    assert(G<float>(inst+0x34)==41.f && G<float>(inst+0x38)==42.f && G<float>(inst+0x3C)==43.f);
    assert(G<float>(body+0x80)==41.f && G<float>(body+0x84)==42.57f && G<float>(body+0x88)==43.f);
}
}

void HostMarkerPhaseTests() {
    namespace hm=host_marker_test;
    namespace rr=mod::race;
    // Reception changes mod state only. The next guest tick cancels the old phase before it can teleport.
    auto original=hm::Arm();auto newer=host_street_test::Start(502);newer.race_idx=8;
    auto before=ram;rr::OnStart(newer);
    assert(ram==before && rr::g_phase==1 && rr::g_phase_serial==501 && rr::g_start_in.serial==502);
    rr::Tick(false,true);
    assert(rr::g_phase==0 && rr::g_phase_serial==0 && !rr::g_fields_lent && !rr::g_start_done);
    assert(hm::ReadRecord()==original);hm::AssertPoseUnchanged();
    // The synthetic list is full and the database absent: the new request fails before a new marker arm.
    assert(rr::g_done_serial==502 && rr::g_race_serial==502 && G<uint8_t>(hm::model+0x132)==0);

    // Context invalidation must cancel before a stale target is applied, even when the elapsed phase is >300ms.
    for(int part=0;part<5;part++) {
        original=hm::Arm();rr::g_done_serial=501; // prevent a new arm after cancellation in this fixture
        if(part==0)W32(career+0x0C,0x18B000);
        if(part==1)G<int8_t>(setup)=1;
        if(part==2)W32(0x53F2E0,next_setup);
        if(part==3)W32(0x53F2D4,5);
        if(part==4)W32(0x53F2D4,3);
        auto borrowed=hm::ReadRecord();rr::Tick(false,true);
        assert(rr::g_phase==0 && !rr::g_fields_lent && !rr::g_start_done);
        assert(hm::ReadRecord()==(part==0?borrowed:original)); // never restore into a different save's list
        hm::AssertPoseUnchanged();assert(G<uint8_t>(hm::model+0x132)==0);
    }
    // Matching UniqueId alone does not authorize writes when the record now denotes another city/race.
    for(int part=0;part<2;part++) {
        hm::Arm();rr::g_done_serial=501;
        if(part==0)G<int8_t>(hm::record+8)=1;else G<int16_t>(hm::record+6)=8;
        rr::g_cancel_marker=true;auto borrowed=hm::ReadRecord();rr::Tick(false,true);
        assert(hm::ReadRecord()==borrowed && rr::g_phase==0 && !rr::g_fields_lent);
        hm::AssertPoseUnchanged();
    }

    // A timeout consumes the attempt but does not advertise a loaded race or a successful native start.
    original=hm::Arm();rr::g_phase_at=test_now-10001;rr::Tick(false,true);
    assert(rr::g_phase==0 && !rr::g_start_done && rr::g_done_serial==501 && !rr::g_bound_race);
    assert(hm::ReadRecord()==original && !rr::MatchesSharedRace());hm::AssertPoseUnchanged();

    // A genuine native StartRace request is recognized even after the cruise enters result state 7.
    original=hm::Arm();W32(career+0x2C,4);G<uint16_t>(career+0x34)=hm::record_id;
    G<uint8_t>(race+0x0D)=1;W32(machine+4,7);
    assert(rr::MarkerStarted() && !rr::MarkerContext());rr::Tick(false,true);
    assert(rr::g_phase==0 && rr::g_start_done && rr::g_done_serial==501 && rr::g_race_serial==501);
    assert(G<uint8_t>(hm::record+9)==3 && G<uint8_t>(hm::record+10)==2 && rr::g_fields_lent);
    assert(G<uint8_t>(hm::record+0x0B)==0xA5); // opponent fields stay lent until native allocation has finished
    hm::AssertPoseUnchanged();
    // A different record's request must never mark this authorization successfully started.
    original=hm::Arm();W32(career+0x2C,4);G<uint16_t>(career+0x34)=hm::record_id+1;W32(machine+4,7);
    assert(!rr::MarkerStarted());rr::Tick(false,true);
    assert(!rr::g_start_done && rr::g_phase==0 && hm::ReadRecord()==original);hm::AssertPoseUnchanged();

    // A new network session can restart at serial 1. Cancellation is not lost when that numeric serial matches.
    original=hm::Arm(1);before=ram;rr::ResetNetwork();
    assert(ram==before && rr::g_cancel_marker && rr::g_phase==1 && !rr::g_have_start && !rr::g_start_done);
    assert(rr::g_serial==0 && rr::g_done_serial==0 && rr::g_race_serial==0);
    auto restarted=host_street_test::Start(1);restarted.race_idx=8;rr::OnStart(restarted);
    assert(ram==before && rr::g_start_in.serial==1 && rr::g_phase_serial==1 && rr::g_cancel_marker);
    rr::Tick(false,true);
    assert(rr::g_phase==0 && !rr::g_cancel_marker && !rr::g_start_done && hm::ReadRecord()==original);
    hm::AssertPoseUnchanged();
    host_street_test::Fixture();W32(0x53F2D4,0);W32(race,0x46B2D8);W32(setup+0x18,0);
    std::puts("PASS: marker phase serial/context ownership; superseding starts and network reset defer guest writes; cancellation/timeout preserve records and prevent stale teleport; real request4 alone confirms start.");
}
