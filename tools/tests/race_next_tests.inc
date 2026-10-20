// Exercise the real read-only native-next helpers with synthetic Xbox memory only.
namespace race_next_test {
namespace rr = mod::race;
constexpr uint32_t db=0x180000,names=0x181000,name=0x182000,descriptors=0x183000;
constexpr uint32_t open=0x184000,pending=0x185000,manager=0x186000,tasks=0x187000;
constexpr uint32_t task=0x188000,items=0x189000;
constexpr int city=2,index=7;
constexpr uint16_t local_id=41,host_id=901;
constexpr uint32_t row=db+city*0x50,descriptor=descriptors+index*0x30;
constexpr uint32_t selected=open+0x4C,entry=items+8;
constexpr char full[]="circuit\\easy\\identical_prefix_until_the_last_part_race01";

void Record(uint32_t at,uint16_t id,int race_index=index,int race_city=city) {
    std::memset(mod::HostPtr(at),0,0x4C);
    W32(at,0x473E74);G<uint16_t>(at+4)=id;G<int16_t>(at+6)=int16_t(race_index);
    G<int8_t>(at+8)=int8_t(race_city);G<uint8_t>(at+0x0B)=0x52;
}

void Fixture() {
    ::Fixture(); // resets RAM and BeginRace's legacy state; helpers themselves own no globals
    W32(0x557EE4,db);W32(row+0x14,8);W32(row+0x18,names);W32(row+0x20,descriptors);
    W32(names+index*4,name);std::memcpy(mod::HostPtr(name),full,sizeof full);
    for(uint32_t s:{setup,next_setup}) {
        W32(s,city);W32(s+0x18,7);std::memcpy(mod::HostPtr(s+0x68),full,sizeof full);
    }
    W32(descriptor,7);W32(data+0x50,city);G<uint16_t>(data+0x58)=local_id;
    W32(data+0xA5C,open);W32(data+0xA60,open+2*0x4C);W32(data+0xA64,open+4*0x4C);
    Record(open,13,3);Record(selected,local_id);
    W32(data+0xA6C,pending);W32(data+0xA70,pending+0x50);W32(data+0xA74,pending+4*0x50);
    Record(pending,29,5);W32(pending+0x4C,0xA1B2C3D4); // native Pending's trailing four bytes
    W32(data+0x10,manager);W32(manager+0x78,tasks);W32(manager+0x7C,tasks+24);
    W32(manager+0x94,1);W32(tasks+12+8,task);
    W32(task,0x470348);G<uint8_t>(task+0x0C)=1;G<uint8_t>(task+0xE0)=1;
    W32(task+0x10,3);W32(task+0x14,items);W32(task+0xDC,1);
    G<uint8_t>(items+4)=1;G<uint8_t>(items+2*8+4)=1;
    G<uint16_t>(entry+6)=local_id;
}

rr::Start HostStart() {
    rr::Start out{};out.serial=1201;out.city=city;out.race_idx=index;
    std::memcpy(out.record,mod::HostPtr(selected),sizeof out.record);
    std::memcpy(out.record+4,&host_id,sizeof host_id); // each career assigns its own UniqueId
    out.record[0x0B]=0xA8; // host's opponents/difficulty need not match the local record
    std::strcpy(out.name,"truncated display label");return out;
}

void Loaded(bool expected) {
    rr::Start out{};std::memset(&out,0xA7,sizeof out);auto untouched=out;auto before=ram;
    assert(rr::ResolveHostLoadedStart(out)==expected);assert(ram==before);
    if(!expected) { assert(!std::memcmp(&out,&untouched,sizeof out));return; }
    assert(out.serial==0 && out.city==city && out.race_idx==index);
    assert(!std::memcmp(out.record,mod::HostPtr(selected),sizeof out.record));
    assert(!std::strncmp(out.name,full,sizeof out.name-1) && out.name[sizeof out.name-1]==0);
}

void Next(const rr::Start& start,bool expected) {
    auto before=ram;auto input=start;
    assert(rr::NativeNextMatches(start)==expected);assert(ram==before);
    assert(!std::memcmp(&start,&input,sizeof start));
}

void Next(bool expected) { Next(HostStart(),expected); }
}

void RaceNextTests() {
    namespace nt=race_next_test;
    // A real identity is a full CURRENT name + descriptor + current career UUID, not NEXT or a display prefix.
    nt::Fixture();nt::Loaded(true);nt::Loaded(true);
    nt::Fixture();W32(setup+0x18,21);W32(nt::descriptor,21);nt::Loaded(true);
    nt::Fixture();
    W32(next_setup+0x18,21);G<uint8_t>(next_setup+0x68)='x';nt::Loaded(true);
    nt::Fixture();W32(0x53F2E0,0);nt::Loaded(false);
    nt::Fixture();W32(0x557F0C,0);nt::Loaded(false);
    nt::Fixture();W32(career+0x0C,0);nt::Loaded(false);
    nt::Fixture();G<uint16_t>(data+0x58)=0;nt::Loaded(false);
    nt::Fixture();G<uint16_t>(data+0x58)=123;nt::Loaded(false);
    nt::Fixture();G<uint16_t>(nt::open+4)=nt::local_id;nt::Loaded(false); // duplicate UUID within Open
    nt::Fixture();W32(nt::selected,0x470348);nt::Loaded(false);
    nt::Fixture();G<uint8_t>(setup+0x68+sizeof nt::full-3)^=1;nt::Loaded(false); // difference beyond 32-byte label
    nt::Fixture();W32(setup+0x18,21);nt::Loaded(false);
    for(uint32_t type:{0u,1u}) {
        nt::Fixture();W32(setup+0x18,type);W32(nt::descriptor,type);nt::Loaded(false);
    }
    nt::Fixture();W32(setup,0);nt::Loaded(false);
    nt::Fixture();W32(data+0x50,0);nt::Loaded(false);
    nt::Fixture();G<uint8_t>(setup+0x108)=1;nt::Loaded(false);
    for(uint32_t count:{0u,7u,161u,UINT32_MAX}) {
        nt::Fixture();W32(nt::row+0x14,count);nt::Loaded(false);
    }
    nt::Fixture();W32(0x557EE4,0xFFFFFFF0);nt::Loaded(false);
    nt::Fixture();W32(nt::row+0x18,0xFFFFFFF0);nt::Loaded(false);
    nt::Fixture();W32(nt::row+0x20,0xFFFFFFF0);nt::Loaded(false);
    nt::Fixture();W32(nt::names+nt::index*4,0);nt::Loaded(false);
    nt::Fixture();std::memset(mod::HostPtr(nt::name),'a',128);nt::Loaded(false);
    nt::Fixture();W32(nt::names+nt::index*4,uint32_t(ram.size()-2));
    ram[ram.size()-2]='a';ram[ram.size()-1]='b';nt::Loaded(false); // missing terminator at mapping boundary
    nt::Fixture();W32(data+0xA60,nt::open+0x4D);nt::Loaded(false);
    nt::Fixture();W32(data+0xA64,nt::open);nt::Loaded(false);
    nt::Fixture();W32(data+0xA60,nt::open+513*0x4C);W32(data+0xA64,nt::open+513*0x4C);nt::Loaded(false);
    nt::Fixture();W32(data+0xA5C,uint32_t(ram.size()-16));W32(data+0xA60,uint32_t(ram.size()-16+0x4C));
    W32(data+0xA64,uint32_t(ram.size()-16+0x4C));nt::Loaded(false);

    // Predict a middle entry of an active task. Local UUID41 deliberately differs from host UUID901.
    nt::Fixture();nt::Next(true);nt::Next(true);
    nt::Fixture();W32(setup+0x18,1);W32(machine+4,12);G<uint8_t>(setup+0x68)='x';nt::Next(true);
    // Move the selected race to the SECOND Pending entry: its stride is50, not the Open stride4C.
    nt::Fixture();auto host=nt::HostStart();W32(data+0xA60,nt::open+0x4C);
    nt::Record(nt::pending+0x50,nt::local_id);W32(data+0xA70,nt::pending+2*0x50);
    nt::Next(host,true);nt::Loaded(false); // loaded resolver intentionally uses Open only
    nt::Fixture();nt::Record(nt::pending+0x50,nt::local_id);W32(data+0xA70,nt::pending+2*0x50);
    nt::Next(false); // same UUID in Open and Pending is ambiguous
    nt::Fixture();W32(data+0xA6C,0);W32(data+0xA70,0);W32(data+0xA74,0);nt::Next(true);
    nt::Fixture();W32(data+0xA5C,0);W32(data+0xA60,0);W32(data+0xA64,0);nt::Next(false);

    nt::Fixture();host=nt::HostStart();host.record[8]=0;nt::Next(host,false);
    nt::Fixture();host=nt::HostStart();int16_t bad_index=6;std::memcpy(host.record+6,&bad_index,2);nt::Next(host,false);
    nt::Fixture();host=nt::HostStart();host.city=-1;nt::Next(host,false);
    nt::Fixture();host=nt::HostStart();host.race_idx=160;nt::Next(host,false);
    nt::Fixture();host=nt::HostStart();G<int16_t>(nt::selected+6)=6;nt::Next(host,false);
    nt::Fixture();host=nt::HostStart();G<int8_t>(nt::selected+8)=1;nt::Next(host,false);
    nt::Fixture();W32(nt::task,0x47034C);nt::Next(false);
    nt::Fixture();G<uint8_t>(nt::task+0x0C)=0;nt::Next(false);
    nt::Fixture();G<uint8_t>(nt::task+0xE0)=0;nt::Next(false);
    nt::Fixture();G<uint8_t>(nt::entry+4)=1;nt::Next(false);
    nt::Fixture();G<uint16_t>(nt::entry+6)=0;nt::Next(false);
    nt::Fixture();G<uint16_t>(nt::entry+6)=nt::host_id;nt::Next(false); // a host UUID must not resolve locally
    nt::Fixture();W32(nt::selected,0xDEADBEEF);nt::Next(false);
    nt::Fixture();W32(nt::pending,0xDEADBEEF);nt::Next(false);
    for(uint32_t cursor:{3u,512u,UINT32_MAX}) {
        nt::Fixture();W32(nt::task+0xDC,cursor);nt::Next(false);
    }
    for(uint32_t count:{0u,1u,513u,UINT32_MAX}) {
        nt::Fixture();W32(nt::task+0x10,count);nt::Next(false);
    }
    nt::Fixture();W32(nt::manager+0x94,UINT32_MAX);nt::Next(false);
    nt::Fixture();W32(nt::manager+0x94,2);nt::Next(false);
    nt::Fixture();W32(nt::manager+0x7C,nt::tasks+13);nt::Next(false);
    nt::Fixture();W32(nt::manager+0x7C,nt::tasks+513*12);nt::Next(false);
    nt::Fixture();W32(nt::manager+0x78,0xFFFFFFF0);W32(nt::manager+0x7C,8);nt::Next(false);
    nt::Fixture();W32(nt::tasks+20,0);nt::Next(false);
    nt::Fixture();W32(nt::task+0x14,0xFFFFFFFC);nt::Next(false);
    nt::Fixture();W32(nt::task+0x14,uint32_t(ram.size()-12));nt::Next(false);
    nt::Fixture();W32(data+0x10,0);nt::Next(false);
    nt::Fixture();W32(data+0xA70,nt::pending+0x51);nt::Next(false);
    nt::Fixture();W32(data+0xA74,nt::pending);nt::Next(false);
    nt::Fixture();W32(data+0xA70,nt::pending+513*0x50);W32(data+0xA74,nt::pending+513*0x50);nt::Next(false);
    ::Fixture();
    puts("PASS: loaded/native-next identity, local UUIDs, Open/Pending strides and malformed contexts; no guest RAM writes.");
}
