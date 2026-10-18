// The last session's log kept: the release log (logs\black.log, black-N.log for another instance) is opened anew at
// each start, and a session that ended on its own (the title leaving through HalReturnToFirmware, which exits with
// no crash dump) would lose the only trace of why as soon as the game is opened again. Before main, while no log is
// open, each one goes to <name>.prev.log. A log another running copy holds stays where it is.
#include <filesystem>
#include <string>
#include <system_error>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace {

namespace fs = std::filesystem;

struct KeepLastLog {
    KeepLastLog() {
        wchar_t exe[MAX_PATH];
        DWORD n = GetModuleFileNameW(nullptr, exe, MAX_PATH);
        if (!n || n >= MAX_PATH) return;
        std::error_code ec;
        fs::path dir = fs::path(exe).parent_path() / "logs";
        if (!fs::is_directory(dir, ec)) return;
        for (const auto& e : fs::directory_iterator(dir, ec)) {
            const fs::path& p = e.path();
            std::wstring stem = p.stem().wstring();
            if (p.extension() != L".log" || stem.rfind(L"mc3", 0) != 0 || stem.find(L".prev") != std::wstring::npos)
                continue;
            fs::path to = dir / (stem + L".prev.log");
            // replaced only if this one can go: a log in use by another copy fails the move and both stay
            MoveFileExW(p.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING);
        }
    }
} g_keep_last_log;

}  // namespace
