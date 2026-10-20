// Exercise the actual result controller and verified-guest input contract, without any Windows input/game.
namespace race_flow_test {
namespace rr=mod::race;
namespace ui=race_ui_test;
constexpr uint32_t db=0x250000,names=0x251000,desc=0x252000,name7=0x253000,name8=0x254000;
constexpr uint32_t records=0x255000,taskItems=0x256000;
constexpr char oldName[]="circuit\\easy\\old_event_with_full_name";
constexpr char newName[]="circuit\\easy\\next_event_with_full_name";

rr::Start Start(uint32_t serial,int index=7,uint16_t id=41) {
    rr::Start s{};s.serial=serial;s.city=0;s.race_idx=int16_t(index);
    uint32_t vt=0x473E74;std::memcpy(s.record,&vt,4);std::memcpy(s.record+4,&id,2);
    std::memcpy(s.record+6,&s.race_idx,2);s.record[8]=0;
    std::snprintf(s.name,sizeof s.name,"event %d",index);return s;
}

void Fixture() {
    ui::Fixture();test_now=10000;rr::ResetNetwork();rr::g_result_reset=false;
    W32(0x557EE4,db);W32(db+0x14,9);W32(db+0x18,names);W32(db+0x20,desc);
    W32(names+7*4,name7);W32(names+8*4,name8);
    std::memcpy(mod::HostPtr(name7),oldName,sizeof oldName);std::memcpy(mod::HostPtr(name8),newName,sizeof newName);
    W32(desc+7*0x30,7);W32(desc+8*0x30,7);
    for(uint32_t s:{setup,next_setup}) {
        std::memcpy(mod::HostPtr(s+0x68),oldName,sizeof oldName);W32(s+0x18,7);
    }
    rr::g_start_in=Start(401);rr::g_have_start=true;rr::ActivateStart(rr::g_start_in,true);
    rr::g_start_done=true;rr::g_done_serial=401;rr::g_bound_race=race;
    rr::g_bound_ran=rr::g_bound_ended=true;W32(machine+4,12);
}

void Tick() { rr::ResultFlowTick(false,true,test_now); }
uint16_t Poll(uint16_t raw=0xF3FF) {
    auto before=ram;uint16_t b=raw;assert(rr::ControlInput(b));assert(ram==before);return b;
}
uint16_t Pulse() {
    uint16_t b=Poll();if(!b)b=Poll();return b;
}
rr::After Street(uint32_t revision=1) {
    rr::After a{};a.serial=401;a.revision=revision;a.action=rr::AfterAction::Street;a.city=0;return a;
}
void Confirm() {
    W32(ui::front+0xD8,6);W32(ui::front+0xFC,0x1A);
    W32(ui::menus+0xC0,ui::confirmPage);W32(ui::confirmPage,0x45DE48);W32(ui::confirmPage+0x44,1);
    W32(ui::menus+0x118,ui::dialog);W32(ui::dialog,0x45E0D0);W32(ui::dialog+0x44,1);
    W32(ui::dialog+0x60,ui::dialogData);W32(ui::dialogData+8,ui::yesNo);
    ui::List(ui::yesNo,ui::yesNoEntries,0x45DFA8,4,2);
    W32(ui::yesNoEntries,4);W32(ui::yesNoEntries+16,8);ui::Select(ui::yesNo,ui::yesNoEntries,4,0,1);
}
rr::After Next() {
    rr::After a=Street();a.action=rr::AfterAction::Next;a.next=Start(402,8,901);
    W32(data+0xA5C,records);W32(data+0xA60,records+0x98);W32(data+0xA64,records+0x98);
    auto old=Start(401);auto localNext=Start(402,8,82);
    std::memcpy(mod::HostPtr(records),old.record,0x4C);std::memcpy(mod::HostPtr(records+0x4C),localNext.record,0x4C);
    W32(data+0x10,ui::tasks);W32(ui::tasks+0x78,ui::taskEntries);W32(ui::tasks+0x7C,ui::taskEntries+12);
    W32(ui::tasks+0x94,0);W32(ui::taskEntries+8,ui::task);W32(ui::task,0x470348);
    G<uint8_t>(ui::task+0x0C)=G<uint8_t>(ui::task+0xE0)=1;
    W32(ui::task+0x10,1);W32(ui::task+0x14,taskItems);W32(ui::task+0xDC,0);
    G<uint16_t>(taskItems+6)=82;assert(rr::NativeNextMatches(a.next));return a;
}
}

void RaceFlowTests() {
    namespace ft=race_flow_test;namespace rr=mod::race;namespace ui=race_ui_test;
    auto saved_now=test_now;
    // Authority comes from the consumed Start: a missing/reordered Wait packet never permits local buttons.
    ft::Fixture();assert(!rr::DecisionFor(401));auto before=ram;
    for(uint32_t s:{7u,8u,9u,10u,11u,12u}) {
        W32(machine+4,s);ft::Tick();assert(rr::g_result_hold && ft::Poll()==0 && ft::Poll(0)==0);
    }
    W32(machine+4,12);rr::OnAfter(ft::Street());ft::Tick();assert(ft::Pulse()==2);
    assert(R32(ui::list+0x268)==0); // planning/injection never edits menu selection itself
    ui::Select(ui::list,ui::entries,4,0,1);test_now+=200;ft::Tick();assert(ft::Pulse()==2);
    ui::Select(ui::list,ui::entries,4,0,2);test_now+=200;ft::Tick();assert(ft::Pulse()==0x1000);
    test_now+=200;ft::Tick();assert(ft::Poll()==0 && !rr::g_result_loading); // don't repeat confirmation
    ft::Confirm();test_now+=200;ft::Tick();assert(ft::Pulse()==1); // default No, never confirm it
    ui::Select(ui::yesNo,ui::yesNoEntries,4,0,0);test_now+=200;ft::Tick();assert(ft::Pulse()==0x1000);
    test_now+=200;ft::Tick();assert(ft::Poll()==0); // Quit confirmation also consumed once
    W32(race,0x46A7D8);W32(setup+0x18,1);W32(machine+4,6);ft::Tick();
    assert(!rr::g_result_hold && ft::Poll(0x1000)==0); // raw held A cannot leak to a subsequent menu
    uint16_t b=0;assert(!rr::ControlInput(b));b=0x2000;assert(!rr::ControlInput(b)&&b==0x2000);

    // A revised decision invalidates a queued gesture before the guest receives it.
    ft::Fixture();rr::OnAfter(ft::Street());ft::Tick();assert(rr::g_result_plan.button==2);
    rr::OnAfter(ft::Street(2));assert(ft::Poll()==0 && !rr::g_result_plan.button);
    ft::Tick();assert(ft::Pulse()==2);
    ft::Fixture();rr::OnAfter(ft::Street());ft::Tick();
    W32(ui::front+0xD8,6);W32(ui::front+0xFC,0x1C);assert(ft::Poll()==0 && ft::Poll()==0);
    assert(!rr::g_result_plan.button); // no pulse into an unrelated confirmation

    // Tournament overlays use their own native dismiss handler; neither it nor EC pages activate Next early.
    ft::Fixture();ft::Next();rr::OnAfter(ft::Street());W32(machine+4,9);
    W32(ui::front+0xD8,1);G<uint8_t>(ui::front+0x222)=1;
    W32(ui::task+0xEC,3);W32(ui::task+0xE8,ui::taskWidget);W32(ui::task+0xE4,ui::taskInput);
    W32(ui::taskWidget,0x46FFF8);W32(ui::taskWidget+0x44,1);W32(ui::taskInput+0x44,0x201);
    W32(ui::taskWidget+0x58,0x257000);
    assert(rr::CanDismissTournamentOverlay());ft::Tick();assert(ft::Pulse()==0x1000);
    assert(G<uint8_t>(ui::front+0x222)==1 && rr::g_race_serial==401 && !rr::g_result_loading);
    G<uint8_t>(ui::front+0x222)=0;W32(ui::task+0xEC,1);test_now+=200;
    ft::Tick();assert(ft::Pulse()==0x1000 && rr::g_race_serial==401);
    W32(ui::task+0xEC,2);test_now+=200;ft::Tick();assert(ft::Pulse()==0x1000 && rr::g_race_serial==401);
    W32(ui::task+0xEC,3);G<uint8_t>(ui::taskWidget+0x358)=1;test_now+=200;
    ft::Tick();assert(ft::Poll()==0 && rr::g_race_serial==401);

    // Next requires the actual local task's next record and the matching received setup.
    ft::Fixture();auto next=ft::Next();rr::OnAfter(next);ft::Tick();
    assert(rr::g_start_in.serial==402 && rr::g_race_serial==401 && !rr::g_result_plan.button);
    rr::Setup authority{402,1,0xFFFFFF1F,100,0,0,8,{}};rr::OnSetup(authority);
    ui::Select(ui::list,ui::entries,4,0,1);ft::Tick();assert(rr::g_result_plan.button==0x1000);
    assert(rr::g_race_serial==401);assert(ft::Pulse()==0x1000);
    assert(rr::g_race_serial==402 && rr::g_result_loading && !rr::g_active_ready && !rr::g_bound_race);
    test_now+=200;ft::Tick();assert(ft::Poll()==0 && rr::g_result_loading);
    // Native loader exposes phase1 before allocating racers: apply received setup to the matching filename.
    for(uint32_t s:{setup,next_setup})std::memcpy(mod::HostPtr(s+0x68),ft::newName,sizeof ft::newName);
    W32(machine+4,1);rr::ApplyHostSetup();assert(R32(setup+0x3D4)==0xFFFFFF1F);
    W32(machine+4,4);rr::Tick(false,true);rr::Tick(false,true);
    assert(rr::g_race_serial==402 && rr::g_active_ready && rr::g_start_done && rr::g_bound_race==race &&
           rr::g_done_serial==402 && !rr::g_result_loading && !rr::g_partner_won);
    // Old decisions cannot release a later result; late packets leave the new race's credit independent.
    rr::OnAfter(ft::Street(3));W32(machine+4,12);ft::Tick();assert(rr::g_result_hold && ft::Poll()==0);
    rr::OnPartnerWon(401,99999);assert(!rr::g_partner_won);

    // No setup/changed cursor after queueing means no A and no activation; disconnect restores normal input.
    ft::Fixture();next=ft::Next();rr::OnAfter(next);ft::Tick();rr::OnSetup(authority);
    ui::Select(ui::list,ui::entries,4,0,1);ft::Tick();W32(ui::task+0xDC,1);
    assert(ft::Poll()==0 && ft::Poll()==0 && rr::g_race_serial==401 && !rr::g_result_loading);
    test_now+=200;ft::Tick();assert(rr::g_result_plan.button==2 && rr::g_result_plan_quit);
    ui::Select(ui::list,ui::entries,4,0,2);rr::g_result_plan={};ft::Tick();assert(ft::Pulse()==0x1000);
    assert(rr::g_race_serial==401 && rr::g_start_in.serial==402 && !rr::g_result_loading);
    ft::Confirm();test_now+=200;ft::Tick();assert(ft::Pulse()==1);
    ui::Select(ui::yesNo,ui::yesNoEntries,4,0,0);test_now+=200;ft::Tick();assert(ft::Pulse()==0x1000);
    // Street fallback retains the host start; it does not credit or activate it during the old result UI.
    W32(race,0x46A7D8);W32(setup+0x18,1);W32(machine+4,6);ft::Tick();
    assert(!rr::g_result_hold && rr::g_start_in.serial==402 && rr::g_done_serial==401);
    rr::ResultFlowTick(false,false,test_now);b=0x1000;assert(!rr::ControlInput(b)&&b==0x1000);

    // Setup history must outlive the active stage, just as the retained AfterNext does.
    rr::ResetNetwork();
    for(uint32_t serial:{401u,402u,403u}) {auto s=authority;s.serial=serial;rr::g_setup_history[serial%8]=s;}
    rr::BeginRace(403);bool saw[3]{};
    for(int i=0;i<6;i++){test_now+=100;rr::Setup s{};assert(rr::TakeSetup(s));saw[s.serial-401]=true;}
    assert(saw[0]&&saw[1]&&saw[2]);
    rr::ResetNetwork();rr::Setup cleared{};assert(!rr::TakeSetup(cleared));
    ::Fixture();rr::ResetFlowNetwork();rr::g_result_reset=false;test_now=saved_now;
    std::puts("PASS: result wait without Wait packets; native Quit/Yes navigation, pulse/revision guards, Next setup+local cursor, reused race binding, old-credit rejection and retained setup retransmit.");
}
