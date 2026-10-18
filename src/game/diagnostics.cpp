// Record guest-memory faults before a library's unhandled-exception handler
// gets control. This observer never handles an exception or resumes bad code.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdio>

#include "app/app.h"
#include "build_info.h"
#include "core/log.h"
#include "game/diagnostics.h"
#include "kernel/kernel.h"

namespace game::diagnostics {
namespace {

std::atomic_flag g_recorded = ATOMIC_FLAG_INIT;

LONG WINAPI GuestFault(EXCEPTION_POINTERS* exception) {
    const auto* record = exception->ExceptionRecord;
    if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2)
        return EXCEPTION_CONTINUE_SEARCH;
    uintptr_t start = reinterpret_cast<uintptr_t>(kernel::g_base);
    uintptr_t address = record->ExceptionInformation[1];
    auto* thread = kernel::Current();
    if (!start || !thread || !thread->ctx || address < start || address - start > UINT32_MAX ||
        g_recorded.test_and_set()) return EXCEPTION_CONTINUE_SEARCH;

    LOGE("guest fault: build=%s operation=%llu address=%08llX native=%p thread=%lu",
         MC3_COMMIT, static_cast<unsigned long long>(record->ExceptionInformation[0]),
         static_cast<unsigned long long>(address - start), record->ExceptionAddress, GetCurrentThreadId());
    kernel::LogGuestStack(*thread->ctx, 128);
    logx::Flush();

    auto dir = app::ExeDir() / "CrashDumps";
    CreateDirectoryW(dir.c_str(), nullptr);
    SYSTEMTIME now;
    GetLocalTime(&now);
    wchar_t name[128];
    swprintf(name, 128, L"MC3.guest-fault.%04u%02u%02u-%02u%02u%02u-%lu.dmp",
             now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    HANDLE file = CreateFileW((dir / name).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION info{GetCurrentThreadId(), exception, FALSE};
        BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                                   MINIDUMP_TYPE(MiniDumpWithDataSegs | MiniDumpWithThreadInfo |
                                                 MiniDumpWithIndirectlyReferencedMemory),
                                   &info, nullptr, nullptr);
        DWORD error = ok ? 0 : GetLastError();
        CloseHandle(file);
        LOGR("guest fault: diagnostic dump %s (error=%lu)", ok ? "saved" : "failed", error);
    } else LOGR("guest fault: cannot create diagnostic dump (error=%lu)", GetLastError());
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void Init() {
    LOGR("MC3 build %s, compiled %s %s", MC3_COMMIT, __DATE__, __TIME__);
    if (!AddVectoredExceptionHandler(1, GuestFault))
        LOGR("guest fault: observer unavailable (error=%lu)", GetLastError());
}
}  // namespace game::diagnostics
