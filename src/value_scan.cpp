#include "value_scan.h"

#include "features.h"
#include "log.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace {

std::vector<uintptr_t> g_hits;
bool g_active = false;
bool g_truncated = false;
constexpr size_t kHitCap = 1500000;
constexpr size_t kWriteCap = 8;
constexpr SIZE_T kChunk = 256 * 1024;

HMODULE ThisModule() {
  HMODULE self = nullptr;
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                     reinterpret_cast<LPCWSTR>(&ThisModule), &self);
  return self;
}

bool WritableRegion(const MEMORY_BASIC_INFORMATION& info) {
  if (info.State != MEM_COMMIT || info.RegionSize < 4) {
    return false;
  }
  if ((info.Protect & (PAGE_GUARD | PAGE_NOACCESS)) != 0) {
    return false;
  }
  const DWORD protect = info.Protect & 0xFF;
  const bool writable = protect == PAGE_READWRITE || protect == PAGE_WRITECOPY || protect == PAGE_EXECUTE_READWRITE ||
                        protect == PAGE_EXECUTE_WRITECOPY;
  return writable && (info.Type == MEM_PRIVATE || info.Type == MEM_IMAGE);
}

bool ParseWhole(const std::wstring& text, int32_t& out, std::wstring& error) {
  if (text.empty()) {
    error = L"Type the number you see in the game.";
    return false;
  }
  wchar_t* end = nullptr;
  const long value = wcstol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != L'\0' || value < INT32_MIN || value > INT32_MAX) {
    error = L"Use a whole number, such as 1200.";
    return false;
  }
  out = static_cast<int32_t>(value);
  return true;
}

bool ParseDecimal(const std::wstring& text, float& out, std::wstring& error) {
  if (text.empty()) {
    error = L"Type the number you see in the game.";
    return false;
  }
  wchar_t* end = nullptr;
  const float value = wcstof(text.c_str(), &end);
  if (end == text.c_str() || *end != L'\0' || !std::isfinite(value)) {
    error = L"Use a decimal number, such as 100.5.";
    return false;
  }
  out = value;
  return true;
}

bool Matches(const uint8_t* bytes, bool whole_number, int32_t whole, float decimal) {
  if (whole_number) {
    int32_t value = 0;
    std::memcpy(&value, bytes, sizeof(value));
    return value == whole;
  }
  float value = 0.f;
  std::memcpy(&value, bytes, sizeof(value));
  return std::isfinite(value) && std::fabs(value - decimal) <= 0.02f;
}

bool ReadBytes(uintptr_t address, uint8_t* bytes, SIZE_T size) {
  SIZE_T read = 0;
  return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(address), bytes, size, &read) != 0 && read == size;
}

bool WriteValue(uintptr_t address, bool whole_number, int32_t whole, float decimal) {
  uint8_t bytes[4] = {};
  if (whole_number) {
    std::memcpy(bytes, &whole, sizeof(whole));
  } else {
    std::memcpy(bytes, &decimal, sizeof(decimal));
  }
  DWORD old = 0;
  if (!VirtualProtect(reinterpret_cast<void*>(address), sizeof(bytes), PAGE_EXECUTE_READWRITE, &old)) {
    return false;
  }
  std::memcpy(reinterpret_cast<void*>(address), bytes, sizeof(bytes));
  DWORD ignored = 0;
  VirtualProtect(reinterpret_cast<void*>(address), sizeof(bytes), old, &ignored);
  FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), sizeof(bytes));
  return true;
}

}  // namespace

bool ValueScanActive() {
  return g_active;
}

size_t ValueScanCount() {
  return g_hits.size();
}

bool ValueScanFirst(const std::wstring& text, bool whole_number, std::wstring& error) {
  error.clear();
  if (InPreview()) {
    error = L"The value finder runs inside Shadow of War.";
    return false;
  }
  int32_t whole = 0;
  float decimal = 0.f;
  if (whole_number) {
    if (!ParseWhole(text, whole, error)) {
      return false;
    }
  } else if (!ParseDecimal(text, decimal, error)) {
    return false;
  }

  g_hits.clear();
  g_active = false;
  g_truncated = false;
  g_hits.reserve(65536);
  const HMODULE self = ThisModule();
  LogLine(L"Value scan started.");

  std::vector<uint8_t> chunk(kChunk);
  uintptr_t address = 0;
  MEMORY_BASIC_INFORMATION info = {};
  while (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) == sizeof(info)) {
    const auto base = reinterpret_cast<uintptr_t>(info.BaseAddress);
    const uintptr_t next = base + info.RegionSize;
    if (info.AllocationBase != self && WritableRegion(info)) {
      uintptr_t cursor = base;
      while (cursor + 4 <= next && g_hits.size() < kHitCap) {
        const SIZE_T room = static_cast<SIZE_T>(std::min<uintptr_t>(kChunk, next - cursor));
        SIZE_T read = 0;
        if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void*>(cursor), chunk.data(), room, &read) && read >= 4) {
          const SIZE_T last = read - (read % 4);
          for (SIZE_T offset = 0; offset + 4 <= last && g_hits.size() < kHitCap; offset += 4) {
            if (Matches(chunk.data() + offset, whole_number, whole, decimal)) {
              g_hits.push_back(cursor + offset);
            }
          }
        }
        if (room == 0) {
          break;
        }
        cursor += room;
      }
      if (g_hits.size() >= kHitCap) {
        g_truncated = true;
        break;
      }
    }
    if (next <= address) {
      break;
    }
    address = next;
  }

  g_active = true;
  if (g_truncated) {
    error = L"That number is too common. Clear the search and try a rarer number.";
    Logf(L"Value scan stopped after %zu matches.", g_hits.size());
    return false;
  }
  Logf(L"First scan found %zu copies of that number.", g_hits.size());
  return true;
}

bool ValueScanNext(const std::wstring& text, bool whole_number, std::wstring& error) {
  error.clear();
  if (!g_active) {
    error = L"Press First scan before Next scan.";
    return false;
  }
  int32_t whole = 0;
  float decimal = 0.f;
  if (whole_number) {
    if (!ParseWhole(text, whole, error)) {
      return false;
    }
  } else if (!ParseDecimal(text, decimal, error)) {
    return false;
  }

  std::vector<uintptr_t> kept;
  kept.reserve(g_hits.size());
  uint8_t bytes[4] = {};
  for (uintptr_t address : g_hits) {
    if (ReadBytes(address, bytes, sizeof(bytes)) && Matches(bytes, whole_number, whole, decimal)) {
      kept.push_back(address);
    }
  }
  g_hits.swap(kept);
  Logf(L"Next scan left %zu copies.", g_hits.size());
  return true;
}

bool ValueScanWrite(const std::wstring& text, bool whole_number, std::wstring& error) {
  error.clear();
  if (!g_active || g_hits.empty()) {
    error = L"No copies left to write.";
    return false;
  }
  if (g_truncated || g_hits.size() > kWriteCap) {
    error = L"Too many copies. Change the number in game, type the new number, and press Next scan.";
    return false;
  }
  int32_t whole = 0;
  float decimal = 0.f;
  if (whole_number) {
    if (!ParseWhole(text, whole, error)) {
      return false;
    }
  } else if (!ParseDecimal(text, decimal, error)) {
    return false;
  }
  size_t wrote = 0;
  for (uintptr_t address : g_hits) {
    if (WriteValue(address, whole_number, whole, decimal)) {
      ++wrote;
    }
  }
  Logf(L"Wrote the new number to %zu copies.", wrote);
  if (wrote == 0) {
    error = L"The game refused the write.";
    return false;
  }
  return true;
}

void ValueScanClear() {
  g_hits.clear();
  g_active = false;
  g_truncated = false;
  LogLine(L"Value scan cleared.");
}
