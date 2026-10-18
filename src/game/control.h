#pragma once
// The test control channel: with --coop-control [port] this copy takes plain text commands on 127.0.0.1 (port
// 45500 + its --instance by default) and plays with the pad they give it, its window in front or not. Its own
// controllers are not read then. For tests that drive copies on a hidden desktop; see tools/ctl.py.
#include <cstdint>

#include "kernel/kernel.h"

namespace input {
struct Pad;
}

namespace control {

void Init();                               // the command line; before boot
bool Enabled();
void Tick(X86Context& ctx);                // game thread, each frame: commands that need it
bool Override(int port, input::Pad& pad);  // the pad thread: true when the channel gives the pad
uint32_t Frames();                         // frames presented so far

}  // namespace control
