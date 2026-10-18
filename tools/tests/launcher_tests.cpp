// Launcher unit harness. Game CreateProcess calls and UAC are intercepted; only system PowerShell runs.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <chrono>
namespace test {
std::filesystem::path dir, desktop;
std::vector<std::wstring> messages;
int answer=IDNO, gameLaunches=0, elevated=0;
bool fakeProcesses=true, blocked=false;
std::wstring forcedOverride;
std::wstring processPath=L"C:\\tests\\MC3Runner.exe", lastArgs;
std::wstring launchedNative, launchedKeyboard;
std::wstring RealEnv(LPCWSTR name) {
 wchar_t value[256]{}; auto n=GetEnvironmentVariableW(name,value,256);assert(n<256);return value;
}
DWORD WINAPI Environment(LPCWSTR name,LPWSTR out,DWORD n) {
 if(std::wstring(name)!=L"MC3COOP_GAME" || forcedOverride.empty())return GetEnvironmentVariableW(name,out,n);
 if(n<=forcedOverride.size())return DWORD(forcedOverride.size()+1);
 wcscpy_s(out,n,forcedOverride.c_str());return DWORD(forcedOverride.size());
}
int WINAPI Message(HWND,LPCWSTR text,LPCWSTR,UINT flags) {
 messages.emplace_back(text); return flags & MB_YESNO ? answer : IDOK;
}
DWORD WINAPI Module(HMODULE,LPWSTR p,DWORD n) {
 auto v=(dir/L"MC3Coop.exe").wstring(); if(v.size()>=n)return n; wcscpy_s(p,n,v.c_str());return DWORD(v.size());
}
HRESULT WINAPI Folder(REFKNOWNFOLDERID,DWORD,HANDLE,PWSTR* out) {
 auto s=desktop.wstring(); *out=(PWSTR)CoTaskMemAlloc((s.size()+1)*2); wcscpy_s(*out,s.size()+1,s.c_str());return S_OK;
}
BOOL WINAPI Create(LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES pa,LPSECURITY_ATTRIBUTES ta,BOOL inherit,
 DWORD flags,LPVOID env,LPCWSTR cwd,LPSTARTUPINFOW si,LPPROCESS_INFORMATION pi) {
 if(std::filesystem::path(app).filename()==L"MC3Runner.exe") {
  launchedNative=RealEnv(L"XRECOMP_NATIVE_CONTROLLERS");launchedKeyboard=RealEnv(L"XRECOMP_KEYBOARD");
  gameLaunches++;lastArgs=cmd; pi->hProcess=CreateEventW(nullptr,TRUE,TRUE,nullptr);pi->hThread=CreateEventW(nullptr,TRUE,TRUE,nullptr);return TRUE;
 }
 assert(std::filesystem::path(app).filename()==L"powershell.exe");
 return CreateProcessW(app,cmd,pa,ta,inherit,flags,env,cwd,si,pi);
}
BOOL WINAPI Shell(SHELLEXECUTEINFOW* s) {
 elevated++;lastArgs=s->lpParameters; assert(std::wstring(s->lpVerb)==L"runas"); assert(std::filesystem::path(s->lpFile).filename()==L"netsh.exe");
 SetLastError(ERROR_CANCELLED);return FALSE;
}
HANDLE WINAPI Snapshot(DWORD a,DWORD b) {return fakeProcesses?CreateEventW(nullptr,0,0,nullptr):CreateToolhelp32Snapshot(a,b);}
BOOL WINAPI First(HANDLE h,LPPROCESSENTRY32W e) {
 if(!fakeProcesses)return Process32FirstW(h,e);
 e->th32ProcessID=0x123456;wcscpy_s(e->szExeFile,L"MC3Runner.exe");return TRUE;
}
BOOL WINAPI Next(HANDLE h,LPPROCESSENTRY32W e) {return fakeProcesses?FALSE:Process32NextW(h,e);}
HANDLE WINAPI Open(DWORD a,BOOL b,DWORD c) {return fakeProcesses?CreateEventW(nullptr,0,0,nullptr):OpenProcess(a,b,c);}
BOOL WINAPI Image(HANDLE h,DWORD f,LPWSTR p,PDWORD n) {
 if(!fakeProcesses)return QueryFullProcessImageNameW(h,f,p,n);
 auto s=blocked?processPath:L"C:\\Windows\\System32\\harmless.exe";wcscpy_s(p,*n,s.c_str());*n=DWORD(s.size());return TRUE;
}
}
#define GetEnvironmentVariableW test::Environment
#define MessageBoxW test::Message
#define GetModuleFileNameW test::Module
#define SHGetKnownFolderPath test::Folder
#define CreateProcessW test::Create
#define ShellExecuteExW test::Shell
#define CreateToolhelp32Snapshot test::Snapshot
#define Process32FirstW test::First
#define Process32NextW test::Next
#define OpenProcess test::Open
#define QueryFullProcessImageNameW test::Image
#include "../../mod/launcher/launcher.cpp"
#undef GetEnvironmentVariableW
#undef MessageBoxW
#undef GetModuleFileNameW
#undef SHGetKnownFolderPath
#undef CreateProcessW
#undef ShellExecuteExW
#undef CreateToolhelp32Snapshot
#undef Process32FirstW
#undef Process32NextW
#undef OpenProcess
#undef QueryFullProcessImageNameW
bool Has(const wchar_t* s) {for(auto& m:test::messages)if(m.find(s)!=m.npos)return true;return false;}
constexpr uintmax_t kMinRunnerBytes=20u*1024u*1024u;
void FakeRunner(const std::filesystem::path& path,uintmax_t bytes=kMinRunnerBytes) {
 // Deliberately not a PE executable. Sparse/zero bytes meet discovery's size threshold and are never run.
 std::ofstream f(path,std::ios::binary|std::ios::trunc);assert(f);
 f.seekp(std::streamoff(bytes-1));f.put('\0');f.close();assert(std::filesystem::file_size(path)==bytes);
}
void RunnerSelectionTests(const std::filesystem::path& game) {
 namespace fs=std::filesystem;
 const auto modern=game/L"Midnight Club 3 DUB Edition Remix.exe",legacy=game/L".xrevive-fixture.exe";
 FakeRunner(modern,kMinRunnerBytes-1);
 assert(Runner(game).empty() && !IsGame(game)); // undersized EXEs are not runners
 FakeRunner(modern);
 assert(Runner(game)==modern && IsGame(game)); // current xrevive naming, without a legacy runner
 const auto now=fs::file_time_type::clock::now();
 fs::last_write_time(modern,now-std::chrono::hours(2));
 FakeRunner(legacy);
 fs::last_write_time(legacy,now-std::chrono::hours(3));
 assert(Runner(game)==modern); // new name is newer than the old SHA name
 fs::last_write_time(legacy,now-std::chrono::hours(1));
 assert(Runner(game)==legacy); // choose by timestamp, not by a preferred spelling
 fs::last_write_time(modern,now-std::chrono::minutes(10));
 assert(Runner(game)==modern);
 fs::create_directories(game/L"nested");FakeRunner(game/L"nested"/L"not-a-top-level-runner.exe");
 assert(Runner(game)==modern); // nested EXEs do not participate
 assert(fs::file_size(modern)==kMinRunnerBytes);
}
int wmain(int argc,wchar_t** argv) {
 assert(argc==2); auto root=std::filesystem::absolute(argv[1])/std::to_wstring(GetCurrentProcessId());
 test::dir=root/L"mod com acentos á e ' apóstrofo";test::desktop=root/L"desktop simulado";
 std::filesystem::create_directories(test::dir);std::filesystem::create_directories(test::desktop);
 // Todas as execuções de teste herdam a única variável autorizada; jamais chama o runner real.
 SetEnvironmentVariableW(L"MC3COOP_GAME",L"D:\\dev\\mc3-coop-data\\testgame\\Midnight Club 3 DUB Edition Remix");
 assert(Launch({L"--logs"})==0);assert(Has(L"nenhum log"));assert(test::gameLaunches==0);
 std::filesystem::create_directories(test::dir/L"logs"/L"runner");
 std::ofstream(test::dir/L"logs"/L"mod.log")<<"mod sample";
 std::ofstream(test::dir/L"logs"/L"runner"/L"rúnner.log")<<"runner sample";
 std::ofstream(test::dir/L"Log_File_For_Tera.log")<<"legacy sample";
 std::ofstream(test::dir/L"coop-online.toml")<<"DO_NOT_EXPORT";
 std::ofstream(test::dir/L"logs"/L"coop-online.toml")<<"DO_NOT_EXPORT";
 Sleep(1100);test::messages.clear();assert(Launch({L"--logs"})==0);assert(Has(L"Logs reunidos"));assert(test::gameLaunches==0);
 test::messages.clear();assert(Launch({L"--inexistente"})==1);assert(Has(L"Opção desconhecida"));
 test::messages.clear();assert(Launch({})==1);assert(Has(L"Falta xinput1_4.dll"));
 std::ofstream(test::dir/L"xinput1_4.dll")<<"fixture";
 // Redireciona IsGame por fixtures dentro do workspace, sem precisar acessar o testgame real.
 assert(RunnerPath(L"C:\\other\\MC3Runner.exe"));assert(RunnerPath(L"C:\\x\\xrevive.exe"));
 assert(RunnerPath(L"C:\\x\\.xrevive-abc.exe"));assert(!RunnerPath(L"C:\\x\\runner.exe"));
 assert(RunnerPath(L"C:\\tests\\Midnight Club 3 DUB Edition Remix.exe"));
 assert(RunnerPath(L"C:\\tests\\MIDNIGHT CLUB 3 DUB EDITION REMIX.EXE"));
 assert(!RunnerPath(L"C:\\tests\\Midnight Club 3 DUB Edition Remix.txt"));
 assert(RunnerPath(L"C:\\Users\\a\\com.teraflopping.xrevive\\runs\\54540079\\runner.exe"));
 test::blocked=true;test::messages.clear();assert(Launch({})==1);assert(Has(L"já está aberto"));assert(test::gameLaunches==0);test::blocked=false;
 test::processPath=L"C:\\tests\\Midnight Club 3 DUB Edition Remix.exe";
 test::blocked=true;test::messages.clear();assert(Launch({})==1);assert(Has(L"já está aberto"));assert(test::gameLaunches==0);test::blocked=false;
 auto ini=test::dir/L"MC3Coop.ini";
 std::ofstream(ini)<<"game=C:\\legacy\\game\n";
 assert(Remembered(ini)==L"C:\\legacy\\game");assert(IniSet(ini,L"game",L"C:\\pasta á\\jogo"));
 assert(Remembered(ini)==L"C:\\pasta á\\jogo");
 test::messages.clear();test::answer=IDNO;OfferFirewall(ini,test::dir/L"MC3Runner.exe",false);
 assert(test::elevated==0);auto n=test::messages.size();OfferFirewall(ini,test::dir/L"MC3Runner.exe",false);assert(test::messages.size()==n);
 test::answer=IDYES;OfferFirewall(ini,test::dir/L"MC3Runner.exe",true);
 assert(test::elevated==1);assert(Has(L"cancelada"));assert(test::lastArgs.find(L"protocol=UDP")!=std::wstring::npos);
 assert(test::lastArgs.find(L"profile=private")!=std::wstring::npos);

 test::answer=IDNO; test::forcedOverride=(root/L"inexistente").wstring();test::messages.clear();
 assert(Launch({})==1);assert(Has(L"MC3COOP_GAME não aponta"));assert(test::gameLaunches==0);
 auto fakeGame=root/L"jogo simulado";
 std::filesystem::create_directories(fakeGame/L"extracted");
 std::ofstream(fakeGame/L"extracted"/L"default.xbe")<<"fixture, not a game";
 RunnerSelectionTests(fakeGame);
 test::forcedOverride=fakeGame.wstring();
 std::filesystem::remove(test::dir/L"coop-online.toml");test::messages.clear();
 SetEnvironmentVariableW(L"XRECOMP_NATIVE_CONTROLLERS",L"1");
 SetEnvironmentVariableW(L"XRECOMP_KEYBOARD",nullptr);
 assert(Launch({})==0);assert(Has(L"co-op por IP"));assert(test::gameLaunches==1);
 assert(test::launchedNative==L"0");assert(test::launchedKeyboard==L"1");
 assert(test::lastArgs.find(fakeGame.wstring())!=std::wstring::npos);
 assert(Env(L"XRECOMP_LOG_DIR")== (test::dir/L"logs"/L"runner").wstring());
 assert(std::filesystem::is_regular_file(test::dir/L"MC3Runner.exe"));
 SetEnvironmentVariableW(L"XRECOMP_NATIVE_CONTROLLERS",L"1");
 SetEnvironmentVariableW(L"XRECOMP_KEYBOARD",L"0");
 test::messages.clear();assert(Launch({})==0);assert(!Has(L"co-op por IP"));assert(test::gameLaunches==2);
 assert(test::launchedNative==L"0");assert(test::launchedKeyboard==L"0");
 auto lock=CreateFileW((test::dir/L"logs"/L"locked.log").c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
 assert(lock!=INVALID_HANDLE_VALUE);Sleep(1100);test::messages.clear();
 assert(Launch({L"--logs"})==0);assert(Has(L"parte dos logs"));CloseHandle(lock);
 test::fakeProcesses=false;test::messages.clear();(void)CheckProcesses(); // enumeração real somente leitura
 std::wcout<<L"PASS: logs vazio/preenchido, Unicode/apóstrofo, opções, DLL ausente, processo bloqueado por caminho, INI UTF-16, firewall recusado/repetição/UAC cancelado, pasta forçada inválida, TOML ausente continua (CreateProcess interceptado), logs bloqueados. Runner real nunca iniciado.\n";
}
