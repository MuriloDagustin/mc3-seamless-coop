// Exercises production traffic code against RAM only. No game/runner/network/firewall.
namespace traffic_test {
using namespace mod;
constexpr uint32_t root=0x160000, traffic_tm=0x161000, slots=0x162000, types=0x166000, type=0x167000;
constexpr uint32_t trailer=0x168000, bound=0x169000, model=0x16A000, matrices=0x16B000;
constexpr uint32_t collider=0x170000, trailer_col=0x171000, shape=0x172000;
constexpr uint32_t road=0x173000, next=0x173100;
using Packets=std::vector<std::vector<uint8_t>>;
void Matrix(uint32_t p,float x=100,float y=2,float z=30) {
    float m[12]={1,0,0,0,1,0,0,0,1,x,y,z};std::memcpy(HostPtr(p),m,sizeof m);
}
void Body(uint32_t c,uint32_t inst,uint16_t handle,bool alias=false) {
    W32(c+4,inst);W32(c+0x78,slots);W32(c+0x10,c+0x80);W32(c+0x14,alias?c+0xDC:inst+0x10);
    uint32_t bound_shape=(inst==slots+0x24&&R32(slots+0xEC))?model:shape+(c==trailer_col?0x100:0);
    uint32_t container=bound+(c==trailer_col?0x100:0);
    W32(inst+4,container);W32(container+0x0C,bound_shape);
    W32(c+0x0C,bound_shape);G<uint8_t>(bound_shape+7)=alias?0:1;
    G<float>(bound_shape+0x30)=1;G<uint16_t>(c+8)=handle;
    uint32_t b=c+0x80;G<float>(b+4)=1000;G<float>(b+8)=.001f;
    for(int i=0;i<3;i++)G<float>(b+0x0C+i*4)=100+100.f*i;
    Matrix(b+0x5C);G<float>(b+0x84)=alias?2.f:3.f;
    G<float>(b+0x44)=2;G<float>(b+0x4C)=-5;G<float>(b+0x54)=.4f;
    G<float>(b+0x8C)=71;G<float>(b+0xA4)=72; // native force/impulse preserved
}
void TFixture(bool truck=false,bool wreck=false,bool trailer_body=false,int n=10) {
    ::Fixture();traffic::Stop();traffic::Tick(false,test_now);test_now+=1000;
    _putenv_s("MC3COOP_TRAFFIC_PHYSICS","1");
    W32(0x50152C,root);W32(root+8,traffic_tm);W32(traffic_tm,0x44CE90);G<int16_t>(traffic_tm+0x12C)=int16_t(n);
    W32(traffic_tm+0x130,slots);W32(traffic_tm+0x13C,uint32_t(n/30+1));W32(traffic_tm+0x30,types);
    G<uint16_t>(traffic_tm+0x34)=1;W32(types,type);G<float>(type+0x298)=3;
    std::memcpy(HostPtr(setup+0x68),"circuit/test",13);W32(machine+4,6);
    W32(0x10A000+0x14,0x180000);W32(0x180008,0x181000);W32(0x18001C,0x182000);
    W32(0x181068,0x183000);W32(0x183010,0x184000);
    for(int i=0;i<n;i++) {
        uint32_t s=slots+uint32_t(i)*0x21C;W32(s,0x44D1CC);W32(s+0x138,type);
        G<uint16_t>(s+0x2C)=uint16_t(20+i);W32(s+0xFC,road);W32(s+0x100,next);
        Matrix(s+0x34,100.f+i*10);Matrix(s+0x74,100.f+i*10);
        G<float>(s+0xE0)=10;G<uint8_t>(s+0x16C)=3;
    }
    G<uint8_t>(shape+7)=1;G<float>(shape+0x30)=1;
    if(truck) {
        W32(slots+0xEC,trailer);W32(slots+0x28,bound);W32(bound+0x0C,model);
        G<uint8_t>(model+4)=0x10;G<uint16_t>(model+0x4E)=2;W32(model+0x5C,matrices);
        Matrix(trailer+0x13C);G<uint16_t>(trailer+0x134)=60;
        // A 90-degree articulated trailer makes a convention error observable.
        float t[12]={0,0,-1,0,1,0,1,0,0,100,2,30};std::memcpy(HostPtr(trailer+0x13C),t,sizeof t);
    }
    if(wreck) {W32(slots+0x13C,collider);Body(collider,slots+0x24,20,true);}
    if(trailer_body) {
        assert(truck&&wreck);W32(trailer+0xA4,trailer_col);Body(trailer_col,trailer+0x12C,60);
        W32(trailer+4,collider+0x80);W32(trailer+8,trailer_col+0x80);
        G<float>(trailer_col+0x80+0x44)=-3;G<float>(trailer_col+0x80+0x4C)=1;
        G<float>(trailer_col+0x80+0x54)=.8f;
    }
}
Packets Snapshot() {Packets p;test_now+=60;traffic::Capture(test_now);assert(traffic::Snapshot(p,test_now));return p;}
void Receive(const Packets& p) {for(const auto& one:p)traffic::Receive(one.data(),int(one.size()));}
traffic::Car ReadRecord(const std::vector<uint8_t>& p,int i=0) {
    traffic::Car c;std::memcpy(&c,p.data()+sizeof(traffic::Header)+i*sizeof(c),sizeof c);return c;
}
void Near(float actual,float expected) {assert(std::fabs(actual-expected)<.002f);}
void Apply(const Packets& p) {Receive(p);traffic::Tick(true,test_now);}
}
void TrafficMemoryTests() {
    using namespace traffic_test;
    TFixture(true,true,true);auto packets=Snapshot();assert(packets.size()==2);
    for(auto& p:packets)assert(p.size()+20<=1400);
    auto first=ReadRecord(packets[0]);assert(first.flags==(traffic::kActive|traffic::kTrailer|traffic::kWreck|traffic::kTrailerBody));
    assert(first.velocity[0]==2&&first.trailer_velocity[0]==-3);
    // Incomplete and reversed chunks may not publish or touch RAM.
    auto original=ram;traffic::Receive(packets[1].data(),int(packets[1].size()));traffic::Tick(true,test_now);
    assert(ram==original&&traffic::g_remote.empty());
    traffic::Receive(packets[0].data(),int(packets[0].size()));
    G<float>(slots+0x58)=900;G<float>(collider+0x80+0x44)=99;
    traffic::Tick(true,test_now);
    Near(RF(slots+0x58),100);assert(R32(slots+0xFC)==road&&R32(slots+0x100)==next);
    assert(R32(slots+0xC4)==1&&G<uint8_t>(slots+0xAC)==0);
    Near(RF(collider+0x80+0x44),2);Near(RF(collider+0x80+0x2C),2000);
    Near(RF(collider+0x80+0x54),.4f);Near(RF(collider+0x80+0x3C),80);
    Near(RF(collider+0x80+0x84),2); // no-offset native bound aliases COM and visual origin
    Near(RF(collider+0x80+0x8C),71);Near(RF(collider+0x80+0xA4),72);
    Near(RF(trailer+0x13C+8),-1);Near(RF(matrices+48+8),-1);
    Near(RF(trailer_col+0x80+0x44),-3);Near(RF(trailer_col+0x80+0x2C),-3000);
    assert(traffic::g_trailers==1&&traffic::g_bodies==1);
    // Duplicate and older chunks cannot renew freshness or revert authority.
    auto ready=traffic::g_ready;auto at=traffic::g_ready_at;test_now+=80;Receive(packets);
    assert(traffic::g_ready.cycle==ready.cycle&&traffic::g_ready_at==at);
    // No writes for bad payloads, dimensions, NaNs, slot order, or protocol.
    for(int bad=0;bad<6;bad++) {
        auto corrupt=packets[0];traffic::Header h;std::memcpy(&h,corrupt.data(),sizeof h);h.cycle+=100;
        if(bad==0)h.version=1;if(bad==1)h.chunk=255;if(bad==2)h.count=1025;
        std::memcpy(corrupt.data(),&h,sizeof h);
        if(bad==3) {float nan=NAN;std::memcpy(corrupt.data()+sizeof h+offsetof(traffic::Car,trailer),&nan,4);}
        if(bad==4)corrupt[sizeof h]=200;
        if(bad==5)corrupt.resize(corrupt.size()-1);
        auto before=ram;traffic::Receive(corrupt.data(),int(corrupt.size()));
        assert(ram==before&&traffic::g_ready.cycle==ready.cycle);
    }
    // Local impact state is overwritten by host body state, including a normal host with a local wreck.
    TFixture(false,false);packets=Snapshot();W32(slots+0x13C,collider);Body(collider,slots+0x24,20);
    Apply(packets);Near(RF(collider+0x80+0x4C),-10);Near(RF(collider+0x80+0x84),3);
    // Host wreck without a local collider uses a kinematic proxy; never forges/allocates a body.
    TFixture(false,true);G<uint8_t>(slots+0x14B)=200;G<uint8_t>(slots+0x14E)=1;
    packets=Snapshot();W32(slots+0x13C,0);Apply(packets);
    assert(R32(slots+0x13C)==0&&traffic::g_proxies==1);
    assert(G<uint8_t>(slots+0x14B)==0x7E&&G<uint8_t>(slots+0x14E)==0);
    assert(R32(slots+0xC4)==1&&R32(slots+0xFC)==road);
    // Null links, foreign body, missing trailer bounds, and native scripted cars are untouched.
    for(int bad=0;bad<7;bad++) {
        TFixture(bad==2||bad==5,bad==5,bad==5);packets=Snapshot();
        if(bad==0)W32(slots+0x100,0);
        if(bad==1) {W32(slots+0x13C,collider);Body(collider,slots+0x24,20);W32(collider+0x78,slots+0x21C);}
        if(bad==2)G<uint16_t>(model+0x4E)=129;
        if(bad==3) {W32(slots+0xC4,1);W32(slots+0xA8,0x185000);}
        if(bad==4)W32(slots+0xEC,trailer); // host/local kind mismatch
        if(bad==5)W32(trailer+4,collider+0x90); // foreign/detached native joint
        if(bad==6) {W32(slots+0x13C,collider);Body(collider,slots+0x24,20);W32(collider+0x14,collider+0xDC);}
        auto before=ram;Apply(packets);
        assert(std::memcmp(ram.data()+slots,before.data()+slots,0x21C)==0&&traffic::g_blocked>=1);
    }
    // Complete inactivity hides and restores both bodies, trailer and submesh; stale packets release C4.
    TFixture(true,true,true);packets=Snapshot();Apply(packets);
    auto host=original; // use an isolated host-memory view to build its next snapshot
    auto partner=ram;ram=host;mod::g_base=ram.data();
    for(int i=0;i<10;i++)G<uint16_t>(slots+uint32_t(i)*0x21C+0x2C)=0xFFFF;
    auto inactive=Snapshot();ram=partner;mod::g_base=ram.data();Apply(inactive);
    Near(RF(slots+0x5C),-500);Near(RF(trailer+0x164),-500);
    Near(RF(collider+0x80+0x84),-500);Near(RF(trailer_col+0x80+0x84),-499);
    test_now+=401;traffic::Tick(true,test_now);
    assert(R32(slots+0xC4)==0);Near(RF(slots+0x5C),2);Near(RF(trailer+0x164),2);
    // Reallocated handles/worlds must never receive saved fields from a former instance.
    TFixture(true,true);packets=Snapshot();Apply(packets);G<uint16_t>(slots+0x2C)=99;
    W32(slots+0xC4,7);traffic::Stop();traffic::Tick(false,test_now);assert(R32(slots+0xC4)==7);
    TFixture();packets=Snapshot();Apply(packets);W32(holder+4,::race+0x3000);
    auto before=ram;traffic::Stop();traffic::Tick(false,test_now);assert(ram==before);
    // World names, malformed native matrices and zero inertia are rejected.
    TFixture(true,true);packets=Snapshot();
    float nan=NAN;std::memcpy(mod::HostPtr(slots+0x34),&nan,4);test_now+=60;
    Packets none;traffic::Capture(test_now);assert(!traffic::Snapshot(none,test_now));
    TFixture();packets=Snapshot();Apply(packets);auto newer=Snapshot();Apply(newer);
    auto latest=traffic::g_ready;Receive(packets);assert(traffic::g_ready.cycle==latest.cycle);
    TFixture();packets=Snapshot();G<uint8_t>(setup+0x68)='X';before=ram;Apply(packets);assert(ram==before);
    TFixture(false,true);packets=Snapshot();G<float>(collider+0x80+0x0C)=0;before=ram;Apply(packets);
    assert(std::memcmp(ram.data()+slots,before.data()+slots,0x21C)==0);
    // Session publication uses only a captured frame, even after native memory has changed.
    TFixture();test_now+=60;traffic::Capture(test_now);G<float>(slots+0x58)=800;
    Packets captured;assert(traffic::Snapshot(captured,test_now));Near(ReadRecord(captured[0]).pos[0],100);
    assert(!traffic::Snapshot(captured,test_now));
    // The session thread passes steady_clock while Capture stamps GetTickCount64. Snapshot must use Capture's clock,
    // not the caller's, or every traffic publication is rejected (the real regression that left traffic off).
    TFixture();test_now+=60;traffic::Capture(test_now);
    Packets crossed;assert(traffic::Snapshot(crossed,1));assert(!traffic::Snapshot(crossed,1));
    // A transient invalid pool must not lose ownership and leave a scripted puppet stuck after disconnect.
    TFixture();packets=Snapshot();Apply(packets);G<uint16_t>(traffic_tm+0x34)=0;
    traffic::Stop();before=ram;traffic::Tick(false,test_now);assert(ram==before);
    G<uint16_t>(traffic_tm+0x34)=1;traffic::Tick(false,test_now);assert(R32(slots+0xC4)==0);
    TFixture();packets=Snapshot();Apply(packets);W32(machine+4,7);traffic::Tick(true,test_now);
    assert(R32(slots+0xC4)==0&&R32(slots+0xFC)==road);
    // An older world generation cannot win by supplying a numerically newer cycle.
    TFixture();packets=Snapshot();Apply(packets);auto current=traffic::g_ready;
    for(auto& p:packets) {
        traffic::Header h;std::memcpy(&h,p.data(),sizeof h);assert(h.epoch>1);--h.epoch;h.cycle+=100;
        std::memcpy(p.data(),&h,sizeof h);
    }
    Receive(packets);assert(traffic::g_ready.epoch==current.epoch&&traffic::g_ready.cycle==current.cycle);
    traffic::Stop();traffic::Tick(false,test_now);_putenv_s("MC3COOP_TRAFFIC_PHYSICS","0");
    puts("PASS: traffic atomic chunks/MTU/validation; articulated bounds and both bodies; COM aliases/momentum; native guards; proxy/no body allocation; inactive/stale/reset/handle safety.");
}
