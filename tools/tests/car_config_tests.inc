// Actual car.cpp against RAM. These tests prove installation/order/lifecycle, not native resource rebuilding.
namespace cfg_test {
using mod::W32;using mod::R32;using mod::G;
constexpr uint32_t cfg_current=0x194000,cfg_next=0x195000,cfg_live=0x196000,cfg_local=0x197000;
constexpr uint32_t vehicle=0x190000,player=0x10B000;
using Config=std::array<uint8_t,mod::car::kCarCfgBytes>;
Config ConfigFor(const char* model,uint8_t paint=3) {
    Config c{};std::memcpy(c.data()+0xE3,model,std::strlen(model)+1);c[0x36]=paint;
    uint32_t foreign_vt=0xDEADBEEF;std::memcpy(c.data(),&foreign_vt,4);return c;
}
void Put(uint32_t p,const Config& c,uint32_t vt) {
    std::memcpy(mod::HostPtr(p),c.data(),c.size());W32(p,vt);
}
Config ReadConfig(uint32_t p) {Config c;std::memcpy(c.data(),mod::HostPtr(p),c.size());return c;}
void CfgFixture() {
    ::Fixture();auto initial=ConfigFor("vp_d_scion_tc_05");
    for(auto p:{cfg_current,cfg_next,cfg_live,cfg_local})Put(p,initial,0x440000+p);
    W32(setup+0x14C,cfg_current);W32(next_setup+0x14C,cfg_next);
    for(auto s:{setup,next_setup}){W32(s+0x150,0xFFFFFFFF);W32(s+0x158,0xFFFFFFFF);}
    W32(setup+0x120,cfg_local);W32(next_setup+0x120,cfg_local);
    W32(player+0x14,vehicle);W32(vehicle+0x64,cfg_live);
    W32(0x10A000+0x14,vehicle+0x1000);W32(vehicle+0x1000+0x64,cfg_local);
    // No body is needed: config processing precedes the pose path and must not allocate one.
}
void Tick() {mod::car::Tick(test_now);}
}
void CarConfigTests() {
    using namespace cfg_test;
    namespace car=mod::car;
    CfgFixture();auto updated=ConfigFor("vp_d_scion_tc_05",12);
    auto local_before=ReadConfig(cfg_local);uint32_t vt=R32(cfg_live);
    car::SetPartnerCar(1,updated.data());Tick();
    for(uint32_t p:{cfg_current,cfg_next,cfg_live}) {
        assert(G<uint8_t>(p+0x36)==12&&R32(p+8)==0x3F7F);
        assert(R32(p)==0x440000+p);
    }
    assert(ReadConfig(cfg_local)==local_before&&R32(cfg_live)==vt);
    uint64_t applications=car::g_remote[1].applications,revision=car::g_remote[1].revision;
    for(auto p:{cfg_current,cfg_next,cfg_live})W32(p+8,0); // native consumer clears its mask
    auto before=ram;
    for(int i=0;i<10;i++){car::SetPartnerCar(1,updated.data());Tick();}
    assert(ram==before&&car::g_remote[1].applications==applications);
    // Sender's native dirty flags/vtable are transient: neither is a content update.
    uint32_t transient=0xFFFFFFFF;std::memcpy(updated.data()+8,&transient,4);updated[0]^=1;
    car::SetPartnerCar(1,updated.data());Tick();
    assert(ram==before&&car::g_remote[1].revision==revision);
    // A real second paint change is installed once, retaining unrelated LOCAL dirty bits.
    W32(cfg_live+8,0x4000);updated[0x36]=14;car::SetPartnerCar(1,updated.data());Tick();
    assert(R32(cfg_live+8)==0x7F7F&&G<uint8_t>(cfg_live+0x36)==14);
    assert(car::g_remote[1].revision==revision+1);
    // Distinct TypeName never contaminates the old vehicle/current comparison base. NEXT is staged.
    auto different=ConfigFor("vp_d_new_car",7);auto live_before=ReadConfig(cfg_live),current_before=ReadConfig(cfg_current);
    car::SetPartnerCar(1,different.data());Tick();
    assert(ReadConfig(cfg_live)==live_before&&ReadConfig(cfg_current)==current_before);
    assert(std::strcmp(static_cast<const char*>(mod::HostPtr(cfg_next+0xE3)),"vp_d_new_car")==0);
    assert(R32(cfg_next+8)==0x3FFF&&car::g_remote[1].model_pending);
    assert(car::Describe().find("car_model_pending=1")!=std::string::npos);
    W32(cfg_next+8,0);before=ram;Tick();assert(ram==before);
    // The native reload identity also contains E2/F9; these are not ordinary live customizations.
    for(int resource=0;resource<2;resource++) {
        CfgFixture();auto changed=updated;
        if(resource==0)changed[0xE2]=1;else std::memcpy(changed.data()+0xF9,"new_resource",13);
        live_before=ReadConfig(cfg_live);current_before=ReadConfig(cfg_current);
        car::SetPartnerCar(1,changed.data());Tick();
        assert(car::g_remote[1].model_pending&&ReadConfig(cfg_live)==live_before&&ReadConfig(cfg_current)==current_before);
    }
    // A configuration alias (including an overlapping object) may not mutate the old car or local player.
    for(int alias=0;alias<4;alias++) {
        CfgFixture();
        W32(next_setup+0x14C,alias==0?cfg_live:alias==1?cfg_current:alias==2?cfg_local:cfg_local+4);
        auto live=ReadConfig(cfg_live),current=ReadConfig(cfg_current),local=ReadConfig(cfg_local);
        car::SetPartnerCar(1,different.data());Tick();
        assert(ReadConfig(cfg_live)==live&&ReadConfig(cfg_current)==current&&ReadConfig(cfg_local)==local);
    }
    CfgFixture();W32(next_setup+0x178,cfg_next); // slot2 has a setup but no live vehicle
    auto reserved_before=ReadConfig(cfg_next);car::SetPartnerCar(1,updated.data());Tick();
    assert(ReadConfig(cfg_next)==reserved_before);
    // Same-model update through an alias is applied once and does not reintroduce consumed bits.
    CfgFixture();W32(setup+0x14C,cfg_live);W32(next_setup+0x14C,cfg_live);
    car::SetPartnerCar(1,updated.data());Tick();assert(car::g_remote[1].applications==1);
    W32(cfg_live+8,0);before=ram;Tick();assert(ram==before);
    // New CFG object / vehicle generation receives the latest same-model configuration.
    CfgFixture();car::SetPartnerCar(1,updated.data());Tick();
    auto old_cfg=ReadConfig(cfg_live);Put(cfg_live+0x3000,ConfigFor("vp_d_scion_tc_05"),0x445500);
    W32(player+0x14,vehicle+0x2000);W32(vehicle+0x2000+0x64,cfg_live+0x3000);Tick();
    assert(G<uint8_t>(cfg_live+0x3000+0x36)==14&&ReadConfig(cfg_live)==old_cfg);
    // A genuine native new vehicle of the new model clears pending; no synthetic pointer transplant.
    CfgFixture();car::SetPartnerCar(1,different.data());Tick();
    Put(cfg_live+0x3000,ConfigFor("vp_d_new_car"),0x445500);
    W32(player+0x14,vehicle+0x2000);W32(vehicle+0x2000+0x64,cfg_live+0x3000);Tick();
    assert(!car::g_remote[1].model_pending&&G<uint8_t>(cfg_live+0x3000+0x36)==7);
    // Before native construction, current and NEXT can receive a full configuration for the next load.
    CfgFixture();W32(player+0x14,0);car::SetPartnerCar(1,different.data());Tick();
    assert(std::strcmp(static_cast<const char*>(mod::HostPtr(cfg_current+0xE3)),"vp_d_new_car")==0);
    assert(R32(cfg_current+8)==0x3FFF);
    // The rendering resource's immutable construction name takes precedence over stale config metadata.
    CfgFixture();constexpr uint32_t custom=0x1A0000;
    W32(vehicle+0x68,custom);W32(custom,0x454ADC);
    std::memcpy(mod::HostPtr(custom+0x102C),"vp_d_new_car",13);
    live_before=ReadConfig(cfg_live);current_before=ReadConfig(cfg_current);
    car::SetPartnerCar(1,updated.data());Tick();
    assert(car::g_remote[1].model_pending&&ReadConfig(cfg_live)==live_before&&ReadConfig(cfg_current)==current_before);
    car::SetPartnerCar(1,different.data());Tick();
    assert(!car::g_remote[1].model_pending&&G<uint8_t>(cfg_live+0x36)==7);
    // A config observed during construction may become valid later at the same address.
    CfgFixture();G<uint8_t>(cfg_live+0xE3)=0;car::SetPartnerCar(1,updated.data());Tick();
    assert(car::g_remote[1].model_pending);
    Put(cfg_live,ConfigFor("vp_d_scion_tc_05"),0x440000+cfg_live);Tick();
    assert(!car::g_remote[1].model_pending&&G<uint8_t>(cfg_live+0x36)==14);
    // Invalid/truncated TypeNames do not replace the last valid payload or touch RAM.
    CfgFixture();car::SetPartnerCar(1,updated.data());Tick();before=ram;
    for(int kind=0;kind<4;kind++) {
        auto bad=updated;
        if(kind==0)bad[0xE3]=0;if(kind==1)std::memset(bad.data()+0xE3,'x',0x16);if(kind==2)bad[0xE3]='/';
        if(kind==3)std::memset(bad.data()+0xF9,'x',13);
        revision=car::g_remote[1].revision;car::SetPartnerCar(1,bad.data());Tick();
        assert(car::g_remote[1].revision==revision&&ram==before);
    }
    car::SetPartnerCar(1,nullptr);assert(ram==before);
    // A native multiplayer context must not receive live config writes.
    CfgFixture();G<uint8_t>(setup+0x108)=G<uint8_t>(next_setup+0x108)=1;before=ram;
    car::SetPartnerCar(1,updated.data());Tick();assert(ram==before);
    // Missing/bad mapped target never becomes a write to an arbitrary/unmapped object.
    CfgFixture();W32(setup+0x14C,0xFFFFFF00);W32(next_setup+0x14C,0xFFFFFF00);W32(vehicle+0x64,0xFFFFFF00);
    before=ram;car::SetPartnerCar(1,updated.data());Tick();assert(ram==before);
    // Configuration ordering is independent from pose sequence, with wrap and reconnect semantics.
    CfgFixture();car::OnPartnerCar(1,updated.data(),0xFFFFFFFE);Tick();revision=car::g_remote[1].revision;
    car::OnPartnerCar(1,different.data(),0xFFFFFFFD);assert(car::g_remote[1].revision==revision);
    car::OnPartnerCar(1,different.data(),0);assert(car::g_remote[1].revision==revision+1);
    car::Remove(1);car::OnPartnerCar(1,updated.data(),1);Tick();assert(car::g_remote[1].cfg_sequence==1);
    puts("PASS: same-model dirty installation once; vtable/local-mask preservation; current/live model guard; NEXT staging/aliases; native new vehicle; packet order/wrap/reconnect; invalid/native-net guards.");
}
