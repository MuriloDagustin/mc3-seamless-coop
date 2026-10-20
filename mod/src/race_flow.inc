// Host decisions and native result UI, included inside race.cpp's anonymous namespace.
// All guest writes remain in the verified input hook. No native callback is invoked by this module.
std::array<After, 8> g_after_out{}, g_after_in{};
uint32_t g_after_revision = 0, g_after_cursor = 0;
uint64_t g_after_send_at = 0;
bool g_result_hold = false, g_result_tail = false, g_result_neutral = false;
bool g_result_loading = false, g_result_reset = false;
uint32_t g_result_load_origin = 0, g_result_load_serial = 0;
uint64_t g_result_load_at = 0;
UiPlan g_result_plan{};
uint32_t g_result_plan_serial = 0, g_result_plan_revision = 0;
uint64_t g_result_pulse_at = 0;
uint32_t g_result_confirm_serial = 0, g_result_confirm_revision = 0, g_result_confirm_menu = 0;
uint32_t g_result_confirm_page = 0;
bool g_result_plan_next = false, g_result_plan_tournament = false, g_result_plan_quit = false;

bool ValidAfter(const After& a) {
    if (!a.serial || !a.revision || a.reserved || a.city < 0 || a.city > 3) return false;
    if (a.action == AfterAction::Next) {
        uint32_t vt = 0; uint16_t id = 0; int16_t index = -1;
        std::memcpy(&vt, a.next.record, 4);
        std::memcpy(&id, a.next.record + 4, 2);
        std::memcpy(&index, a.next.record + 6, 2);
        return int32_t(a.next.serial - a.serial) > 0 && a.next.city == a.city &&
               a.next.race_idx >= 0 && a.next.race_idx < 160 && !a.next.pad &&
               vt == kRecordVtable && id && index == a.next.race_idx &&
               int8_t(a.next.record[8]) == a.city && std::memchr(a.next.name, 0, sizeof a.next.name);
    }
    Start zero{};
    return (a.action == AfterAction::Wait || a.action == AfterAction::Street) &&
           !std::memcmp(&a.next, &zero, sizeof zero);
}

void PublishAfter(uint32_t serial, AfterAction action, int city, const Start* next = nullptr) {
    After a{};
    a.serial = serial; a.revision = ++g_after_revision;
    if (!a.revision) a.revision = ++g_after_revision;
    a.action = action; a.city = int8_t(city);
    if (next) a.next = *next;
    if (!ValidAfter(a)) return;
    g_after_out[serial % g_after_out.size()] = a;
    g_after_cursor = serial % uint32_t(g_after_out.size());
    g_after_send_at = 0;
}

const After* DecisionFor(uint32_t serial) {
    const After& a = g_after_in[serial % g_after_in.size()];
    return a.serial == serial ? &a : nullptr;
}

bool HostControls(uint32_t serial) {
    if (serial && g_active_start.serial == serial) return true; // receiving Wait is not a prerequisite for authority
    if (DecisionFor(serial)) return true;
    for (const After& a : g_after_in)
        if (a.action == AfterAction::Next && a.next.serial == serial) return true;
    return false;
}

void ResetFlowNetwork() {
    g_after_out = g_after_in = {};
    g_after_revision = g_after_cursor = 0; g_after_send_at = 0;
    g_result_pulse_at = 0;
    g_result_hold = g_result_tail = g_result_neutral = g_result_loading = false;
    g_result_load_origin = g_result_load_serial = 0;
    g_result_load_at = 0;
    g_result_plan = {};
    g_result_confirm_serial = g_result_confirm_revision = g_result_confirm_menu = g_result_confirm_page = 0;
    g_result_reset = true;
}

void PublishHostWaiting(const Start& start) {
    PublishAfter(start.serial, AfterAction::Wait, start.city);
}

void ObserveHostExit() {
    if (!g_active_start.serial || !g_active_ready || !g_bound_ran ||
        (P(kMode) == 2 && !Cruise())) return;
    After& old = g_after_out[g_active_start.serial % g_after_out.size()];
    if (old.serial == g_active_start.serial && old.action == AfterAction::Street) return;
    PublishAfter(g_active_start.serial, AfterAction::Street, g_active_start.city);
}

bool PublishHostSeries() {
    Start next{};
    if (P(kMode) != 2 || RaceState() < 2 || RaceState() > 6 || !ResolveHostLoadedStart(next)) return false;
    uint32_t previous = g_race_serial;
    next.serial = ++g_serial;
    if (!next.serial) next.serial = ++g_serial;
    g_start_out = next;
    g_out_start = false; // the repeated decision carries the start and its parent serial atomically
    ActivateStart(next, true);
    PublishAfter(previous, AfterAction::Next, next.city, &next);
    CaptureHostSetup();
    Log("race: host next stage serial %u -> %u (%s)", previous, next.serial, next.name);
    return true;
}

bool ResultContext() {
    uint32_t setup = P(kSetupCurrent), race = P(P(kRaceHolder) + 4);
    return P(kMode) == 2 && !Cruise() && g_race_serial && g_bound_race == race &&
           race && g_active_ready && MatchesSharedRace() && Mapped(setup, 0x109) &&
           !G<uint8_t>(setup + 0x108) && P(setup + 0x18) >= 4 &&
           RaceState() >= 7 && RaceState() <= 12;
}

void QueueResultPulse(const UiPlan& plan, const After& a, bool tournament, uint64_t now, bool quit = false) {
    if (g_result_plan.button || (g_result_pulse_at && now - g_result_pulse_at < 160)) return;
    if (plan.button == 0x1000 && !tournament && g_result_confirm_serial == a.serial &&
        g_result_confirm_revision == a.revision && g_result_confirm_menu == plan.menu &&
        g_result_confirm_page == plan.page) return; // one confirmation for this exact native page/decision
    g_result_plan = plan;
    g_result_plan_serial = a.serial; g_result_plan_revision = a.revision;
    g_result_plan_next = a.action == AfterAction::Next && plan.page == 5 && !quit;
    g_result_plan_quit = quit;
    g_result_plan_tournament = tournament;
}

void ResultFlowTick(bool hosting, bool connected, uint64_t now) {
    if (g_result_reset || !connected || hosting) {
        g_result_plan = {};
        g_result_hold = g_result_loading = g_result_tail = g_result_neutral = false;
        g_result_reset = false;
        return;
    }
    if (g_result_loading) {
        uint32_t loaded = P(P(kRaceHolder) + 4);
        if (P(kMode) == 2 && loaded && !Cruise() && RaceState() >= 2 && RaceState() <= 6 &&
            g_active_start.serial == g_result_load_serial &&
            SetupMatches(P(kSetupCurrent), g_active_start.city, g_active_start.race_idx)) {
            g_active_ready = g_start_done = true;
            g_done_serial = g_active_start.serial;
            g_result_loading = false;
        } else {
            g_result_hold = true;
            g_notice = now - g_result_load_at > 10000 ?
                "A próxima largada não foi confirmada. Use o F10 para desconectar se precisar sair." :
                "Aguardando carregar a próxima corrida do host.";
            return;
        }
    }
    bool hold = ResultContext() && HostControls(g_race_serial);
    if (!hold) {
        if (g_result_hold) g_result_tail = true;
        g_result_hold = false; g_result_plan = {};
        return;
    }
    g_result_hold = true;
    g_notice = "Aguardando o host escolher o que fazer.";
    const After* a = DecisionFor(g_race_serial);
    if (!a || a->action == AfterAction::Wait) return;
    g_notice = a->action == AfterAction::Street ?
        "O host voltou para a rua. Aguardando a tela de saída do jogo." :
        "O host avançou. Preparando a próxima corrida.";
    bool quit = a->action == AfterAction::Street;
    if (a->action == AfterAction::Next) {
        if (g_have_start && int32_t(g_start_in.serial - a->next.serial) > 0) return;
        if (!g_have_start || g_start_in.serial != a->next.serial) {
            // This is a pending authorization, not a replacement for the still-active finish gate/result.
            g_start_in = a->next; g_have_start = true; g_start_done = false; g_pending_setup = {};
        }
        // Different career/cursor: quit through the real native action, then consume the retained Start
        // from the street. Never force a local tournament to use a fabricated cursor or task pointer.
        quit = !NativeNextMatches(a->next);
    }
    if (RaceState() == 9 && (CanAdvanceTournamentResult() || CanDismissTournamentOverlay())) {
        UiPlan plan{}; plan.button = 0x1000; plan.page = 9;
        QueueResultPulse(plan, *a, true, now);
        return;
    }
    if (RaceState() != 12) return; // rewards10 and autosave11 complete natively
    if (a->action == AfterAction::Next && g_pending_setup.serial != a->next.serial) return;
    UiPlan plan{};
    if (PlanResultMenu(quit, plan)) QueueResultPulse(plan, *a, false, now, quit);
}

bool FilterResultInput(uint16_t& buttons) {
    if (!g_result_hold) {
        if (g_result_tail && buttons) { buttons = 0; return true; }
        g_result_tail = false;
        return false;
    }
    buttons = 0;
    const After* a = DecisionFor(g_result_plan_serial);
    if (!g_result_plan.button || !a || a->revision != g_result_plan_revision) {
        g_result_plan = {};
        g_result_neutral = true;
        return true;
    }
    if (!g_result_neutral) {
        g_result_neutral = true;
        return true;
    }
    UiPlan actual{};
    bool valid = g_result_plan_tournament ? (CanAdvanceTournamentResult() || CanDismissTournamentOverlay()) :
        PlanResultMenu(g_result_plan_quit, actual) &&
        actual.menu == g_result_plan.menu && actual.page == g_result_plan.page && actual.button == g_result_plan.button;
    if (valid && g_result_plan_next && g_result_plan.button == 0x1000)
        valid = g_pending_setup.serial == a->next.serial && NativeNextMatches(a->next);
    if (valid) {
        buttons = g_result_plan.button;
        g_result_neutral = false;
        g_result_pulse_at = GetTickCount64();
        if (buttons == 0x1000 && !g_result_plan_tournament) {
            g_result_confirm_serial = a->serial; g_result_confirm_revision = a->revision;
            g_result_confirm_menu = actual.menu; g_result_confirm_page = actual.page;
            if (g_result_plan_next) {
                g_result_load_origin = P(P(kRaceHolder) + 4);
                g_result_load_serial = a->next.serial;
                g_result_load_at = GetTickCount64();
                ActivateStart(a->next, false);
                g_result_loading = true;
            }
        }
    }
    g_result_plan = {};
    return true;
}
