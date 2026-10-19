// Real race::Context and car::DriveWorld/Tick against synthetic RAM. No game, socket or native callback.
namespace world_pose_test {
namespace rr = mod::race;
namespace cc = mod::car;
constexpr uint32_t db = 0x210000, names = 0x211000, name = 0x212000;
constexpr uint32_t vehicle0 = 0x220000, vehicle1 = 0x222000;
constexpr char filename[] = "circuit\\easy\\identical_prefix_1234567890_full_database_filename_not_display_label";

void Chain(uint32_t player, uint32_t vehicle, float x) {
    W32(player + 0x14, vehicle);
    W32(vehicle + 8, vehicle + 0x200); W32(vehicle + 0x1C, vehicle + 0x400);
    W32(vehicle + 0x200 + 0x68, vehicle + 0x500); W32(vehicle + 0x500 + 0x10, vehicle + 0x600);
    for (int i = 0; i < 3; ++i) {
        G<float>(vehicle + 0x400 + 0x10 + uint32_t(i * 16)) = 1;
        G<float>(vehicle + 0x600 + 0x5C + uint32_t(i * 16)) = 1;
    }
    G<float>(vehicle + 0x400 + 0x34) = x;
    G<float>(vehicle + 0x600 + 0x80) = x;
}

void Fixture(bool cruise = false) {
    ::Fixture(); test_now = 10000;
    rr::g_have_start = false; rr::g_start_in = {}; rr::g_start_out = {};
    rr::g_start_done = false; rr::g_done_serial = 0;
    rr::g_bound_race = 0; rr::g_bound_ran = rr::g_bound_ended = false;
    W32(0x53F2D4, 2); W32(machine + 4, 5);
    W32(0x557EE4, db); W32(db + 0x18, names); W32(names + 7 * 4, name);
    std::memcpy(mod::HostPtr(name), filename, sizeof filename);
    for (uint32_t s : {setup, next_setup}) {
        std::memcpy(mod::HostPtr(s + 0x68), filename, sizeof filename);
        W32(s + 0x18, cruise ? 1 : 4);
    }
    W32(race, cruise ? 0x46A7D8 : 0x46B2D8);
    if (!cruise) {
        rr::Start start{}; start.serial = 401; start.city = 0; start.race_idx = 7;
        std::strcpy(start.name, "short display label");
        rr::OnStart(start); rr::g_bound_race = race;
        rr::g_start_done = true; rr::g_done_serial = start.serial;
        rr::g_active_ready = true; // Fixture models a race already consumed by the native marker.
    }
    Chain(0x10A000, vehicle0, -10); Chain(0x10B000, vehicle1, -20);
    cc::g_grid = 0; cc::g_grid_done = true;
    cc::g_world_car = vehicle0 + 0x200; cc::g_world_at = test_now - 2000;
}

void AssertUnavailable() {
    auto before = ram;
    const auto world = rr::Context();
    assert(!rr::ValidWorld(world) && ram == before); // context publication never mutates guest RAM
}

cc::Pose Pose(float x) {
    cc::Pose p{};
    p.m[0][0] = p.m[1][1] = p.m[2][2] = 1;
    p.m[3][0] = x; p.m[3][1] = 2; p.m[3][2] = 3;
    p.throttle = 0.5f;
    return p;
}

void SetConfig() {
    std::array<uint8_t, cc::kCarCfgBytes> cfg{};
    std::memcpy(cfg.data() + 0xE3, "vp_test_car", 12); cfg[0x36] = 19;
    cc::OnPartnerCar(1, cfg.data(), 77);
}

float RemoteX() { return G<float>(vehicle1 + 0x400 + 0x34); }
}

void WorldPoseTests() {
    namespace wt = world_pose_test;
    namespace rr = mod::race;
    namespace cc = mod::car;
    const uint64_t saved_now = test_now;

    wt::Fixture(true);
    auto world = rr::Context();
    assert(rr::MatchesWorld(world, rr::World{0, -1, 0, 1}));
    // A pending network start does not turn an existing cruise into that race.
    rr::Start start{}; start.serial = 501; start.city = 0; start.race_idx = 7;
    rr::OnStart(start);
    assert(rr::MatchesWorld(rr::Context(), rr::World{0, -1, 0, 1}));
    for (uint32_t state : {2u, 3u, 4u, 5u, 6u}) {
        W32(machine + 4, state); assert(rr::ValidWorld(rr::Context()));
    }
    for (uint32_t state : {0u, 1u, 7u, 11u, 12u, 13u}) {
        W32(machine + 4, state); wt::AssertUnavailable();
    }
    W32(machine + 4, 5); W32(setup + 0x18, 4); wt::AssertUnavailable();
    W32(setup + 0x18, 1);
    for (int8_t city : {int8_t(-1), int8_t(4)}) { G<int8_t>(setup) = city; wt::AssertUnavailable(); }
    G<int8_t>(setup) = 3; assert(rr::MatchesWorld(rr::Context(), rr::World{0, -1, 3, 1}));

    wt::Fixture();
    world = rr::Context(); assert(rr::MatchesWorld(world, rr::World{401, 7, 0, 2}));
    for (uint32_t state : {2u, 6u, 7u, 11u, 12u}) {
        W32(machine + 4, state); assert(rr::MatchesWorld(rr::Context(), world));
    }
    for (uint32_t state : {1u, 13u}) { W32(machine + 4, state); wt::AssertUnavailable(); }
    W32(machine + 4, 5);
    for (uint32_t mode : {0u, 3u, 5u}) { W32(0x53F2D4, mode); wt::AssertUnavailable(); }
    W32(0x53F2D4, 2); G<uint8_t>(setup + 0x108) = 1; wt::AssertUnavailable();
    G<uint8_t>(setup + 0x108) = 0;
    // The abbreviated display label is insufficient: compare the full local database filename.
    G<uint8_t>(setup + 0x68 + 50) ^= 1; wt::AssertUnavailable();
    G<uint8_t>(setup + 0x68 + 50) ^= 1;
    G<int8_t>(setup) = 1; wt::AssertUnavailable(); G<int8_t>(setup) = 0;
    rr::g_active_start.serial = 400; wt::AssertUnavailable(); rr::g_active_start.serial = 401;
    rr::g_bound_race = race + 0x2000; wt::AssertUnavailable(); rr::g_bound_race = race;
    rr::g_have_start = false; assert(rr::MatchesWorld(rr::Context(), world)); // active race outlives its pending queue
    rr::g_start_out = rr::g_start_in; assert(rr::MatchesWorld(rr::Context(), world)); // host side
    rr::g_start_out = {}; rr::g_have_start = true;
    rr::OnStart(start); // next start waits while the old race remains bound
    assert(rr::MatchesWorld(rr::Context(), world));
    rr::g_bound_race = 0;
    W32(holder + 4, 0); wt::AssertUnavailable(); // zero loaded == zero bound must not publish a race
    W32(holder + 4, race); rr::g_bound_race = race;
    assert(rr::MatchesWorld(rr::Context(), world));

    // World changes invalidate interpolation/clock state while retaining ordered car configuration.
    wt::Fixture(); wt::SetConfig(); world = rr::Context();
    const auto cfg = cc::g_remote[1].cfg;
    const auto revision = cc::g_remote[1].revision;
    cc::DriveWorld(1, wt::Pose(100), 1000, world);
    cc::DriveWorld(1, wt::Pose(110), 1010, world);
    assert(cc::g_remote[1].samples.size() == 2 && cc::g_remote[1].offset_set);
    auto other = world; ++other.serial;
    cc::DriveWorld(1, wt::Pose(200), 10, other); // reset clock domain, even if its timestamp is lower
    assert(cc::g_remote[1].samples.size() == 1 && cc::g_remote[1].samples.front().at == 10);
    assert(cc::g_remote[1].samples.front().pose.m[3][0] == 200);
    assert(cc::g_remote[1].have_cfg && cc::g_remote[1].active && cc::g_remote[1].cfg == cfg);
    assert(cc::g_remote[1].revision == revision && cc::g_remote[1].cfg_sequence == 77);
    cc::DriveWorld(1, wt::Pose(300), 20, {});
    assert(cc::g_remote[1].samples.empty() && !cc::g_remote[1].offset_set);
    assert(cc::g_remote[1].have_cfg && cc::g_remote[1].cfg == cfg && cc::g_remote[1].cfg_sequence == 77);

    // Tick rejects queued peer state from another serial BEFORE writing either visible matrix or controls.
    wt::Fixture(); wt::SetConfig(); world = rr::Context();
    other = world; ++other.serial;
    cc::DriveWorld(1, wt::Pose(999), test_now, other);
    const float before = wt::RemoteX();
    G<float>(wt::vehicle1 + 0x200 + 0x70) = 0.125f;
    cc::Tick(test_now);
    assert(cc::g_remote[1].samples.empty() && !cc::g_remote[1].offset_set);
    assert(wt::RemoteX() == before && G<float>(wt::vehicle1 + 0x200 + 0x70) == 0.125f);
    assert(cc::g_remote[1].have_cfg && cc::g_remote[1].cfg == cfg);
    cc::DriveWorld(1, wt::Pose(123), test_now, world); cc::Tick(test_now);
    assert(wt::RemoteX() == 123 && G<float>(wt::vehicle1 + 0x200 + 0x70) == 0.5f);

    // No packet is needed to flush an old queue when the LOCAL context changes or becomes unavailable.
    for (int transition = 0; transition < 4; ++transition) {
        wt::Fixture(); wt::SetConfig(); world = rr::Context();
        cc::DriveWorld(1, wt::Pose(456), test_now, world);
        if (transition == 0) W32(0x53F2D4, 5); // garage retains the same player/car pointers
        if (transition == 1) { W32(race, 0x46A7D8); W32(setup + 0x18, 1); } // race -> street
        if (transition == 2) { ++rr::g_race_serial; ++rr::g_start_in.serial; ++rr::g_active_start.serial; } // next authorized serial
        if (transition == 3) G<uint8_t>(setup + 0x68 + 50) ^= 1; // different complete filename
        const float x = wt::RemoteX();
        cc::Tick(test_now);
        assert(cc::g_remote[1].samples.empty() && !cc::g_remote[1].offset_set && wt::RemoteX() == x);
        assert(cc::g_remote[1].have_cfg && cc::g_remote[1].cfg == cfg && cc::g_remote[1].cfg_sequence == 77);
        // Returning to the old world without fresh state must not resurrect its previous pose.
        W32(0x53F2D4, 2); W32(race, 0x46B2D8); W32(setup + 0x18, 4);
        rr::g_race_serial = rr::g_start_in.serial = rr::g_active_start.serial = rr::g_done_serial = 401;
        std::memcpy(mod::HostPtr(setup + 0x68), wt::filename, sizeof wt::filename);
        cc::Tick(test_now); assert(wt::RemoteX() == x && cc::g_remote[1].samples.empty());
    }
    ::Fixture(); cc::g_world_car = 0; cc::g_world_at = 0; cc::g_grid_done = false;
    test_now = saved_now;
    std::puts("PASS: actual world publication; full identity/serial/load guards; scoped pose queue invalidation and config preservation.");
}

void WorldCreditTests() {
    namespace wt = world_pose_test;
    namespace rr = mod::race;
    namespace cc = mod::car;
    const uint64_t saved_now = test_now;
    auto terminal = [](uint32_t serial) {
        rr::Finished out{};
        out.serial = serial; out.won = 1; out.finish_time = 99;
        out.progress = 123; out.score = 456;
        return out;
    };
    auto gate_dependencies = [] {
        // Make gate arming otherwise possible, so a missing identity guard really would arm it.
        constexpr uint32_t session = 0x213000, hud = 0x214000, cps = 0x215000;
        W32(0x557EFC, session); W32(session + 0x10, 0xFFFFFFFF);
        W32(0x10A000 + 0x58, hud); W32(race + 0x24, 4); W32(race + 0x28, cps);
        rr::g_gate_requested = true;
        assert(rr::GateSetup(race));
    };
    auto assert_placeholder = [] {
        // Safety remains enabled even when the peer belongs to a different loaded race.
        assert(G<uint8_t>(remote + 4) == 1 && G<uint8_t>(remote + 6) == 1);
        assert(R32(remote + 0x18) == 0 && G<uint16_t>(remote + 0x34) == 0xFFFF);
        assert(R32(race + 0x1250) == 1 && R32(race + 0x1258) == 2);
        assert(R32(setup + 0x4C) == 2 && R32(setup + 0x154) == 0xFFFFFFFF);
    };
    auto assert_no_credit = [] {
        assert(!G<uint8_t>(local + 0x5D) && R32(data + 0xACC) == 1000);
        assert(!rr::g_local_finished && !rr::g_out_won);
        assert(rr::g_gate == rr::GatePhase::Off && rr::g_gate_race == 0);
        // Buffered peer terminal data must not have overwritten the loaded race's progress.
        assert(G<float>(remote + 0x10) == 33 && G<float>(remote + 0x3C) == 44 && R32(remote + 0x44) == 55);
    };
    auto seed_rank = [] {
        G<float>(remote + 0x10) = 33; G<float>(remote + 0x3C) = 44; W32(remote + 0x44, 55);
    };

    // A new announcement can arrive while this copy is still inside a different race. The filenames share
    // their first 50 bytes, so only the complete database identity detects the mismatch.
    for (bool claimed_started : {false, true}) {
        wt::Fixture(); wt::SetConfig(); seed_rank();
        const auto cfg = cc::g_remote[1].cfg;
        rr::Start start{}; start.serial = 501; start.city = 0; start.race_idx = 7;
        rr::OnStart(start);
        rr::g_start_done = claimed_started;
        rr::g_done_serial = claimed_started ? start.serial : 401;
        G<uint8_t>(setup + 0x68 + 50) ^= 1;
        gate_dependencies();
        rr::OnPartnerWon(start.serial, 800); rr::OnPartnerFinished(terminal(start.serial));
        assert(rr::g_race_serial == 401 && !rr::g_partner_won && !rr::g_peer_finished); // pending serial is not active
        cc::DriveWorld(1, wt::Pose(999), test_now, rr::World{501, 7, 0, 2});
        const float x = wt::RemoteX();
        rr::Tick(false, true); cc::Tick(test_now);
        assert_placeholder(); assert_no_credit();
        assert(!rr::ValidWorld(rr::Context()) && wt::RemoteX() == x);
        assert(cc::g_remote[1].samples.empty() && cc::g_remote[1].have_cfg && cc::g_remote[1].cfg == cfg);
        for (int i = 0; i < 3; ++i) rr::Tick(false, true);
        assert_placeholder(); assert_no_credit(); // neither repeated polls nor duplicate receipts count twice

        // Simulate the old local race reaching its result screen with its own already processed base payment.
        W32(machine + 4, 7); G<uint8_t>(local + 4) = 1;
        G<uint8_t>(race + 0x0E) = 1; W32(rewards + 0xBC, 200);
        rr::Tick(false, true);
        assert_no_credit(); assert(R32(rewards + 0xBC) == 200);
        rr::Finished sent{}; assert(!rr::TakeFinished(sent));
    }

    // A pending next start must preserve the active race's finish gate, serial and already received prize.
    wt::Fixture(); seed_rank(); gate_dependencies();
    rr::Tick(false, true);
    const auto old_gate = rr::g_gate;
    const auto old_gate_race = rr::g_gate_race;
    assert(old_gate == rr::GatePhase::Guarding);
    rr::OnPartnerWon(401, 700);
    rr::Start pending{}; pending.serial = 502; pending.city = 0; pending.race_idx = 7;
    const auto before_start = ram;
    rr::OnStart(pending);
    assert(ram == before_start && rr::g_race_serial == 401 && rr::g_active_start.serial == 401);
    assert(rr::g_gate == old_gate && rr::g_gate_race == old_gate_race && rr::g_partner_prize == 700);
    rr::OnPartnerWon(pending.serial, 800); rr::OnPartnerFinished(terminal(pending.serial));
    assert(rr::g_partner_prize == 700 && !rr::g_peer_finished && rr::g_race_serial == 401);
    rr::Setup queued{502, 2, 0xFFFFFF1F, 100, 0, 0, 7, {}};
    rr::OnSetup(queued);
    assert(rr::g_pending_setup.serial == 502 && !rr::g_have_setup);
    auto stale = queued; stale.revision = 1; stale.opponent_mask = 0;
    rr::OnSetup(stale); assert(rr::g_pending_setup.revision == 2);

    // Simulate consuming the pending start at a subsequent street transition, then the native load.
    // Activation, not receipt, resets the old gate and consumes the separately buffered setup.
    rr::ActivateStart(pending, false);
    assert(rr::g_race_serial == 502 && !rr::g_partner_won && rr::g_partner_prize == 0 &&
           rr::g_gate == rr::GatePhase::Off && rr::g_setup_in.revision == 2 && rr::g_have_setup &&
           rr::g_pending_setup.serial == 0);
    rr::g_active_ready = true;
    rr::g_start_done = true; rr::g_done_serial = pending.serial; rr::g_bound_race = race;
    rr::g_gate_requested = false; // this branch specifically covers the existing opt-out/legacy credit path
    W32(race + 0x1250, 1); // the new native race's local no-HUD placeholder is already completed
    rr::OnPartnerWon(pending.serial, 800); rr::OnPartnerFinished(terminal(pending.serial));
    rr::Tick(false, true);
    assert(rr::MatchesWorld(rr::Context(), rr::World{502, 7, 0, 2}));
    assert(G<uint8_t>(local + 0x5D) == 1 && G<uint8_t>(remote + 4) == 1 && !G<uint8_t>(remote + 6));
    assert(G<float>(remote + 0x10) == 99 && G<float>(remote + 0x3C) == 123 && R32(remote + 0x44) == 456);
    assert(R32(race + 0x1250) == 1 && R32(data + 0xACC) == 1000);
    NativeLocalFinish(true, 200); rr::Tick(false, true);
    assert(R32(data + 0xACC) == 1600 && R32(rewards + 0xBC) == 800 && G<uint8_t>(career + 0x20) == 1);
    for (int i = 0; i < 3; ++i) {
        rr::OnPartnerWon(pending.serial, 800); rr::OnPartnerFinished(terminal(pending.serial)); rr::Tick(false, true);
    }
    assert(R32(data + 0xACC) == 1600 && R32(rewards + 0xBC) == 800 && R32(race + 0x1250) == 2);

    ::Fixture(); cc::g_world_car = 0; cc::g_world_at = 0; cc::g_grid_done = false;
    test_now = saved_now;
    std::puts("PASS: pending/other-world results cannot alter win, prize, terminal rank, pose or finish gate; matching consumed race retains legacy credit.");
}
