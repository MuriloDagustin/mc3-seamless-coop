#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <toml++/toml.hpp>

#include "app/app.h"
#include "common/options.h"
#include "core/input.h"
#include "core/log.h"
#include "game/control.h"
#include "game/coop.h"
#include "game/diagnostics.h"
#include "game/mc3.h"

namespace mc3 {

Options& Opt() {
    static Options o;
    return o;
}

namespace {

void Load(const void* table) {
    const auto& t = *static_cast<const toml::table*>(table);
    if (auto v = t["coop_nickname"].value<std::string>()) Opt().coop_nickname = *v;
}

void Save(void* table) {
    auto& t = *static_cast<toml::table*>(table);
    t.insert_or_assign("coop_nickname", Opt().coop_nickname);
}

void Init() {
    game::diagnostics::Init();
    control::Init();
    coop::Init();
}

bool Message(void*, uint32_t msg, uintptr_t wp, intptr_t lp, intptr_t* result) {
    switch (msg) {
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:  // F10 comes as a system key
            if (wp == VK_F10 && !(GetKeyState(VK_CONTROL) & 0x8000)) {
                TogglePanel();
                return true;
            }
            if (wp == VK_F8) {
                coop::RequestRescue();
                return true;
            }
            break;
        case WM_SETCURSOR:
            if (PanelOpen() && LOWORD(static_cast<LPARAM>(lp)) == HTCLIENT) {
                SetCursor(LoadCursorW(nullptr, IDC_ARROW));
                *result = TRUE;
                return true;
            }
            break;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_CLOSE) LOGR("window: closed by Windows (Alt+F4, the title bar, the taskbar)");
            break;
        case WM_CLOSE:
            LOGR("window: close asked");
            break;
        case WM_QUERYENDSESSION:
        case WM_ENDSESSION:
            LOGR("window: Windows is ending the session");
            break;
    }
    return false;
}

// copies on one PC share the controllers: a networked test copy not in front reads none of them
bool Background() {
    return app::Instance() && coop::Networked() && GetForegroundWindow() != static_cast<HWND>(app::Window());
}

void Pad(int port, input::Pad& pad) {
    if (control::Override(port, pad)) return;
    if (Background()) pad = input::Pad{};
}

void Rumble(int, uint16_t& left, uint16_t& right) {
    if (control::Enabled() || Background()) left = right = 0;
}

}  // namespace

}  // namespace mc3

// what the runtime asks of the game; strings must be set, hooks may stay null
const app::GameInfo app::kGame = {
    .title = L"Midnight Club 3: DUB Edition Remix",  // window and splash
    .short_title = L"MC3",                           // message boxes
    .name = L"MC3",                                  // window classes, crash dumps, handover event
    .log = "mc3",                                    // log file prefix
    .settings = "mc3.toml",                          // settings file beside the exe
    .splash = L"splash",                             // optional splash.png or splash.bmp beside the exe
    .missing = L"Put the game in the assets folder: its disc image (.iso), or default.xbe with its folders beside it.",
    .images = {{"default.xbe", "MC3"}},  // each recompiled xbe and its exe, for title launches
    .options = {{"coop_port", nullptr, &mc3::Opt().coop_port}},
    .load = mc3::Load,
    .save = mc3::Save,
    .init = mc3::Init,
    .loaded = mc3::Loaded,
    .window = nullptr,
    .message = mc3::Message,
    .ui = mc3::DrawUi,
    .pad = mc3::Pad,
    .rumble = mc3::Rumble,
};
