#pragma once

#include <string>

enum class FeatureId {
  None = -1,
  Health = 0,
  Arrows,
  Might,
  Focus,
  Wrath,
  MissionTimers,
  ItemChallenges,
  Mirian,
  Count
};

constexpr int kFeatureCount = static_cast<int>(FeatureId::Count);

const wchar_t* FeatureName(FeatureId id);

void ScanFeatures();
std::wstring FeatureScanSummary();
bool FeaturesReady(FeatureId id);
const wchar_t* FeatureReason(FeatureId id);
bool FeatureEnabled(FeatureId id);
bool SetFeatureEnabled(FeatureId id, bool on, std::wstring& error);
void RestoreAllFeatures();
bool InPreview();
bool HealthHookSelfTest();
void PollMirianGrant();
void PollPlayerCapture();
