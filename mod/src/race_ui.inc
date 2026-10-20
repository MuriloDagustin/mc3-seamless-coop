// Read-only plans for existing native result menus. Included inside race.cpp's
// anonymous namespace. Never call a guest callback or manufacture menu entries.
// Evidence: docs/RE-HOST-SAIDA-UI.md.
struct UiPlan {
    uint32_t menu = 0, page = 0;
    uint16_t button = 0;
    bool confirm = false;
};

struct ResultUiList {
    uint32_t object = 0, bank = 0, count = 0, selected = 0, items = 0;
};

bool ReadResultUiList(uint32_t menu, uint32_t vtable, ResultUiList& out) {
    if (!Ptr(menu) || !Mapped(menu, 0x35C) || P(menu) != vtable ||
        !(G<uint8_t>(menu + 0x44) & 1) || P(menu + 0x300) != 0 ||
        G<uint8_t>(menu + 0x358)) return false;
    uint32_t bank = P(menu + 0x25C), cap = P(menu + 0x230);
    // Three live banks, followed by their saved selection states. Callback
    // descriptors are only available for the first twenty item indices.
    if (bank > 2 || cap == 0 || cap > 20) return false;
    uint32_t count = P(menu + 0x238 + bank * 4);
    uint32_t selection = menu + 0x268 + bank * 16;
    uint32_t selected = P(selection), buffer = P(menu + 0x248);
    if (!count || count > cap || selected >= count ||
        G<uint8_t>(selection + 4) != 1 || !Ptr(buffer) ||
        !Mapped(buffer, (bank + 1) * cap * 16)) return false;
    uint32_t items = buffer + bank * cap * 16;
    uint32_t entry = items + selected * 16;
    if (P(selection + 8) != P(entry) || P(selection + 12) != P(entry + 4)) return false;
    out = {menu, bank, count, selected, items};
    return true;
}

bool PlanResultMenu(bool quitToStreet, UiPlan& out) {
    out = {};
    if (P(kMode) != 2 || RaceState() < 7 || RaceState() > 12 || Cruise()) return false;
    uint32_t app = P(0x50F148), menus = P(app + 0x0C), frontend = P(menus + 0x0C);
    if (!Ptr(app) || !Ptr(menus) || !Ptr(frontend) || !Mapped(frontend, 0x228)) return false;
    uint32_t page = P(frontend + 0xD8);
    ResultUiList list{};
    uint32_t target = 0xFFFFFFFF;
    if (page == 5) {
        // Native 16A460 accepts input offline only with E0==1 and the game paused.
        uint32_t game = P(kGameState);
        if (P(frontend + 0xE0) != 1 || !Ptr(game) || !Mapped(game + 9, 1) ||
            !G<uint8_t>(game + 9) || P(frontend + 0xFC) != 0 ||
            !ReadResultUiList(P(menus + 0xFC), 0x45DAF8, list)) return false;
        uint32_t context = quitToStreet ? frontend : P(menus + 0x2C);
        if (!Ptr(context)) return false;
        for (uint32_t i = 0; i < list.count; ++i) {
            uint32_t item = list.items + i * 16, callback = list.object + 0xEC + i * 16;
            // Flag bit 0 is disabled; exactly 2 is a left/right-only action.
            // Accept the plain native action (flags 0) only.
            if (P(item + 12) != 0 || P(callback) != context ||
                P(callback + 4) != (quitToStreet ? 0x1642C0u : 0x16B7F0u) ||
                P(callback + 8) != (quitToStreet ? 0x1Au : 0u) ||
                P(callback + 12) != (quitToStreet ? 2u : 1u)) continue;
            if (target != 0xFFFFFFFF) return false;
            target = i;
        }
    } else if (page == 6 && quitToStreet && P(frontend + 0xFC) == 0x1A) {
        uint32_t confirmation = P(menus + 0xC0), dialog = P(menus + 0x118);
        if (!Ptr(confirmation) || !Mapped(confirmation, 0x48) ||
            P(confirmation) != 0x45DE48 || !(G<uint8_t>(confirmation + 0x44) & 1) ||
            !Ptr(dialog) || !Mapped(dialog, 0x78) || P(dialog) != 0x45E0D0 ||
            !(G<uint8_t>(dialog + 0x44) & 1) || P(dialog + 0x64) != 0 ||
            !ReadResultUiList(P(P(dialog + 0x60) + 8), 0x45DFA8, list) || list.count != 2)
            return false;
        bool haveNo = false;
        for (uint32_t i = 0; i < list.count; ++i) {
            uint32_t item = list.items + i * 16, key = P(item);
            if (P(item + 4) || P(item + 12) || (key != 4 && key != 8)) return false;
            if (key == 4) {
                if (target != 0xFFFFFFFF) return false;
                target = i;
            } else {
                if (haveNo) return false;
                haveNo = true;
            }
        }
        if (!haveNo) return false;
    } else return false;
    if (target == 0xFFFFFFFF) return false;
    out.menu = list.object;
    out.page = page;
    out.confirm = page == 6;
    out.button = list.selected == target ? 0x1000 : (list.selected > target ? 0x0001 : 0x0002);
    return true;
}

bool CanAdvanceTournamentResult() {
    if (P(kMode) != 2 || RaceState() != 9 || Cruise()) return false;
    uint32_t menus = P(P(0x50F148) + 0x0C), frontend = P(menus + 0x0C);
    if (!Ptr(frontend) || !Mapped(frontend, 0x228) || G<uint8_t>(frontend + 0x222) ||
        P(frontend + 0xD8) != 1 || P(frontend + 0xFC)) return false;
    uint32_t tasks = P(P(P(kCareer) + 0x0C) + 0x10);
    if (!Ptr(tasks) || !Mapped(tasks, 0x98)) return false;
    uint32_t index = P(tasks + 0x94), begin = P(tasks + 0x78), end = P(tasks + 0x7C);
    if (!Ptr(begin) || end < begin || (end - begin) % 12 ||
        (end - begin) / 12 > 1024 || index >= (end - begin) / 12 ||
        !Mapped(begin + index * 12, 12)) return false;
    uint32_t task = P(begin + index * 12 + 8);
    if (!Ptr(task) || !Mapped(task, 0xF4) || P(task) != 0x470348 ||
        !G<uint8_t>(task + 0x0C)) return false;
    uint32_t phase = P(task + 0xEC), widget = P(task + 0xE8), input = P(task + 0xE4);
    // EC==3 already requested Close; waiting for that value would miss the gate.
    if (phase < 1 || phase > 2 || !Ptr(widget) || !Mapped(widget, 0x35C) ||
        P(widget) != 0x46FFF8 || !(G<uint8_t>(widget + 0x44) & 1) ||
        P(widget + 0x300) != 0 || G<uint8_t>(widget + 0x358) ||
        !Ptr(input) || !Mapped(input, 0x148) || (P(input + 0x44) & 0x201) != 0x201)
        return false;
    return true;
}

bool CanDismissTournamentOverlay() {
    if (P(kMode) != 2 || RaceState() != 9 || Cruise()) return false;
    uint32_t menus = P(P(0x50F148) + 0x0C), frontend = P(menus + 0x0C);
    if (!Ptr(frontend) || !Mapped(frontend, 0x228) || G<uint8_t>(frontend + 0x222) != 1 ||
        P(frontend + 0xD8) != 1 || P(frontend + 0xFC)) return false;
    uint32_t tasks = P(P(P(kCareer) + 0x0C) + 0x10);
    if (!Ptr(tasks) || !Mapped(tasks, 0x98)) return false;
    uint32_t index = P(tasks + 0x94), begin = P(tasks + 0x78), end = P(tasks + 0x7C);
    if (!Ptr(begin) || end < begin || (end - begin) % 12 ||
        (end - begin) / 12 > 1024 || index >= (end - begin) / 12 ||
        !Mapped(begin + index * 12, 12)) return false;
    uint32_t task = P(begin + index * 12 + 8);
    if (!Ptr(task) || !Mapped(task, 0xF4) || P(task) != 0x470348 ||
        !G<uint8_t>(task + 0x0C)) return false;
    uint32_t widget = P(task + 0xE8), input = P(task + 0xE4);
    if (!Ptr(widget) || !Mapped(widget, 0x35C) || P(widget) != 0x46FFF8 ||
        !(G<uint8_t>(widget + 0x44) & 1) || P(widget + 0x300) != 0 ||
        G<uint8_t>(widget + 0x358) || !Ptr(input) || !Mapped(input, 0x148) ||
        (P(input + 0x44) & 0x201) != 0x201) return false;
    uint32_t columns = P(widget + 0x58);
    // 266E3C..4F ->264100 writes columns+20, clears F+222 and resets
    // the list. This consumer neither checks nor advances task+EC.
    return Ptr(columns) && Mapped(columns, 0x24);
}
