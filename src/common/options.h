// MC3's own settings, kept in mc3.toml beside the SDK's
#pragma once
#include <cstdint>
#include <string>

namespace mc3 {

struct Options {
    std::string coop_nickname = "Player";
    uint32_t coop_port = 45410;
};
Options& Opt();  // under app::UiLock for writes the F10 panel can see

void DrawUi(double fps);  // menu.cpp: the F10 panel and the co-op line
bool PanelOpen();
void TogglePanel();

}  // namespace mc3
