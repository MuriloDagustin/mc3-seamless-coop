// The mod's own settings, mc3coop.toml beside the runner
#pragma once
#include <cstdint>
#include <string>

namespace mod {

struct Options {
    std::string coop_nickname = "Player";
    uint32_t coop_port = 45410;
    std::string join_ip;
};
Options& Settings();
void LoadSettings();
void SaveSettings();

}  // namespace mod
