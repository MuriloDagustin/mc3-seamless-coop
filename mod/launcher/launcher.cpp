// MC3Coop.exe: starts the xrevive Midnight Club 3 runner with the co-op mod.
// The mod is xinput1_4.dll beside a copy of the runner: the runner (.xrevive-*.exe in the game folder, never changed)
// is copied here as MC3Runner.exe and started with the game's own files and saves, on the D3D12 backend (the F10
// panel draws there). The game folder is remembered in MC3Coop.ini; the first run looks for it or asks.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

const wchar_t* kTitle = L"MC3 Co-op";

fs::path ExeDir() {
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return fs::path(p).parent_path();
}

void Fail(const std::wstring& text) { MessageBoxW(nullptr, text.c_str(), kTitle, MB_ICONERROR | MB_OK); }

// the game folder as xrevive lays it out: extracted\default.xbe, SaveData, and the runner .xrevive-<sha>.exe
fs::path Runner(const fs::path& game) {
    std::error_code ec;
    fs::path best;
    fs::file_time_type newest{};
    for (auto& e : fs::directory_iterator(game, ec)) {
        auto name = e.path().filename().wstring();
        if (name.rfind(L".xrevive-", 0) == 0 && e.path().extension() == L".exe" && e.last_write_time() >= newest)
            best = e.path(), newest = e.last_write_time();
    }
    return best;
}

bool IsGame(const fs::path& game) {
    std::error_code ec;
    return fs::exists(game / L"extracted" / L"default.xbe", ec) && !Runner(game).empty();
}

fs::path Remembered() {
    std::wifstream f(ExeDir() / L"MC3Coop.ini");
    std::wstring line;
    while (std::getline(f, line))
        if (line.rfind(L"game=", 0) == 0) return fs::path(line.substr(5));
    return {};
}

void Remember(const fs::path& game) {
    std::wofstream f(ExeDir() / L"MC3Coop.ini");
    f << L"game=" << game.wstring() << L"\n";
}

fs::path Search() {
    std::vector<fs::path> guesses;
    for (wchar_t d = L'C'; d <= L'Z'; d++) {
        std::wstring root = std::wstring(1, d) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        for (const wchar_t* sub : {L"Jogos\\xbox-xrevive", L"Games\\xbox-xrevive", L"xbox-xrevive", L"xrevive",
                                   L"Games\\xrevive", L"Jogos\\xrevive"})
            guesses.push_back(fs::path(root) / sub / L"Midnight Club 3 DUB Edition Remix");
    }
    for (auto& g : guesses)
        if (IsGame(g)) return g;
    return {};
}

fs::path Ask() {
    MessageBoxW(nullptr,
                L"Choose the Midnight Club 3 DUB Edition Remix folder that xrevive made "
                L"(the one with the 'extracted' and 'SaveData' folders).\n\n"
                L"Escolha a pasta do Midnight Club 3 DUB Edition Remix criada pelo xrevive "
                L"(a que tem as pastas 'extracted' e 'SaveData').",
                kTitle, MB_ICONINFORMATION | MB_OK);
    BROWSEINFOW bi{};
    bi.lpszTitle = L"Midnight Club 3 DUB Edition Remix (xrevive)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST id = SHBrowseForFolderW(&bi);
    if (!id) return {};
    wchar_t p[MAX_PATH];
    bool ok = SHGetPathFromIDListW(id, p);
    CoTaskMemFree(id);
    return ok ? fs::path(p) : fs::path();
}

std::wstring Quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    fs::path dir = ExeDir();
    std::error_code ec;
    fs::path game;
    if (!fs::exists(dir / L"xinput1_4.dll", ec)) {
        Fail(L"xinput1_4.dll (the co-op mod) is missing beside MC3Coop.exe. Extract the whole zip.");
        return 1;
    }
    // MC3COOP_GAME: exactly that folder, never looked for (tests point it at a disposable copy)
    wchar_t forced[MAX_PATH]{};
    if (GetEnvironmentVariableW(L"MC3COOP_GAME", forced, MAX_PATH)) {
        if (!IsGame(forced)) {
            Fail(std::wstring(L"MC3COOP_GAME is not an xrevive game folder: ") + forced);
            return 1;
        }
        game = forced;
    } else {
        game = Remembered();
        if (!IsGame(game)) game = Search();
    }
    while (!IsGame(game)) {
        game = Ask();
        if (game.empty()) return 1;
        if (!IsGame(game))
            Fail(L"That folder has no extracted\\default.xbe and xrevive runner (.xrevive-*.exe).\n"
                 L"Essa pasta nao tem extracted\\default.xbe e o executavel do xrevive.");
    }
    if (!GetEnvironmentVariableW(L"MC3COOP_GAME", nullptr, 0)) Remember(game);
    // the runner, copied beside the mod when it is new or changed
    fs::path runner = Runner(game), local = dir / L"MC3Runner.exe";
    if (!fs::exists(local, ec) || fs::file_size(local, ec) != fs::file_size(runner, ec) ||
        fs::last_write_time(local, ec) < fs::last_write_time(runner, ec)) {
        if (!fs::copy_file(runner, local, fs::copy_options::overwrite_existing, ec)) {
            Fail(L"Could not copy the xrevive runner here (is the game already open?):\n" + runner.wstring());
            return 1;
        }
    }
    fs::path extracted = game / L"extracted";
    fs::path saves = game / L"SaveData" / L"Midnight Club 3_ DUB edition";
    fs::create_directories(saves, ec);
    // the F10 panel is drawn on the D3D12 backend; keyboard play as xrevive offers it
    SetEnvironmentVariableW(L"XRECOMP_GPU_BACKEND", L"d3d12");
    if (!GetEnvironmentVariableW(L"XRECOMP_KEYBOARD", nullptr, 0)) SetEnvironmentVariableW(L"XRECOMP_KEYBOARD", L"1");
    std::wstring cmd = Quote(local) + L" --xbe " + Quote(extracted / L"default.xbe") + L" --game-dir " + Quote(extracted) +
                       L" --save-name " + Quote(saves);
    STARTUPINFOW si{sizeof si};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(local.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        Fail(L"Could not start the game (error " + std::to_wstring(GetLastError()) + L").");
        return 1;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return 0;
}
