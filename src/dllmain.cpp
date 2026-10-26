#include "gear_window.h"
#include "menu.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

volatile LONG g_started = 0;

DWORD WINAPI GearThread(LPVOID) {
  RunGearWindow();
  return 0;
}

void StartOnce() {
  if (InterlockedCompareExchange(&g_started, 1, 0) != 0) {
    return;
  }
  if (CreateThread(nullptr, 0, GearThread, nullptr, 0, nullptr) == nullptr) {
    InterlockedExchange(&g_started, 0);
  }
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    StartOnce();
  }
  return TRUE;
}

extern "C" {
__declspec(dllexport) void OnRegisterLogAPI(void*) {}
__declspec(dllexport) void OnRegisterMenuAPI(void*) {}
__declspec(dllexport) void OnPluginLoad(void*) { StartOnce(); }
__declspec(dllexport) void OnPluginUnload() { RequestMenuShutdown(); }
__declspec(dllexport) void OnLoad() { StartOnce(); }
}
