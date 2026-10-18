// The F10 co-op panel over the runner's D3D12 frame (ui.cpp draws, panel.cpp holds its rows)
#pragma once

struct IDXGISwapChain3;
struct ID3D12CommandQueue;
struct ID3D12Device;

namespace mod::ui {

void Render(IDXGISwapChain3* sc, ID3D12CommandQueue* queue, ID3D12Device* device);  // before each present
bool Open();
void Toggle();
void DrawPanel(bool open);  // panel.cpp: the session's status line, and the panel when open

}  // namespace mod::ui
