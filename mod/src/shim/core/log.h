// The SDK log macros, to the mod's log
#pragma once
namespace mod {
void Log(const char* fmt, ...);
}
#define LOGE(...) ::mod::Log(__VA_ARGS__)
#define LOGR(...) ::mod::Log(__VA_ARGS__)
#define LOGW(...) ::mod::Log(__VA_ARGS__)
#define LOGI(...) ((void)0)
