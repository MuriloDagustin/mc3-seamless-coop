// Read-only native UI planning against synthetic guest RAM; no game or input is sent.
namespace race_ui_test {
constexpr uint32_t app=0x1D0000, menus=0x1D1000, front=0x1D2000, mainPage=0x1D3000;
constexpr uint32_t list=0x1D4000, entries=0x1D5000, game=0x1D6000;
constexpr uint32_t confirmPage=0x1D7000, dialog=0x1D8000, dialogData=0x1D9000;
constexpr uint32_t yesNo=0x1DA000, yesNoEntries=0x1DB000;
constexpr uint32_t tasks=0x1DC000, taskEntries=0x1DD000, task=0x1DE000;
constexpr uint32_t taskWidget=0x1DF000, taskInput=0x1E0000;
constexpr uint32_t taskColumns=0x1E1000;

void Select(uint32_t menu,uint32_t buffer,uint32_t cap,uint32_t bank,uint32_t index) {
    uint32_t selection=menu+0x268+bank*16, entry=buffer+(bank*cap+index)*16;
    W32(selection,index);G<uint8_t>(selection+4)=1;
    W32(selection+8,R32(entry));W32(selection+12,R32(entry+4));
}

void List(uint32_t menu,uint32_t buffer,uint32_t vt,uint32_t cap,uint32_t count) {
    W32(menu,vt);W32(menu+0x44,1);W32(menu+0x230,cap);
    W32(menu+0x238,count);W32(menu+0x248,buffer);W32(menu+0x25C,0);
    for(uint32_t i=0;i<count;++i)W32(buffer+i*16,i);
    Select(menu,buffer,cap,0,0);
}

void Fixture() {
    ::Fixture();W32(machine+4,9);W32(0x53F2D4,2);
    W32(0x50F148,app);W32(app+0x0C,menus);W32(menus+0x0C,front);
    W32(menus+0x2C,mainPage);W32(menus+0xFC,list);
    W32(front+0xD8,5);W32(front+0xE0,1);W32(0x557ED0,game);G<uint8_t>(game+9)=1;
    List(list,entries,0x45DAF8,4,3);
    // Existing native entries: Continue at1; StartCruise at2. No forged runtime callback is used.
    uint32_t cont=list+0xEC+16, quit=list+0xEC+32;
    W32(cont,mainPage);W32(cont+4,0x16B7F0);W32(cont+12,1);
    W32(quit,front);W32(quit+4,0x1642C0);W32(quit+8,0x1A);W32(quit+12,2);
}

void Confirmation() {
    Fixture();W32(front+0xD8,6);W32(front+0xFC,0x1A);
    W32(menus+0xC0,confirmPage);W32(confirmPage,0x45DE48);W32(confirmPage+0x44,1);
    W32(menus+0x118,dialog);W32(dialog,0x45E0D0);W32(dialog+0x44,1);
    W32(dialog+0x60,dialogData);W32(dialogData+8,yesNo);
    List(yesNo,yesNoEntries,0x45DFA8,4,2);
    W32(yesNoEntries,4);W32(yesNoEntries+16,8);
    Select(yesNo,yesNoEntries,4,0,1); // native default is No, key8
}

void Tournament() {
    Fixture();W32(front+0xD8,1);W32(data+0x10,tasks);
    W32(tasks+0x78,taskEntries);W32(tasks+0x7C,taskEntries+12);W32(tasks+0x94,0);
    W32(taskEntries+8,task);W32(task,0x470348);G<uint8_t>(task+0x0C)=1;
    W32(task+0xEC,1);W32(task+0xE8,taskWidget);W32(task+0xE4,taskInput);
    W32(taskWidget,0x46FFF8);W32(taskWidget+0x44,1);W32(taskInput+0x44,0x201);
}

void ExpectPlan(bool quit,bool valid,uint16_t button=0,bool confirmation=false) {
    auto before=ram;mod::race::UiPlan plan{0xDEAD,0xBEEF,0xFFFF,true};
    assert(mod::race::PlanResultMenu(quit,plan)==valid);assert(ram==before);
    if(valid) {
        assert(plan.button==button && plan.confirm==confirmation);
        assert(plan.menu==(confirmation?yesNo:list) && plan.page==(confirmation?6u:5u));
    } else assert(!plan.menu && !plan.page && !plan.button && !plan.confirm);
}

void ExpectTournament(bool valid) {
    auto before=ram;assert(mod::race::CanAdvanceTournamentResult()==valid);assert(ram==before);
}

void Overlay() {
    Tournament();G<uint8_t>(front+0x222)=1;W32(taskWidget+0x58,taskColumns);
    W32(taskColumns+0x20,0xAABBCCDD); // native 264100 would write1; planning must not write it
}

void ExpectOverlay(bool valid) {
    auto before=ram;assert(mod::race::CanDismissTournamentOverlay()==valid);assert(ram==before);
}
} // namespace race_ui_test

void RaceUiTests() {
    namespace ui=race_ui_test;
    ui::Fixture();ui::ExpectPlan(true,true,2);
    ui::Select(ui::list,ui::entries,4,0,1);ui::ExpectPlan(true,true,2);
    ui::Select(ui::list,ui::entries,4,0,2);ui::ExpectPlan(true,true,0x1000);
    ui::ExpectPlan(false,true,1); // Continue uses menus+2C as its context
    ui::Select(ui::list,ui::entries,4,0,1);ui::ExpectPlan(false,true,0x1000);
    W32(ui::list+0xEC+16,ui::front);ui::ExpectPlan(false,false);

    // Count/order is read from the active bank; callback descriptors remain indexed by item.
    ui::Fixture();W32(ui::list+0x25C,2);W32(ui::list+0x238+8,3);
    for(uint32_t i=0;i<3;i++)W32(ui::entries+(8+i)*16,i);
    ui::Select(ui::list,ui::entries,4,2,2);ui::ExpectPlan(true,true,0x1000);

    ui::Confirmation();ui::ExpectPlan(true,true,1,true); // No -> Up -> Yes
    ui::Select(ui::yesNo,ui::yesNoEntries,4,0,0);ui::ExpectPlan(true,true,0x1000,true);
    ui::ExpectPlan(false,false); // Continue cannot approve a Quit confirmation

    for(uint32_t action:{0u,0x0Cu,0x1Cu,0xFFFFFFFFu}) {
        ui::Confirmation();W32(ui::front+0xFC,action);ui::ExpectPlan(true,false);
    }
    for(uint32_t wrongPage:{1u,4u,7u,0x3Au}) {
        ui::Fixture();W32(ui::front+0xD8,wrongPage);ui::ExpectPlan(true,false);
    }
    for(uint32_t offset:{0u,4u,8u,12u}) {
        ui::Fixture();W32(ui::list+0xEC+32+offset,0x12345678);ui::ExpectPlan(true,false);
    }
    ui::Fixture();W32(ui::entries+32+12,1);ui::ExpectPlan(true,false); // disabled
    ui::Fixture();W32(ui::entries+32+12,2);ui::ExpectPlan(true,false); // left/right action
    ui::Fixture();
    for(uint32_t j=0;j<4;++j)W32(ui::list+0xEC+j*4,R32(ui::list+0xEC+32+j*4));
    ui::ExpectPlan(true,false); // ambiguous duplicate callback

    for(uint32_t invalidBank:{3u,6u,0xFFFFFFFFu}) {
        ui::Fixture();W32(ui::list+0x25C,invalidBank);ui::ExpectPlan(true,false);
    }
    for(uint32_t invalidCap:{0u,21u,0xFFFFFFFFu}) {
        ui::Fixture();W32(ui::list+0x230,invalidCap);ui::ExpectPlan(true,false);
    }
    for(uint32_t invalidCount:{0u,5u,0xFFFFFFFFu}) {
        ui::Fixture();W32(ui::list+0x238,invalidCount);ui::ExpectPlan(true,false);
    }
    ui::Fixture();W32(ui::list+0x268,3);ui::ExpectPlan(true,false);
    ui::Fixture();G<uint8_t>(ui::list+0x26C)=0;ui::ExpectPlan(true,false);
    ui::Fixture();W32(ui::list+0x270,999);ui::ExpectPlan(true,false);
    ui::Fixture();W32(ui::list+0x274,999);ui::ExpectPlan(true,false);
    ui::Fixture();W32(ui::list+0x248,0x5FFFF0);ui::ExpectPlan(true,false);
    ui::Fixture();W32(ui::list,0x45DFA8);ui::ExpectPlan(true,false);
    ui::Fixture();W32(ui::list+0x44,0);ui::ExpectPlan(true,false);
    ui::Fixture();W32(ui::list+0x300,3);ui::ExpectPlan(true,false);
    ui::Fixture();G<uint8_t>(ui::list+0x358)=1;ui::ExpectPlan(true,false);
    ui::Fixture();W32(ui::front+0xE0,2);ui::ExpectPlan(true,false);
    ui::Fixture();G<uint8_t>(ui::game+9)=0;ui::ExpectPlan(true,false);
    ui::Fixture();W32(machine+4,6);ui::ExpectPlan(true,false);
    ui::Fixture();W32(race,0x46A7D8);ui::ExpectPlan(true,false);
    ui::Fixture();W32(0x53F2D4,5);ui::ExpectPlan(true,false);
    for(uint32_t object:{ui::confirmPage,ui::dialog,ui::yesNo}) {
        ui::Confirmation();W32(object,0x12345678);ui::ExpectPlan(true,false);
    }
    ui::Confirmation();W32(ui::dialog+0x64,1);ui::ExpectPlan(true,false);
    ui::Confirmation();W32(ui::yesNoEntries,1);ui::ExpectPlan(true,false); // Ok is not Yes
    ui::Confirmation();W32(ui::yesNoEntries+16,4);ui::ExpectPlan(true,false);
    ui::Confirmation();W32(ui::yesNoEntries+4,1);ui::ExpectPlan(true,false);
    ui::Confirmation();W32(ui::yesNoEntries+12,1);ui::ExpectPlan(true,false);
    ui::Confirmation();W32(ui::yesNo+0x238,1);ui::ExpectPlan(true,false);

    ui::Tournament();ui::ExpectTournament(true);
    W32(ui::task+0xEC,2);ui::ExpectTournament(true);
    W32(ui::task+0xEC,3);ui::ExpectTournament(false); // Close already authorized
    W32(ui::task+0xEC,0);ui::ExpectTournament(false);
    for(uint32_t animation:{1u,3u,4u,5u,6u}) {
        ui::Tournament();W32(ui::taskWidget+0x300,animation);ui::ExpectTournament(false);
    }
    ui::Tournament();G<uint8_t>(ui::taskWidget+0x358)=1;ui::ExpectTournament(false);
    ui::Tournament();G<uint8_t>(ui::front+0x222)=1;ui::ExpectTournament(false);
    ui::Tournament();W32(ui::front+0xD8,6);ui::ExpectTournament(false);
    ui::Tournament();W32(ui::front+0xFC,0x1A);ui::ExpectTournament(false);
    for(uint32_t inputFlags:{0u,1u,0x200u}) {
        ui::Tournament();W32(ui::taskInput+0x44,inputFlags);ui::ExpectTournament(false);
    }
    ui::Tournament();W32(ui::taskWidget,0x45DAF8);ui::ExpectTournament(false);
    ui::Tournament();W32(ui::task,0x12345678);ui::ExpectTournament(false);
    ui::Tournament();G<uint8_t>(ui::task+0x0C)=0;ui::ExpectTournament(false);
    ui::Tournament();W32(ui::tasks+0x94,1);ui::ExpectTournament(false);
    ui::Tournament();W32(ui::tasks+0x94,0xFFFFFFFF);ui::ExpectTournament(false);
    ui::Tournament();W32(ui::tasks+0x7C,ui::taskEntries+11);ui::ExpectTournament(false);
    ui::Tournament();W32(machine+4,10);ui::ExpectTournament(false);
    ui::Overlay();ui::ExpectOverlay(true);ui::ExpectTournament(false);
    // Unlike normal result advance, this native consumer does not use/change EC.
    for(uint32_t phase:{0u,1u,2u,3u}) {
        W32(ui::task+0xEC,phase);ui::ExpectOverlay(true);
    }
    for(uint8_t flag:{uint8_t(0),uint8_t(2)}) {
        ui::Overlay();G<uint8_t>(ui::front+0x222)=flag;ui::ExpectOverlay(false);
    }
    for(uint32_t page:{5u,6u,0x3Au}) {
        ui::Overlay();W32(ui::front+0xD8,page);ui::ExpectOverlay(false);
    }
    ui::Overlay();W32(ui::front+0xFC,0x1A);ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::taskWidget+0x58,0);ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::taskWidget+0x58,0x5FFFE0);ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::taskWidget,0x45DAF8);ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::taskWidget+0x44,0);ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::taskWidget+0x300,3);ui::ExpectOverlay(false);
    ui::Overlay();G<uint8_t>(ui::taskWidget+0x358)=1;ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::taskInput+0x44,1);ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::task,0x12345678);ui::ExpectOverlay(false);
    ui::Overlay();G<uint8_t>(ui::task+0x0C)=0;ui::ExpectOverlay(false);
    ui::Overlay();W32(ui::tasks+0x94,1);ui::ExpectOverlay(false);
    ui::Overlay();W32(machine+4,12);ui::ExpectOverlay(false);
    ui::Overlay();W32(0x53F2D4,5);ui::ExpectOverlay(false);
    puts("PASS: native result item/context, active banks, Quit Yes/No keys, read-only guards, tournament advance and overlay dismissal.");
}
