#include "gear_window.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

DWORD WINAPI GearThread(LPVOID) {
  RunGearWindow();
  return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(module);
    if (CreateThread(nullptr, 0, GearThread, nullptr, 0, nullptr) == nullptr) {
      return FALSE;
    }
  }
  return TRUE;
}
