// Include ai.cpp alongside car/race/traffic sources, then this file after Fixture/ram exist.
// Call AiMemoryTests() from main. This uses only the synthetic guest address space.
void AiMemoryTests() {
    Fixture();
    using namespace mod;
    constexpr uint32_t mgr=0x120000, arr=0x121000, desc=0x122000, prog=0x123000;
    constexpr uint32_t veh=0x124000, acar=0x125000, inst=0x126000, coll=0x127000, body=0x128000;
    constexpr uint32_t cfg=0x129000, name=0x12A000, game=0x12B000;
    W32(0x557ED0,game);W32(game+4,4);W32(machine+4,5);
    W32(0x534074,mgr);W32(mgr,1);W32(mgr+4,1);W32(mgr+8,arr);
    W32(::race+0x2C,desc);W32(::race+0x658,prog);W32(::race+0x10,0);W32(::race+0x24,12);
    G<uint8_t>(desc+9)=1;W32(desc+0x64,cfg);std::memcpy(HostPtr(cfg+0xE3),"car_a",6);
    W32(prog+0x5C,name);std::memcpy(HostPtr(name),"opponent",9);
    W32(arr+0x20,veh);W32(veh+8,acar);W32(veh+0x1C,inst);W32(acar,0x4564F0);
    W32(acar+0x68,coll);W32(coll+0x10,body);W32(coll+0x14,inst+0x10);
    G<float>(body+4)=1000;for(int j=0;j<3;j++)G<float>(body+0x0C+j*4)=200;
    for(int j=0;j<3;j++)G<float>(inst+0x10+(j*3+j)*4)=1;
    G<float>(inst+0x34)=100;G<float>(body+0x80)=100;G<float>(body+0x84)=0.5f;
    G<float>(body+0x44)=10;
    ai::Stop();ai::Tick(false,test_now);ai::SetRace(400,0,2);
    std::vector<std::vector<uint8_t>> packets;
    assert(ai::Snapshot(packets,test_now+50) && packets.size()==1);
    auto packet=packets[0];
    ai::Receive(packet.data(),int(packet.size()));
    std::memcpy(HostPtr(name),"cosmetic_peer",14); // career-local names are not opponent identity
    G<float>(inst+0x34)=200;W32(acar+0x15C,4);
    ai::Tick(true,test_now);
    assert(R32(acar+0x15C)==0x14 && G<float>(inst+0x34)==100);
    assert(G<float>(body+0x2C)==10000 && G<uint8_t>(prog+4)==0);
    W32(acar+0x15C,0x16);ai::Stop();
    assert(R32(acar+0x15C)==0x16); // Stop itself is never a guest writer.
    ai::Tick(false,test_now);assert(R32(acar+0x15C)==6); // preserve native crash bits
    ai::SetRace(401,0,2);ai::Receive(packet.data(),int(packet.size()));ai::Tick(true,test_now);
    assert(R32(acar+0x15C)==6); // stale generation rejected
    ai::SetRace(400,0,2);ai::Receive(packet.data(),int(packet.size()));
    ai::Tick(true,test_now+501);assert(R32(acar+0x15C)==6); // loss releases brain
    ai::Stop();ai::Tick(false,test_now);ai::SetRace(400,0,2);
    auto invalid=packet;float nan=std::numeric_limits<float>::quiet_NaN();
    std::memcpy(invalid.data()+sizeof(ai::WireHeader),&nan,4);ai::Receive(invalid.data(),int(invalid.size()));
    ai::Tick(true,test_now);assert(R32(acar+0x15C)==6);
    ai::Receive(packet.data(),int(packet.size()));G<uint8_t>(cfg+0xE3)='z';
    ai::Tick(true,test_now);assert(R32(acar+0x15C)==6); // manifest mismatch
    G<uint8_t>(cfg+0xE3)='c';W32(machine+4,4);ai::Tick(true,test_now);
    assert(R32(acar+0x15C)==6); // countdown reset path is excluded
    W32(machine+4,6);ai::SetRace(400,0,2,true);
    auto final_packet=packet;ai::WireCar final_car{};
    std::memcpy(&final_car,final_packet.data()+sizeof(ai::WireHeader),sizeof(final_car));
    final_car.finished=1;final_car.finish_time=123;final_car.place=1;final_car.laps=0;
    final_car.checkpoint=-1;final_car.checkpoints=12;
    for(auto& mask:final_car.checkpoint_mask)mask=0xFFFFFFFF;
    std::memcpy(final_packet.data()+sizeof(ai::WireHeader),&final_car,sizeof(final_car));
    ai::Receive(final_packet.data(),int(final_packet.size()));ai::Tick(true,test_now);
    assert(G<uint8_t>(prog+4)==1 && R32(::race+0x1254)==1 && R32(prog+0x5C)==name);
    assert(R32(prog+0x18)==0 && G<int16_t>(prog+0x34)==-1 && R32(prog+0x58)==0xFFFFFFFF);
    ai::Tick(true,test_now);assert(R32(::race+0x1254)==1); // idempotent final import
    ai::Stop();ai::Tick(false,test_now);ai::SetRace(400,0,2,true);
    auto bad_progress=final_packet;auto bad_car=final_car;bad_car.checkpoint=12; // N, not a valid index
    std::memcpy(bad_progress.data()+sizeof(ai::WireHeader),&bad_car,sizeof bad_car);
    ai::Receive(bad_progress.data(),int(bad_progress.size()));ai::Tick(true,test_now);
    assert(R32(acar+0x15C)==6 && ai::g_applied==0);
    ai::Stop();ai::Tick(false,test_now);ai::SetRace(400,0,2,true);
    bad_car.checkpoint=-1;bad_car.checkpoint_aux=13;
    std::memcpy(bad_progress.data()+sizeof(ai::WireHeader),&bad_car,sizeof bad_car);
    ai::Receive(bad_progress.data(),int(bad_progress.size()));ai::Tick(true,test_now);
    assert(R32(acar+0x15C)==6 && ai::g_applied==0);
    ai::Stop();ai::Tick(false,test_now);ai::SetRace(401,0,2,true);
    W32(machine+4,7);std::vector<std::vector<uint8_t>> terminal_packets;
    assert(ai::Snapshot(terminal_packets,test_now+100));
    ai::WireHeader terminal_head{};std::memcpy(&terminal_head,terminal_packets[0].data(),sizeof terminal_head);
    assert(terminal_head.reserved==1);
    W32(machine+4,5);ai::Receive(terminal_packets[0].data(),int(terminal_packets[0].size()));
    ai::Tick(true,test_now+3000);assert(R32(acar+0x15C)==0x16);
    W32(machine+4,7);W32(prog+0x14,7);ai::Tick(true,test_now+3050);
    assert(R32(prog+0x14)==1 && R32(acar+0x15C)==0x16); // result HUD keeps host rank/pose authority
    ai::Stop();ai::Tick(false,test_now+3000);assert(R32(acar+0x15C)==6);
    puts("AI synthetic memory: passed");
}

