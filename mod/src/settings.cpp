#include "settings.h"

#include <fstream>
#include <mutex>

#include <toml++/toml.hpp>

#include "mod.h"

namespace mod {

namespace {
std::mutex g_lock;
std::wstring Path() { return ExeDir() + L"\\mc3coop.toml"; }
}  // namespace

Options& Settings() {
    static Options o;
    return o;
}

void LoadSettings() {
    std::lock_guard lock(g_lock);
    try {
        auto t = toml::parse_file(Path());
        Options& o = Settings();
        o.coop_nickname = t["coop_nickname"].value_or(o.coop_nickname);
        o.coop_port = uint32_t(t["coop_port"].value_or(int64_t(o.coop_port)));
        o.join_ip = t["join_ip"].value_or(o.join_ip);
    } catch (...) {
        // no file yet: the defaults
    }
}

void SaveSettings() {
    std::lock_guard lock(g_lock);
    const Options& o = Settings();
    toml::table t{{"coop_nickname", o.coop_nickname}, {"coop_port", int64_t(o.coop_port)}, {"join_ip", o.join_ip}};
    std::ofstream f(Path());
    f << t << "\n";
}

}  // namespace mod
