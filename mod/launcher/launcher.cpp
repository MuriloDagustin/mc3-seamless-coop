// MC3Coop.exe: launcher Win32 do mod, sem alterar a instalação do xrevive.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace fs = std::filesystem;
namespace {
constexpr auto kTitle = L"MC3 Co-op";
struct Handle {
    HANDLE value = nullptr;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
fs::path ExeDir() {
    std::wstring p(32768, L'\0');
    auto n = GetModuleFileNameW(nullptr, p.data(), DWORD(p.size()));
    if (!n || n == p.size()) throw std::runtime_error("exe path");
    p.resize(n);
    return fs::path(p).parent_path();
}
void Info(const std::wstring& s) { MessageBoxW(nullptr, s.c_str(), kTitle, MB_ICONINFORMATION | MB_OK); }
void Fail(const std::wstring& s) { MessageBoxW(nullptr, s.c_str(), kTitle, MB_ICONERROR | MB_OK); }
std::wstring WinError(DWORD code) {
    wchar_t* p = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t*>(&p), 0, nullptr);
    std::wstring s = L"Erro " + std::to_wstring(code) + (p ? L": " + std::wstring(p) : L".");
    if (p) LocalFree(p);
    return s;
}
std::wstring Env(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return {};
    std::wstring s(n, L'\0');
    DWORD got = GetEnvironmentVariableW(name, s.data(), n);
    if (got >= n) throw std::runtime_error("environment changed");
    s.resize(got);
    return s;
}
std::wstring Quote(const fs::path& p) { return L"\"" + p.wstring() + L"\""; }
std::wstring PsLiteral(const fs::path& p) {
    std::wstring s = L"'";
    for (wchar_t c : p.wstring()) { s += c; if (c == L'\'') s += c; }
    return s + L"'";
}
fs::path SystemExe(const wchar_t* name) {
    wchar_t p[MAX_PATH]{};
    if (!GetSystemDirectoryW(p, MAX_PATH)) throw std::runtime_error("system dir");
    return fs::path(p) / name;
}
fs::path Runner(const fs::path& game) {
    if (game.empty()) return {};
    std::error_code ec;
    fs::path best;
    fs::file_time_type newest = fs::file_time_type::min();
    fs::directory_iterator it(game, ec), end;
    while (!ec && it != end) {
        const auto& e = *it;
        auto name = e.path().filename().wstring();
        // the runner xrevive puts beside the game: .xrevive-<sha>.exe in older releases, "<game name>.exe" since
        // its October 2026 update; a top level .exe of 20 MB or more either way
        bool exe = e.path().extension() == L".exe" && e.is_regular_file(ec) && e.file_size(ec) >= 20u * 1024 * 1024;
        if (exe) {
            auto t = e.last_write_time(ec);
            if (!ec && (best.empty() || t > newest)) best = e.path(), newest = t;
        }
        it.increment(ec);
    }
    return ec ? fs::path{} : best;
}
bool IsGame(const fs::path& game) {
    std::error_code ec;
    return !game.empty() && fs::is_regular_file(game / L"extracted" / L"default.xbe", ec) && !Runner(game).empty();
}
// Migra também o INI antigo (game= na raiz). O Windows só escreve INI Unicode se houver BOM UTF-16.
std::wstring ReadIni(const fs::path& ini) {
    std::ifstream f(ini, std::ios::binary);
    std::string b((std::istreambuf_iterator<char>(f)), {});
    if (b.size() >= 2 && (unsigned char)b[0] == 0xFF && (unsigned char)b[1] == 0xFE) {
        std::wstring s;
        for (size_t i = 2; i + 1 < b.size(); i += 2)
            s += wchar_t((unsigned char)b[i] | ((unsigned char)b[i + 1] << 8));
        return s;
    }
    if (b.empty()) return {};
    if (b.compare(0, 3, "\xEF\xBB\xBF") == 0) b.erase(0, 3);
    UINT cp = CP_UTF8;
    int n = MultiByteToWideChar(cp, MB_ERR_INVALID_CHARS, b.data(), int(b.size()), nullptr, 0);
    if (!n) cp = CP_ACP, n = MultiByteToWideChar(cp, 0, b.data(), int(b.size()), nullptr, 0);
    std::wstring s(n, L'\0');
    MultiByteToWideChar(cp, 0, b.data(), int(b.size()), s.data(), n);
    return s;
}
fs::path Remembered(const fs::path& ini) {
    wchar_t p[32768]{};
    GetPrivateProfileStringW(L"launcher", L"game", L"", p, 32768, ini.c_str());
    if (*p) return p;
    auto s = ReadIni(ini);
    size_t start = 0;
    while (start < s.size()) {
        auto end = s.find_first_of(L"\r\n", start);
        auto line = s.substr(start, end - start);
        if (line.rfind(L"game=", 0) == 0) return line.substr(5);
        start = end == s.npos ? s.size() : end + 1;
    }
    return {};
}
bool IniSet(const fs::path& ini, const wchar_t* key, const std::wstring& value) {
    auto text = ReadIni(ini);
    // Regrava o conteúdo existente em UTF-16 antes de usar a API de perfis.
    {
        std::ofstream f(ini, std::ios::binary | std::ios::trunc);
        const unsigned char bom[]{0xFF, 0xFE};
        f.write(reinterpret_cast<const char*>(bom), 2);
        f.write(reinterpret_cast<const char*>(text.data()), std::streamsize(text.size() * sizeof(wchar_t)));
        if (!f) return false;
    }
    WritePrivateProfileStringW(nullptr, nullptr, nullptr, ini.c_str());
    return WritePrivateProfileStringW(L"launcher", key, value.c_str(), ini.c_str()) != FALSE;
}
fs::path Search() {
    for (wchar_t d = L'C'; d <= L'Z'; d++) {
        std::wstring root = std::wstring(1, d) + L":\\";
        if (GetDriveTypeW(root.c_str()) != DRIVE_FIXED) continue;
        for (const wchar_t* sub : {L"Jogos\\xbox-xrevive", L"Games\\xbox-xrevive", L"xbox-xrevive", L"xrevive",
                                   L"Games\\xrevive", L"Jogos\\xrevive"}) {
            fs::path g = fs::path(root) / sub / L"Midnight Club 3 DUB Edition Remix";
            if (IsGame(g)) return g;
        }
    }
    return {};
}
fs::path Ask() {
    Info(L"Escolha a pasta Midnight Club 3 DUB Edition Remix criada pelo xrevive.\n\n"
         L"Selecione a pasta que contém extracted e SaveData, sem entrar nessas subpastas.");
    BROWSEINFOW bi{};
    bi.lpszTitle = L"Selecione a pasta do Midnight Club 3 no xrevive";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    auto id = SHBrowseForFolderW(&bi);
    if (!id) return {};
    wchar_t p[MAX_PATH]{};
    bool ok = SHGetPathFromIDListW(id, p);
    CoTaskMemFree(id);
    return ok ? fs::path(p) : fs::path{};
}
bool EqualI(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) == 0; }
bool RunnerPath(const fs::path& p) {
    auto leaf = p.filename().wstring();
    CharLowerBuffW(leaf.data(), DWORD(leaf.size()));
    // A decisão usa o caminho obtido do processo, nunca só o nome do snapshot.
    if (EqualI(leaf, L"xrevive.exe") || EqualI(leaf, L"MC3Runner.exe") ||
        EqualI(leaf, L"Midnight Club 3 DUB Edition Remix.exe")) return true;
    if (leaf.rfind(L".xrevive-", 0) == 0 && EqualI(p.extension().wstring(), L".exe")) return true;
    std::wstring full = p.wstring();
    CharLowerBuffW(full.data(), DWORD(full.size()));
    return EqualI(leaf, L"runner.exe") && full.find(L"\\com.teraflopping.xrevive\\runs\\") != full.npos;
}
bool CheckProcesses() {
    Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snap.value == INVALID_HANDLE_VALUE) {
        Fail(L"Não foi possível verificar os programas abertos.\n" + WinError(GetLastError()) +
             L"\nTente abrir o MC3Coop novamente.");
        return false;
    }
    PROCESSENTRY32W e{}; e.dwSize = sizeof e;
    if (!Process32FirstW(snap.value, &e)) {
        Fail(L"Não foi possível listar os programas abertos. Tente abrir o MC3Coop novamente.");
        return false;
    }
    do {
        if (e.th32ProcessID == GetCurrentProcessId()) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, e.th32ProcessID)};
        wchar_t p[32768]{};
        DWORD n = 32768;
        if (process.value && QueryFullProcessImageNameW(process.value, 0, p, &n)) {
            if (!RunnerPath(p)) continue;
            Info(L"O xrevive ou um runner já está aberto:\n" + std::wstring(p, n) +
                 L"\n\nFeche esse programa manualmente e abra o MC3Coop novamente. Só uma cópia deve ficar aberta.");
            return false;
        }
        // Se um candidato não puder ser consultado, não arrisca abrir uma segunda cópia.
        if (RunnerPath(e.szExeFile) || EqualI(e.szExeFile, L"runner.exe")) {
            Info(L"Há um possível runner aberto, mas o Windows não permitiu consultar seu caminho.\n\n"
                 L"Verifique o xrevive e o MC3Runner no Gerenciador de Tarefas, feche-os manualmente e tente novamente.");
            return false;
        }
    } while (Process32NextW(snap.value, &e));
    return true;
}
void OfferFirewall(const fs::path& ini, const fs::path& local, bool retry) {
    if (!retry && GetPrivateProfileIntW(L"launcher", L"firewallAsked", 0, ini.c_str())) return;
    int answer = MessageBoxW(nullptr,
        L"Deseja permitir conexões de entrada UDP para o MC3Runner.exe no Firewall do Windows?\n\n"
        L"Isso ajuda o co-op a receber conexões. A regra vale só para este executável, em redes privadas. "
        L"O Windows pedirá permissão de administrador apenas para criar a regra.\n\n"
        L"Você pode escolher Não e jogar. Para tentar depois, use MC3Coop.exe --firewall.",
        kTitle, MB_ICONQUESTION | MB_YESNO | MB_DEFBUTTON2);
    if (answer != IDYES && answer != IDNO) return;
    if (!IniSet(ini, L"firewallAsked", L"1"))
        Info(L"Não foi possível salvar sua escolha de firewall. Extraia o zip numa pasta em que você possa gravar arquivos.");
    if (answer != IDYES) return;
    auto netsh = SystemExe(L"netsh.exe");
    std::wstring args = L"advfirewall firewall add rule name=\"MC3 Co-op UDP\" dir=in action=allow program=" +
                        Quote(local) + L" enable=yes protocol=UDP profile=private";
    SHELLEXECUTEINFOW se{}; se.cbSize = sizeof se;
    se.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    se.lpVerb = L"runas";
    se.lpFile = netsh.c_str();
    se.lpParameters = args.c_str();
    se.nShow = SW_HIDE;
    if (!ShellExecuteExW(&se)) {
        auto error = GetLastError();
        Info((error == ERROR_CANCELLED ? std::wstring(L"A permissão de administrador foi cancelada.") :
              L"Não foi possível criar a regra.\n" + WinError(error)) +
             L"\nVocê pode continuar. Para tentar depois, use MC3Coop.exe --firewall.");
        return;
    }
    Handle process{se.hProcess};
    DWORD code = 1;
    if (WaitForSingleObject(process.value, INFINITE) != WAIT_OBJECT_0 ||
        !GetExitCodeProcess(process.value, &code) || code != 0)
        Info(L"O Windows não confirmou a criação da regra UDP.\nUse MC3Coop.exe --firewall para tentar novamente "
             L"ou permita o MC3Runner.exe nas configurações de firewall.");
}
std::wstring Base64(const std::wstring& text) {
    constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    const auto* bytes = reinterpret_cast<const unsigned char*>(text.data());
    size_t n = text.size() * sizeof(wchar_t);
    std::wstring s;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = unsigned(bytes[i]) << 16;
        if (i + 1 < n) v |= unsigned(bytes[i + 1]) << 8;
        if (i + 2 < n) v |= bytes[i + 2];
        s += alphabet[(v >> 18) & 63]; s += alphabet[(v >> 12) & 63];
        s += i + 1 < n ? alphabet[(v >> 6) & 63] : '=';
        s += i + 2 < n ? alphabet[v & 63] : '=';
    }
    return s;
}
int CollectLogs(const fs::path& dir) {
    PWSTR desktopRaw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_Desktop, 0, nullptr, &desktopRaw))) {
        Fail(L"Não foi possível localizar sua Área de Trabalho. Verifique o acesso a essa pasta e tente --logs novamente.");
        return 1;
    }
    fs::path desktop(desktopRaw);
    CoTaskMemFree(desktopRaw);
    SYSTEMTIME t{}; GetLocalTime(&t);
    wchar_t name[128]{};
    swprintf_s(name, L"MC3Coop-logs-%04u%02u%02u-%02u%02u%02u-%lu.zip", t.wYear, t.wMonth, t.wDay,
               t.wHour, t.wMinute, t.wSecond, GetCurrentProcessId());
    auto zip = desktop / name;
    // ZipArchive lê arquivos abertos com compartilhamento; não inclui TOML, saves ou executáveis.
    // Ignora reparse points para não sair da árvore logs através de links/junctions.
    std::wstring script = L"$ErrorActionPreference='Stop'; Add-Type -AssemblyName System.IO.Compression; "
        L"$zip=" + PsLiteral(zip) + L"; $root=" + PsLiteral(dir / L"logs") + L"; "
        L"$extra=" + PsLiteral(dir / L"Log_File_For_Tera.log") + L"; "
        LR"PS(
$stream=$null; $archive=$null; $count=0; $skipped=0; $created=$false
function Add-Log($path,$entry) {
    $inputStream=$null; $outputStream=$null
    try {
        $inputStream=[IO.File]::Open($path,[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        $item=$archive.CreateEntry($entry); $outputStream=$item.Open()
        $inputStream.CopyTo($outputStream); $script:count++
    } catch { $script:skipped++ } finally {
        if($outputStream){$outputStream.Dispose()}; if($inputStream){$inputStream.Dispose()}
    }
}
function Walk-Logs($folder,$prefix) {
    foreach($item in Get-ChildItem -LiteralPath $folder -Force) {
        if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){continue}
        if($item.PSIsContainer){Walk-Logs $item.FullName ($prefix+$item.Name+'/')}
        elseif($item.Extension -in '.log','.txt','.json','.dmp') {Add-Log $item.FullName ($prefix+$item.Name)}
    }
}
try {
    $stream=[IO.File]::Open($zip,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None); $created=$true
    $archive=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create,$true)
    if((Test-Path -LiteralPath $root -PathType Container) -and
       !((Get-Item -LiteralPath $root -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){Walk-Logs $root 'logs/'}
    if((Test-Path -LiteralPath $extra -PathType Leaf) -and
       !((Get-Item -LiteralPath $extra -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){Add-Log $extra 'runner/Log_File_For_Tera.log'}
    $entry=$archive.CreateEntry('LEIA-ME.txt'); $writer=[IO.StreamWriter]::new($entry.Open())
    $writer.WriteLine("Arquivos coletados: $count. Arquivos indisponíveis: $skipped.")
    $writer.WriteLine('Inclui logs do mod e do runner. Não inclui coop-online.toml nem saves.')
    if(!$count){$writer.WriteLine('Nenhum log encontrado. Abra o jogo pelo MC3Coop antes de coletar novamente.')}
    $writer.Dispose()
    $archive.Dispose(); $archive=$null; $stream.Dispose(); $stream=$null
    if($skipped){exit 2}; if(!$count){exit 3}; exit 0
} catch {
    if($archive){$archive.Dispose()}; if($stream){$stream.Dispose()}
    if($created){Remove-Item -LiteralPath $zip -Force -ErrorAction SilentlyContinue}; exit 1
}
)PS";
    auto powershell = SystemExe(L"WindowsPowerShell\\v1.0\\powershell.exe");
    std::wstring cmd = Quote(powershell) + L" -NoLogo -NoProfile -NonInteractive -EncodedCommand " + Base64(script);
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(powershell.c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        dir.c_str(), &si, &pi)) {
        Fail(L"Não foi possível coletar os logs.\n" + WinError(GetLastError()) +
             L"\nEnvie manualmente a pasta logs ao lado do MC3Coop.exe."); return 1;
    }
    Handle process{pi.hProcess}, thread{pi.hThread};
    DWORD code = 1;
    if (WaitForSingleObject(process.value, INFINITE) != WAIT_OBJECT_0 || !GetExitCodeProcess(process.value, &code) ||
        (code != 0 && code != 2 && code != 3)) {
        Fail(L"Não foi possível criar o zip de logs na Área de Trabalho.\nVerifique espaço e permissão de gravação "
             L"e tente novamente. Você também pode enviar a pasta logs manualmente."); return 1;
    }
    Info((code == 3 ? L"Zip criado, mas nenhum log foi encontrado. Abra o jogo pelo MC3Coop e colete novamente:\n\n" :
          code == 2 ? L"Zip criado com parte dos logs. Alguns arquivos estavam indisponíveis:\n\n" :
          L"Logs reunidos. Envie este arquivo para quem está ajudando:\n\n") + zip.wstring());
    return 0;
}
int Launch(const std::vector<std::wstring>& args) {
    auto dir = ExeDir();
    if (args.size() == 1 && args[0] == L"--logs") return CollectLogs(dir);
    bool firewallOnly = args.size() == 1 && args[0] == L"--firewall";
    if (!args.empty() && !firewallOnly) {
        Fail(L"Opção desconhecida.\nAbra MC3Coop.exe para jogar, MC3Coop.exe --logs para coletar logs "
             L"ou MC3Coop.exe --firewall para configurar o firewall."); return 1;
    }
    Handle mutex{CreateMutexW(nullptr, FALSE, L"Local\\MC3Coop.Launcher")};
    DWORD lockResult = mutex.value ? WaitForSingleObject(mutex.value, 0) : WAIT_FAILED;
    if (lockResult != WAIT_OBJECT_0 && lockResult != WAIT_ABANDONED) {
        Info(L"O MC3Coop já está preparando uma execução. Aguarde e confira a janela do lançador."); return 1;
    }
    struct Unlock { HANDLE h; ~Unlock() { ReleaseMutex(h); } } unlock{mutex.value};
    if (!CheckProcesses()) return 1;
    std::error_code ec;
    auto ini = dir / L"MC3Coop.ini", local = dir / L"MC3Runner.exe";
    if (firewallOnly) {
        if (!fs::is_regular_file(local, ec)) {
            Fail(L"MC3Runner.exe ainda não existe nesta pasta.\nAbra o MC3Coop normalmente uma vez para preparar o runner, "
                 L"depois tente --firewall."); return 1;
        }
        OfferFirewall(ini, local, true); return 0;
    }
    if (!fs::is_regular_file(dir / L"xinput1_4.dll", ec)) {
        Fail(L"Falta xinput1_4.dll ao lado do MC3Coop.exe.\nExtraia o zip inteiro numa pasta e abra o lançador dessa pasta.");
        return 1;
    }
    fs::path game;
    auto forced = Env(L"MC3COOP_GAME");
    if (!forced.empty()) {
        if (!IsGame(forced)) {
            Fail(L"MC3COOP_GAME não aponta para uma instalação válida do xrevive:\n" + forced +
                 L"\n\nCorrija essa variável para a pasta que contém extracted\\default.xbe e o executável do jogo criado pelo xrevive, ou remova-a.");
            return 1;
        }
        game = forced;
    } else {
        game = Remembered(ini);
        if (!IsGame(game)) game = Search();
    }
    while (!IsGame(game)) {
        game = Ask(); if (game.empty()) return 1;
        if (!IsGame(game)) Fail(L"Essa pasta não contém extracted\\default.xbe e o executável do jogo criado pelo xrevive.\n"
                                L"Escolha a pasta principal do jogo instalado pelo xrevive.");
    }
    if (forced.empty() && !IniSet(ini, L"game", game.wstring()))
        Info(L"Não foi possível lembrar a pasta do jogo.\nExtraia o zip numa pasta em que você tenha permissão de gravação.");
    auto runner = Runner(game);
    // Verifica antes de sobrescrever o runner e novamente imediatamente antes de iniciar.
    if (!fs::exists(local) || fs::file_size(local) != fs::file_size(runner) ||
        fs::last_write_time(local) < fs::last_write_time(runner)) {
        if (!fs::copy_file(runner, local, fs::copy_options::overwrite_existing, ec)) {
            Fail(L"Não foi possível copiar o runner para a pasta do mod.\n" + local.wstring() +
                 L"\n\nExtraia o zip numa pasta gravável, confira espaço livre e tente novamente."); return 1;
        }
    }
    OfferFirewall(ini, local, false);
    if (!fs::is_regular_file(dir / L"coop-online.toml", ec) &&
        !GetPrivateProfileIntW(L"launcher", L"missingOnlineExplained", 0, ini.c_str())) {
        Info(L"coop-online.toml não está nesta pasta. Você pode jogar co-op por IP usando Host e Join IP no painel F10.\n\n"
             L"Para usar salas por código, extraia também esse arquivo do pacote recebido.");
        IniSet(ini, L"missingOnlineExplained", L"1");
    }
    auto extracted = game / L"extracted", saves = game / L"SaveData" / L"Midnight Club 3_ DUB edition";
    fs::create_directories(saves, ec);
    if (ec) { Fail(L"Não foi possível preparar a pasta de saves.\nVerifique a permissão de gravação na pasta do jogo."); return 1; }
    fs::create_directories(dir / L"logs" / L"runner", ec);
    if (ec) { Fail(L"Não foi possível criar a pasta de logs.\nExtraia o zip numa pasta em que você possa gravar arquivos."); return 1; }
    SetEnvironmentVariableW(L"XRECOMP_LOG_DIR", (dir / L"logs" / L"runner").c_str());
    SetEnvironmentVariableW(L"XRECOMP_GPU_BACKEND", L"d3d12");
    // O modo SDL nativo do runner desvia do gancho XInput usado pela sincronização.
    SetEnvironmentVariableW(L"XRECOMP_NATIVE_CONTROLLERS", L"0");
    // Preserva o teclado por padrão e respeita a desativação explícita pelo usuário.
    if (Env(L"XRECOMP_KEYBOARD").empty()) SetEnvironmentVariableW(L"XRECOMP_KEYBOARD", L"1");
    if (!CheckProcesses()) return 1;
    std::wstring cmd = Quote(local) + L" --xbe " + Quote(extracted / L"default.xbe") + L" --game-dir " + Quote(extracted) +
                       L" --save-name " + Quote(saves);
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (!CreateProcessW(local.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, dir.c_str(), &si, &pi)) {
        Fail(L"Não foi possível iniciar o jogo.\n" + WinError(GetLastError()) +
             L"\nExtraia o pacote inteiro novamente. Se persistir, use MC3Coop.exe --logs e envie o zip."); return 1;
    }
    Handle process{pi.hProcess}, thread{pi.hThread};
    return 0;
}
} // namespace
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int result = 1;
    try {
        int argc = 0;
        auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
        if (!argv) throw std::runtime_error("command line");
        std::vector<std::wstring> args(argv + 1, argv + argc);
        LocalFree(argv);
        result = Launch(args);
    } catch (const std::exception&) {
        Fail(L"O lançador não conseguiu acessar os arquivos necessários.\nExtraia o zip inteiro numa pasta gravável "
             L"e tente novamente. Para pedir ajuda, use MC3Coop.exe --logs.");
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return result;
}
