#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "features.h"

struct Settings {
  int menu_key = VK_NUMPAD1;
  int scale_percent = 100;
  int accent = 0;
  bool enabled[kFeatureCount] = {};
  int hotkey[kFeatureCount] = {};

  void Load();
  void Save() const;
  void Reset();
};

Settings& AppSettings();
