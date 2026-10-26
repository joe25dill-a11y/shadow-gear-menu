#include "settings_store.h"

#include "log.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::wstring IniPath() {
  wchar_t path[MAX_PATH] = {};
  HMODULE self = nullptr;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<LPCWSTR>(&IniPath), &self)) {
    return L"shadow-gear-menu.ini";
  }
  const DWORD length = GetModuleFileNameW(self, path, MAX_PATH);
  if (length == 0 || length >= MAX_PATH) {
    return L"shadow-gear-menu.ini";
  }
  wchar_t* slash = wcsrchr(path, L'\\');
  if (slash == nullptr) {
    return L"shadow-gear-menu.ini";
  }
  wcscpy_s(slash + 1, MAX_PATH - static_cast<size_t>(slash + 1 - path), L"shadow-gear-menu.ini");
  return path;
}

int ClampKey(int key) {
  if (key < 0 || key > 255) {
    return 0;
  }
  if (key >= VK_LBUTTON && key <= VK_XBUTTON2) {
    return 0;
  }
  if (key == VK_ESCAPE || key == VK_SHIFT || key == VK_CONTROL || key == VK_MENU) {
    return 0;
  }
  return key;
}

const wchar_t* FeatureKey(FeatureId id) {
  switch (id) {
    case FeatureId::Health:
      return L"health";
    case FeatureId::Arrows:
      return L"arrows";
    case FeatureId::Might:
      return L"might";
    case FeatureId::Focus:
      return L"focus";
    case FeatureId::Wrath:
      return L"wrath";
    case FeatureId::MissionTimers:
      return L"timers";
    case FeatureId::ItemChallenges:
      return L"challenges";
    case FeatureId::Mirian:
      return L"mirian";
    default:
      return L"unknown";
  }
}

}  // namespace

Settings& AppSettings() {
  static Settings settings;
  return settings;
}

void Settings::Load() {
  const std::wstring path = IniPath();
  const int stored_key = GetPrivateProfileIntW(L"ui", L"menu_key", VK_NUMPAD1, path.c_str());
  menu_key = ClampKey(stored_key);
  const bool home_was_menu = menu_key == VK_HOME;
  if (menu_key == 0 || home_was_menu) {
    menu_key = VK_NUMPAD1;
  }
  const int scale = GetPrivateProfileIntW(L"ui", L"scale", 100, path.c_str());
  if (scale == 125 || scale == 150) {
    scale_percent = scale;
  } else {
    scale_percent = 100;
  }
  accent = GetPrivateProfileIntW(L"ui", L"accent", 0, path.c_str());
  if (accent < 0 || accent > 2) {
    accent = 0;
  }
  for (int i = 0; i < kFeatureCount; ++i) {
    const FeatureId id = static_cast<FeatureId>(i);
    wchar_t key_name[32] = {};
    swprintf_s(key_name, L"%s_key", FeatureKey(id));
    enabled[i] = GetPrivateProfileIntW(L"features", FeatureKey(id), 0, path.c_str()) != 0;
    hotkey[i] = ClampKey(GetPrivateProfileIntW(L"features", key_name, 0, path.c_str()));
    if (hotkey[i] == menu_key) {
      hotkey[i] = 0;
    }
  }
  if (home_was_menu) {
    Save();
  }
}

void Settings::Save() const {
  const std::wstring path = IniPath();
  wchar_t value[16] = {};
  swprintf_s(value, L"%d", menu_key);
  WritePrivateProfileStringW(L"ui", L"menu_key", value, path.c_str());
  swprintf_s(value, L"%d", scale_percent);
  WritePrivateProfileStringW(L"ui", L"scale", value, path.c_str());
  swprintf_s(value, L"%d", accent);
  WritePrivateProfileStringW(L"ui", L"accent", value, path.c_str());
  for (int i = 0; i < kFeatureCount; ++i) {
    const FeatureId id = static_cast<FeatureId>(i);
    wchar_t key_name[32] = {};
    swprintf_s(key_name, L"%s_key", FeatureKey(id));
    swprintf_s(value, L"%d", enabled[i] ? 1 : 0);
    WritePrivateProfileStringW(L"features", FeatureKey(id), value, path.c_str());
    swprintf_s(value, L"%d", hotkey[i]);
    WritePrivateProfileStringW(L"features", key_name, value, path.c_str());
  }
}

void Settings::Reset() {
  menu_key = VK_NUMPAD1;
  scale_percent = 100;
  accent = 0;
  for (int i = 0; i < kFeatureCount; ++i) {
    enabled[i] = false;
    hotkey[i] = 0;
  }
  Save();
  LogLine(L"Settings reset to defaults");
}
