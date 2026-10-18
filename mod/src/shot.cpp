// "shot": this process's game window as a BMP, by PrintWindow (PW_RENDERFULLCONTENT reads what DWM composes,
// a Vulkan window included), from the channel's thread on the copy's own desktop.
#include <cstdio>
#include <string>
#include <vector>

#include "mod.h"

namespace mod {

namespace {
HWND g_found = nullptr;
BOOL CALLBACK Each(HWND h, LPARAM) {
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    RECT r;
    if (pid == GetCurrentProcessId() && GetWindowRect(h, &r) && r.right - r.left > 200 && r.bottom - r.top > 150) {
        g_found = h;
        return FALSE;
    }
    return TRUE;
}
}  // namespace

HWND GameWindow() {
    g_found = nullptr;
    EnumWindows(Each, 0);
    return g_found;
}

std::string Shot(const std::wstring& path) {
    HWND w = GameWindow();
    if (!w) return "error no window";
    RECT r;
    GetClientRect(w, &r);
    int width = r.right - r.left, height = r.bottom - r.top;
    if (width <= 0 || height <= 0) return "error empty window";
    HDC screen = GetDC(w);
    HDC dc = CreateCompatibleDC(screen);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), width, -height, 1, 32, BI_RGB};
    void* bits = nullptr;
    HBITMAP bmp = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HGDIOBJ old = SelectObject(dc, bmp);
    BOOL ok = PrintWindow(w, dc, 0x1 | 0x2);  // PW_CLIENTONLY | PW_RENDERFULLCONTENT
    std::string result = "error PrintWindow";
    if (ok) {
        FILE* f = _wfopen(path.c_str(), L"wb");
        if (f) {
            uint32_t bytes = uint32_t(width) * height * 4;
            BITMAPFILEHEADER fh{0x4D42, uint32_t(sizeof fh + sizeof bi.bmiHeader + bytes), 0, 0,
                                uint32_t(sizeof fh + sizeof bi.bmiHeader)};
            fwrite(&fh, sizeof fh, 1, f);
            fwrite(&bi.bmiHeader, sizeof bi.bmiHeader, 1, f);
            fwrite(bits, bytes, 1, f);
            fclose(f);
            result = "ok";
        } else {
            result = "error cannot write";
        }
    }
    SelectObject(dc, old);
    DeleteObject(bmp);
    DeleteDC(dc);
    ReleaseDC(w, screen);
    return result;
}

}  // namespace mod
