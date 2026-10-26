#include "features.h"

#include "log.h"

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <vector>

struct Site {
  bool ready = false;
  wchar_t reason[180] = L"Not scanned yet.";
};

Site g_sites[kFeatureCount];
bool g_on[kFeatureCount] = {};
uint8_t* g_dev_flags = nullptr;
uint8_t* g_timer_site = nullptr;
uint8_t g_timer_original[2] = {0x84, 0xC0};
uint8_t* g_health_site = nullptr;
uint8_t g_health_original[11] = {};
uint8_t* g_health_stub = nullptr;
bool g_health_patched = false;
// Filled by the health stub when the public player-flag check matches.
volatile uintptr_t g_seen_health = 0;
volatile uintptr_t g_seen_flag = 0;
uint8_t* g_mirian_site = nullptr;
uint8_t g_mirian_original[8] = {0x45, 0x89, 0x51, 0x20, 0x48, 0x83, 0xC4, 0x28};
uint8_t* g_mirian_stub = nullptr;
bool g_mirian_patched = false;
volatile int g_mirian_grant = 0;
volatile int g_mirian_applied = 0;
volatile int g_mirian_before = 0;
volatile int g_mirian_after = 0;
uint8_t* g_challenge_site = nullptr;
uint8_t g_challenge_original[6] = {};
uint8_t* g_challenge_stub = nullptr;
bool g_challenge_patched = false;

const wchar_t* kNames[kFeatureCount] = {L"God mode", L"Infinite arrows", L"Infinite might",
                                        L"Infinite focus", L"Infinite wrath", L"Mission timers",
                                        L"Item challenges", L"Mirian"};

int FeatureIndex(FeatureId id) {
  return static_cast<int>(id);
}

bool Valid(FeatureId id) {
  const int index = FeatureIndex(id);
  return index >= 0 && index < kFeatureCount;
}

// Signatures published by ReaperAnon's Cheat Hotkeys mod.
// Wildcards are -1. The health site is the 11-byte compare-and-store.
constexpr int kDevBool[] = {0x48, 0x8D, 0x0D, -1, -1, -1, -1, 0xE8, -1, -1, -1, -1,
                            0x84, 0xC0, 0x74, 0x37, 0x48, 0x8B, 0xCB};
constexpr int kMission[] = {0x48, 0x8D, 0x59, 0x10, 0x48, 0x8B, 0xF9, 0x48, 0x8B, 0xCB, 0xE8, -1, -1, -1, -1,
                            0x84, 0xC0, 0x0F, 0x85};
constexpr int kHealth[] = {0x0F, 0x2F, 0xF2, 0xF3, 0x0F, 0x11, 0x93, 0x2C, 0x04, 0x00, 0x00};
constexpr int kChallenge[] = {0x48, 0x8B, 0x51, 0x08, 0x33, 0xC0, 0x48, 0x85, 0xD2, 0x74, 0x09, 0x8B, 0x4A, 0x34};
// Published by ReaperAnon's Myrian Multiplier. The hook site is the 8 bytes at match + 5.
constexpr int kMirian[] = {0xE8, -1, -1, -1, -1, 0x45, 0x89, 0x51, 0x20, 0x48, 0x83, 0xC4, 0x28};

bool Readable(DWORD protect) {
  if ((protect & PAGE_GUARD) || (protect & PAGE_NOACCESS)) {
    return false;
  }
  switch (protect & 0xFF) {
    case PAGE_READONLY:
    case PAGE_READWRITE:
    case PAGE_WRITECOPY:
    case PAGE_EXECUTE_READ:
    case PAGE_EXECUTE_READWRITE:
    case PAGE_EXECUTE_WRITECOPY:
      return true;
    default:
      return false;
  }
}

bool RegionHolds(const uint8_t* address, size_t size) {
  MEMORY_BASIC_INFORMATION info = {};
  if (VirtualQuery(address, &info, sizeof(info)) == 0) {
    return false;
  }
  if (info.State != MEM_COMMIT || !Readable(info.Protect)) {
    return false;
  }
  const auto start = reinterpret_cast<uintptr_t>(address);
  const auto end = start + size;
  const auto region_end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
  return end <= region_end;
}

void SetReason(FeatureId id, const wchar_t* text) {
  wcscpy_s(g_sites[FeatureIndex(id)].reason, text);
  g_sites[FeatureIndex(id)].ready = false;
}

void MarkReady(FeatureId id) {
  g_sites[FeatureIndex(id)].ready = true;
  wcscpy_s(g_sites[FeatureIndex(id)].reason, L"");
}

bool WriteBytes(void* destination, const void* source, size_t size) {
  DWORD old = 0;
  if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old)) {
    return false;
  }
  std::memcpy(destination, source, size);
  DWORD ignored = 0;
  VirtualProtect(destination, size, old, &ignored);
  FlushInstructionCache(GetCurrentProcess(), destination, size);
  return true;
}

const uint8_t* FindInRegion(const uint8_t* data, size_t size, const int* pattern, size_t length) {
  if (length == 0 || size < length) {
    return nullptr;
  }
  for (size_t i = 0; i + length <= size; ++i) {
    bool matched = true;
    for (size_t j = 0; j < length; ++j) {
      if (pattern[j] >= 0 && data[i + j] != static_cast<uint8_t>(pattern[j])) {
        matched = false;
        break;
      }
    }
    if (matched) {
      return data + i;
    }
  }
  return nullptr;
}

void CollectMatches(uint8_t* base, size_t image_size, const int* pattern, size_t length, std::vector<uint8_t*>& out) {
  const uint8_t* end = base + image_size;
  MEMORY_BASIC_INFORMATION info = {};
  for (uint8_t* cursor = base; cursor < end;) {
    if (VirtualQuery(cursor, &info, sizeof(info)) == 0 || info.RegionSize == 0) {
      break;
    }
    uint8_t* region = static_cast<uint8_t*>(info.BaseAddress);
    uint8_t* region_end = region + info.RegionSize;
    if (region < base) {
      region = base;
    }
    if (region_end > end) {
      region_end = const_cast<uint8_t*>(end);
    }
    if (info.State == MEM_COMMIT && Readable(info.Protect) && region_end > region) {
      const uint8_t* scan = region;
      size_t remain = static_cast<size_t>(region_end - region);
      while (remain >= length && out.size() < 32) {
        const uint8_t* hit = FindInRegion(scan, remain, pattern, length);
        if (hit == nullptr) {
          break;
        }
        out.push_back(const_cast<uint8_t*>(hit));
        const size_t step = static_cast<size_t>(hit - scan) + 1;
        scan += step;
        remain -= step;
      }
    }
    cursor = static_cast<uint8_t*>(info.BaseAddress) + info.RegionSize;
  }
}

bool InImage(uint8_t* base, size_t image_size, const void* pointer) {
  const auto address = reinterpret_cast<uintptr_t>(pointer);
  const auto start = reinterpret_cast<uintptr_t>(base);
  return address >= start && address < start + image_size;
}

int DevOffset(FeatureId id) {
  switch (id) {
    case FeatureId::Focus:
      return 0;
    case FeatureId::Might:
      return 1;
    case FeatureId::Arrows:
      return 2;
    case FeatureId::Wrath:
      return 3;
    default:
      return -1;
  }
}

struct AsmBuf {
  std::vector<uint8_t> bytes;
  void emit(std::initializer_list<uint8_t> values) { bytes.insert(bytes.end(), values); }
  size_t here() const { return bytes.size(); }
  size_t emit_jz() {
    bytes.push_back(0x74);
    bytes.push_back(0x00);
    return bytes.size() - 1;
  }
  bool fix_jz(size_t rel_at, size_t target) {
    const int relative = static_cast<int>(target) - static_cast<int>(rel_at + 1);
    if (relative < -128 || relative > 127) {
      return false;
    }
    bytes[rel_at] = static_cast<uint8_t>(relative);
    return true;
  }
  size_t emit_jcc_near(uint8_t opcode) {
    bytes.push_back(0x0F);
    bytes.push_back(opcode);
    bytes.insert(bytes.end(), 4, 0);
    return bytes.size() - 4;
  }
  void fix_near(size_t rel_at, size_t target) {
    const int relative = static_cast<int>(target) - static_cast<int>(rel_at + 4);
    std::memcpy(&bytes[rel_at], &relative, sizeof(relative));
  }
  void emit_mov_rax(uint64_t address) {
    emit({0x48, 0xB8});
    for (int i = 0; i < 8; ++i) {
      bytes.push_back(static_cast<uint8_t>(address & 0xFF));
      address >>= 8;
    }
  }
};

void EmitStoreAbs(AsmBuf& buf, volatile uintptr_t* slot, bool store_rbx) {
  buf.emit_mov_rax(reinterpret_cast<uint64_t>(slot));
  buf.emit(store_rbx ? std::initializer_list<uint8_t>{0x48, 0x89, 0x18}
                     : std::initializer_list<uint8_t>{0x48, 0x89, 0x10});
}

bool BuildHealthStub(uint8_t* stub_base, uint8_t* return_to, std::vector<uint8_t>& out) {
  AsmBuf asm_buf;
  asm_buf.emit({0x50, 0x52});
  asm_buf.emit({0x48, 0x8B, 0x56, 0x08});
  asm_buf.emit({0x48, 0x85, 0xD2});
  const size_t jz_null_outer = asm_buf.emit_jz();
  asm_buf.emit({0x48, 0x8B, 0x12});
  asm_buf.emit({0x48, 0x85, 0xD2});
  const size_t jz_null_inner = asm_buf.emit_jz();
  // +0x90 does not fit in a signed 8-bit displacement. 0x90 there means -112.
  asm_buf.emit({0xF7, 0x82, 0x90, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00});
  const size_t jz_flag = asm_buf.emit_jz();
  asm_buf.emit({0xF3, 0x0F, 0x10, 0x93, 0x28, 0x04, 0x00, 0x00});
  // rbx is the health object. rdx is the object whose flag said this hit is the player.
  EmitStoreAbs(asm_buf, &g_seen_health, true);
  EmitStoreAbs(asm_buf, &g_seen_flag, false);
  const size_t pops = asm_buf.here();
  asm_buf.emit({0x5A, 0x58});
  asm_buf.emit({0xF3, 0x0F, 0x11, 0x93, 0x2C, 0x04, 0x00, 0x00});
  asm_buf.emit({0x0F, 0x2F, 0xF2});
  const size_t jump_at = asm_buf.here();
  asm_buf.emit({0xE9, 0x00, 0x00, 0x00, 0x00});
  if (!asm_buf.fix_jz(jz_null_outer, pops) || !asm_buf.fix_jz(jz_null_inner, pops) || !asm_buf.fix_jz(jz_flag, pops)) {
    return false;
  }
  const intptr_t next = reinterpret_cast<intptr_t>(stub_base) + static_cast<intptr_t>(jump_at + 5);
  const intptr_t relative = reinterpret_cast<intptr_t>(return_to) - next;
  if (relative < INT32_MIN || relative > INT32_MAX) {
    return false;
  }
  const int32_t rel32 = static_cast<int32_t>(relative);
  std::memcpy(&asm_buf.bytes[jump_at + 1], &rel32, sizeof(rel32));
  out = std::move(asm_buf.bytes);
  return true;
}

extern "C" __declspec(noinline) int32_t AdjustMirian(uint8_t* outer, int32_t proposed) {
  if (!g_mirian_grant || outer == nullptr) {
    return proposed;
  }
  __try {
    auto* inner = *reinterpret_cast<uint8_t**>(outer);
    if (inner == nullptr || *reinterpret_cast<uint32_t*>(inner + 4) != 0x23B603FBu) {
      return proposed;
    }
    const int32_t current = *reinterpret_cast<int32_t*>(outer + 0x20);
    if (current < 0) {
      return proposed;
    }
    int64_t updated = static_cast<int64_t>(current) + 100000;
    if (updated > INT32_MAX) {
      updated = INT32_MAX;
    }
    g_mirian_before = current;
    g_mirian_after = static_cast<int32_t>(updated);
    g_mirian_grant = 0;
    g_mirian_applied = 1;
    return static_cast<int32_t>(updated);
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return proposed;
  }
}

bool BuildMirianStub(uint8_t* stub_base, uint8_t* return_to, std::vector<uint8_t>& out) {
  AsmBuf buf;
  // At this site rsp is 16-byte aligned. push + sub 0x28 keeps the call aligned.
  buf.emit({0x41, 0x51});
  buf.emit({0x48, 0x83, 0xEC, 0x28});
  buf.emit({0x49, 0x8B, 0xC9});
  buf.emit({0x41, 0x8B, 0xD2});
  buf.emit_mov_rax(reinterpret_cast<uint64_t>(&AdjustMirian));
  buf.emit({0xFF, 0xD0});
  buf.emit({0x44, 0x8B, 0xD0});
  buf.emit({0x48, 0x83, 0xC4, 0x28});
  buf.emit({0x41, 0x59});
  buf.emit({0x45, 0x89, 0x51, 0x20});
  buf.emit({0x48, 0x83, 0xC4, 0x28});
  const size_t jump_at = buf.here();
  buf.emit({0xE9, 0x00, 0x00, 0x00, 0x00});
  const intptr_t next = reinterpret_cast<intptr_t>(stub_base) + static_cast<intptr_t>(jump_at + 5);
  const intptr_t back = reinterpret_cast<intptr_t>(return_to) - next;
  if (back < INT32_MIN || back > INT32_MAX) {
    return false;
  }
  const int32_t rel32 = static_cast<int32_t>(back);
  std::memcpy(&buf.bytes[jump_at + 1], &rel32, sizeof(rel32));
  out = std::move(buf.bytes);
  return true;
}

void* AllocateNear(void* target, size_t bytes) {
  SYSTEM_INFO info = {};
  GetSystemInfo(&info);
  const uintptr_t gran = info.dwAllocationGranularity ? info.dwAllocationGranularity : 0x10000;
  const uintptr_t origin = reinterpret_cast<uintptr_t>(target) & ~(gran - 1);
  const uintptr_t span = 0x70000000ull;
  const uintptr_t min_addr = origin > span ? origin - span : 0;
  const uintptr_t max_addr = origin + span;

  auto search = [&](uintptr_t start, uintptr_t stop, bool forward) -> void* {
    uintptr_t addr = start;
    for (int guard = 0; guard < 20000; ++guard) {
      if (forward) {
        if (addr >= stop) {
          return nullptr;
        }
      } else if (addr <= stop || addr < min_addr) {
        return nullptr;
      }
      MEMORY_BASIC_INFORMATION region = {};
      if (VirtualQuery(reinterpret_cast<void*>(addr), &region, sizeof(region)) == 0 || region.RegionSize == 0) {
        return nullptr;
      }
      const uintptr_t region_base = reinterpret_cast<uintptr_t>(region.BaseAddress);
      const uintptr_t region_end = region_base + region.RegionSize;
      if (region.State == MEM_FREE) {
        uintptr_t aligned = (region_base + gran - 1) & ~(gran - 1);
        if (aligned < min_addr) {
          aligned = min_addr;
        }
        if (aligned + bytes <= region_end && aligned < max_addr) {
          void* block = VirtualAlloc(reinterpret_cast<void*>(aligned), bytes, MEM_COMMIT | MEM_RESERVE,
                                     PAGE_EXECUTE_READWRITE);
          if (block != nullptr) {
            return block;
          }
        }
      }
      if (forward) {
        if (region_end <= addr) {
          return nullptr;
        }
        addr = region_end;
      } else {
        if (region_base == 0) {
          return nullptr;
        }
        addr = region_base - 1;
      }
    }
    return nullptr;
  };

  if (void* block = search(origin, max_addr, true)) {
    return block;
  }
  if (origin == 0) {
    return nullptr;
  }
  return search(origin - 1, min_addr, false);
}

bool InstallHealthHook() {
  if (g_health_patched) {
    return true;
  }
  if (g_health_site == nullptr || !RegionHolds(g_health_site, sizeof(g_health_original))) {
    return false;
  }
  if (std::memcmp(g_health_site, g_health_original, sizeof(g_health_original)) != 0) {
    return false;
  }
  if (g_health_stub == nullptr) {
    void* block = AllocateNear(g_health_site, 4096);
    if (block == nullptr) {
      return false;
    }
    std::vector<uint8_t> stub;
    uint8_t* return_to = g_health_site + sizeof(g_health_original);
    if (!BuildHealthStub(static_cast<uint8_t*>(block), return_to, stub)) {
      VirtualFree(block, 0, MEM_RELEASE);
      return false;
    }
    std::memcpy(block, stub.data(), stub.size());
    DWORD old = 0;
    VirtualProtect(block, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), block, stub.size());
    g_health_stub = static_cast<uint8_t*>(block);
  }
  const intptr_t relative = reinterpret_cast<intptr_t>(g_health_stub) - (reinterpret_cast<intptr_t>(g_health_site) + 5);
  if (relative < INT32_MIN || relative > INT32_MAX) {
    return false;
  }
  uint8_t patch[11] = {};
  patch[0] = 0xE9;
  const int32_t rel32 = static_cast<int32_t>(relative);
  std::memcpy(patch + 1, &rel32, sizeof(rel32));
  std::memset(patch + 5, 0x90, 6);
  if (!WriteBytes(g_health_site, patch, sizeof(patch))) {
    return false;
  }
  g_health_patched = true;
  return true;
}

bool RemoveHealthHook() {
  if (!g_health_patched) {
    return true;
  }
  if (!WriteBytes(g_health_site, g_health_original, sizeof(g_health_original))) {
    return false;
  }
  g_health_patched = false;
  return true;
}

bool SetDevFlag(FeatureId id, bool on, std::wstring& error) {
  const int offset = DevOffset(id);
  if (g_dev_flags == nullptr || offset < 0 || !RegionHolds(g_dev_flags + offset, 1)) {
    error = L"The resource flags are not available.";
    return false;
  }
  const uint8_t current = g_dev_flags[offset];
  if (current != 0x00 && current != 0x01) {
    error = L"Those resource flags are already changed, so this menu left them alone.";
    return false;
  }
  const uint8_t value = on ? 0x01 : 0x00;
  if (!WriteBytes(g_dev_flags + offset, &value, 1)) {
    error = L"Could not update that resource flag.";
    return false;
  }
  return true;
}

bool SetMission(bool on, std::wstring& error) {
  if (g_timer_site == nullptr || !RegionHolds(g_timer_site, 2)) {
    error = L"The mission timer site is not available.";
    return false;
  }
  const uint8_t expected_on[2] = {0xEB, 0x06};
  const uint8_t* current_ok = on ? g_timer_original : expected_on;
  if (std::memcmp(g_timer_site, current_ok, 2) != 0 && std::memcmp(g_timer_site, on ? expected_on : g_timer_original, 2) != 0) {
    error = L"Mission timer code is already modified, so this menu left it alone.";
    return false;
  }
  const uint8_t* next = on ? expected_on : g_timer_original;
  if (!WriteBytes(g_timer_site, next, 2)) {
    error = L"Could not update the mission timer.";
    return false;
  }
  return true;
}

bool HookSelfTestBody() {
  std::vector<uint8_t> code;
  auto* base = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(0x10000000));
  auto* ret = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(0x18000000));
  if (!BuildHealthStub(base, ret, code) || code.size() < 8 || code[0] != 0x50 || code[1] != 0x52) {
    return false;
  }
  int pop_at = -1;
  for (size_t i = 0; i + 1 < code.size(); ++i) {
    if (code[i] == 0x48 && i + 1 < code.size() && code[i + 1] == 0xB8) {
      i += 9;
      continue;
    }
    if (code[i] == 0x5A && code[i + 1] == 0x58) {
      pop_at = static_cast<int>(i);
      break;
    }
  }
  if (pop_at < 0) {
    return false;
  }
  int jumps = 0;
  for (size_t i = 0; i + 5 < code.size(); ++i) {
    if (code[i] == 0x48 && code[i + 1] == 0xB8) {
      i += 9;
      continue;
    }
    if (code[i] != 0x74) {
      continue;
    }
    const int target = static_cast<int>(i + 2) + static_cast<int8_t>(code[i + 1]);
    if (target != pop_at) {
      return false;
    }
    ++jumps;
  }
  if (jumps != 3 || code[code.size() - 5] != 0xE9) {
    return false;
  }
  const uint8_t flag_test[] = {0xF7, 0x82, 0x90, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00};
  bool found_test = false;
  for (size_t i = 0; i + sizeof(flag_test) <= code.size(); ++i) {
    if (std::memcmp(code.data() + i, flag_test, sizeof(flag_test)) == 0) {
      found_test = true;
      break;
    }
  }
  if (!found_test) {
    return false;
  }
  int32_t relative = 0;
  std::memcpy(&relative, &code[code.size() - 4], sizeof(relative));
  return base + code.size() + relative == ret;
}

bool InstallChallengeHook() {
  if (g_challenge_patched) {
    return true;
  }
  if (g_challenge_site == nullptr || !RegionHolds(g_challenge_site, sizeof(g_challenge_original))) {
    return false;
  }
  if (std::memcmp(g_challenge_site, g_challenge_original, sizeof(g_challenge_original)) != 0) {
    return false;
  }
  if (g_challenge_stub == nullptr) {
    void* block = AllocateNear(g_challenge_site, 4096);
    if (block == nullptr) {
      return false;
    }
    uint8_t stub[16] = {0x89, 0x4A, 0x30, 0x3B, 0x4A, 0x30, 0xE9, 0, 0, 0, 0};
    uint8_t* return_to = g_challenge_site + sizeof(g_challenge_original);
    const intptr_t relative = reinterpret_cast<intptr_t>(return_to) - (reinterpret_cast<intptr_t>(block) + 11);
    if (relative < INT32_MIN || relative > INT32_MAX) {
      VirtualFree(block, 0, MEM_RELEASE);
      return false;
    }
    const int32_t rel32 = static_cast<int32_t>(relative);
    std::memcpy(stub + 7, &rel32, sizeof(rel32));
    std::memcpy(block, stub, 11);
    DWORD old = 0;
    VirtualProtect(block, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), block, 11);
    g_challenge_stub = static_cast<uint8_t*>(block);
  }
  const intptr_t relative = reinterpret_cast<intptr_t>(g_challenge_stub) - (reinterpret_cast<intptr_t>(g_challenge_site) + 5);
  if (relative < INT32_MIN || relative > INT32_MAX) {
    return false;
  }
  uint8_t patch[6] = {0xE9, 0, 0, 0, 0, 0x90};
  const int32_t rel32 = static_cast<int32_t>(relative);
  std::memcpy(patch + 1, &rel32, sizeof(rel32));
  if (!WriteBytes(g_challenge_site, patch, sizeof(patch))) {
    return false;
  }
  g_challenge_patched = true;
  return true;
}

bool RemoveChallengeHook() {
  if (!g_challenge_patched) {
    return true;
  }
  if (!WriteBytes(g_challenge_site, g_challenge_original, sizeof(g_challenge_original))) {
    return false;
  }
  g_challenge_patched = false;
  return true;
}

bool InstallMirianHook() {
  if (g_mirian_patched) {
    return true;
  }
  if (g_mirian_site == nullptr || !RegionHolds(g_mirian_site, sizeof(g_mirian_original))) {
    return false;
  }
  if (std::memcmp(g_mirian_site, g_mirian_original, sizeof(g_mirian_original)) != 0) {
    return false;
  }
  if (g_mirian_stub == nullptr) {
    void* block = AllocateNear(g_mirian_site, 4096);
    if (block == nullptr) {
      return false;
    }
    std::vector<uint8_t> code;
    if (!BuildMirianStub(static_cast<uint8_t*>(block), g_mirian_site + sizeof(g_mirian_original), code)) {
      VirtualFree(block, 0, MEM_RELEASE);
      return false;
    }
    std::memcpy(block, code.data(), code.size());
    DWORD old = 0;
    VirtualProtect(block, 4096, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), block, code.size());
    g_mirian_stub = static_cast<uint8_t*>(block);
  }
  const intptr_t relative = reinterpret_cast<intptr_t>(g_mirian_stub) - (reinterpret_cast<intptr_t>(g_mirian_site) + 5);
  if (relative < INT32_MIN || relative > INT32_MAX) {
    return false;
  }
  uint8_t patch[8] = {0xE9, 0, 0, 0, 0, 0x90, 0x90, 0x90};
  const int32_t rel32 = static_cast<int32_t>(relative);
  std::memcpy(patch + 1, &rel32, sizeof(rel32));
  if (!WriteBytes(g_mirian_site, patch, sizeof(patch))) {
    return false;
  }
  g_mirian_patched = true;
  return true;
}

bool RemoveMirianHook() {
  if (!g_mirian_patched) {
    return true;
  }
  if (!WriteBytes(g_mirian_site, g_mirian_original, sizeof(g_mirian_original))) {
    return false;
  }
  g_mirian_patched = false;
  return true;
}

void ReleaseForScan() {
  std::wstring error;
  if (g_on[FeatureIndex(FeatureId::Health)]) {
    RemoveHealthHook();
  }
  if (g_on[FeatureIndex(FeatureId::ItemChallenges)]) {
    RemoveChallengeHook();
  }
  if (g_on[FeatureIndex(FeatureId::Mirian)]) {
    RemoveMirianHook();
  }
  if (g_on[FeatureIndex(FeatureId::MissionTimers)]) {
    SetMission(false, error);
  }
  const FeatureId resources[] = {FeatureId::Focus, FeatureId::Might, FeatureId::Arrows, FeatureId::Wrath};
  for (FeatureId id : resources) {
    if (g_on[FeatureIndex(id)]) {
      SetDevFlag(id, false, error);
    }
  }
  for (int i = 0; i < kFeatureCount; ++i) {
    g_on[i] = false;
  }
  if (!g_health_patched && g_health_stub != nullptr) {
    VirtualFree(g_health_stub, 0, MEM_RELEASE);
    g_health_stub = nullptr;
  }
  if (!g_challenge_patched && g_challenge_stub != nullptr) {
    VirtualFree(g_challenge_stub, 0, MEM_RELEASE);
    g_challenge_stub = nullptr;
  }
  if (!g_mirian_patched && g_mirian_stub != nullptr) {
    VirtualFree(g_mirian_stub, 0, MEM_RELEASE);
    g_mirian_stub = nullptr;
  }
}

void FailAll(const wchar_t* reason) {
  for (int i = 0; i < kFeatureCount; ++i) {
    SetReason(static_cast<FeatureId>(i), reason);
  }
}

void ScanReal() {
  ReleaseForScan();
  for (int i = 0; i < kFeatureCount; ++i) {
    g_on[i] = false;
    g_sites[i].ready = false;
  }
  g_dev_flags = nullptr;
  g_timer_site = nullptr;
  g_health_site = nullptr;
  g_challenge_site = nullptr;
  g_mirian_site = nullptr;

  if (!HookSelfTestBody()) {
    FailAll(L"The health hook failed its safety check.");
    LogLine(L"Health hook self-test failed");
    return;
  }

  HMODULE module = GetModuleHandleW(L"ShadowOfWar.exe");
  if (module == nullptr) {
    FailAll(L"Shadow of War is not running this plugin.");
    LogLine(L"ShadowOfWar.exe is not loaded");
    return;
  }
  auto* base = reinterpret_cast<uint8_t*>(module);
  const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
  if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
    FailAll(L"Shadow of War's executable header could not be read.");
    return;
  }
  const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
  if (nt->Signature != IMAGE_NT_SIGNATURE) {
    FailAll(L"Shadow of War's executable header could not be read.");
    return;
  }
  const size_t image_size = nt->OptionalHeader.SizeOfImage;

  std::vector<uint8_t*> dev_hits;
  CollectMatches(base, image_size, kDevBool, sizeof(kDevBool) / sizeof(kDevBool[0]), dev_hits);
  for (uint8_t* hit : dev_hits) {
    if (!RegionHolds(hit + 3, 4)) {
      continue;
    }
    int32_t relative = 0;
    std::memcpy(&relative, hit + 3, sizeof(relative));
    uint8_t* flags = hit + 7 + relative;
    if (!InImage(base, image_size, flags) || !RegionHolds(flags, 4)) {
      continue;
    }
    bool boolean_flags = true;
    for (int i = 0; i < 4; ++i) {
      if (flags[i] != 0x00 && flags[i] != 0x01) {
        boolean_flags = false;
        break;
      }
    }
    if (!boolean_flags) {
      continue;
    }
    g_dev_flags = flags;
    break;
  }
  if (g_dev_flags != nullptr) {
    MarkReady(FeatureId::Focus);
    MarkReady(FeatureId::Might);
    MarkReady(FeatureId::Arrows);
    MarkReady(FeatureId::Wrath);
    Logf(L"Resource flags found at %p", g_dev_flags);
  } else {
    const wchar_t* reason = L"The published focus, might, arrow, and wrath flags were not found.";
    SetReason(FeatureId::Focus, reason);
    SetReason(FeatureId::Might, reason);
    SetReason(FeatureId::Arrows, reason);
    SetReason(FeatureId::Wrath, reason);
    LogLine(L"Resource flags were not found");
  }

  std::vector<uint8_t*> timer_hits;
  CollectMatches(base, image_size, kMission, sizeof(kMission) / sizeof(kMission[0]), timer_hits);
  for (uint8_t* hit : timer_hits) {
    uint8_t* site = hit + 0x0F;
    if (!RegionHolds(site, 4)) {
      continue;
    }
    if (site[0] == 0x84 && site[1] == 0xC0 && site[2] == 0x0F && site[3] == 0x85) {
      g_timer_site = site;
      g_timer_original[0] = site[0];
      g_timer_original[1] = site[1];
      break;
    }
  }
  if (g_timer_site != nullptr) {
    MarkReady(FeatureId::MissionTimers);
    Logf(L"Mission timer site found at %p", g_timer_site);
  } else {
    SetReason(FeatureId::MissionTimers, L"The published mission-timer check was not found.");
    LogLine(L"Mission timer site was not found");
  }

  std::vector<uint8_t*> health_hits;
  CollectMatches(base, image_size, kHealth, sizeof(kHealth) / sizeof(kHealth[0]), health_hits);
  for (uint8_t* hit : health_hits) {
    if (!RegionHolds(hit, sizeof(g_health_original))) {
      continue;
    }
    if (std::memcmp(hit, "\x0F\x2F\xF2\xF3\x0F\x11\x93\x2C\x04\x00\x00", sizeof(g_health_original)) != 0) {
      continue;
    }
    g_health_site = hit;
    std::memcpy(g_health_original, hit, sizeof(g_health_original));
    break;
  }
  if (g_health_site != nullptr) {
    MarkReady(FeatureId::Health);
    Logf(L"Health site found at %p", g_health_site);
  } else {
    SetReason(FeatureId::Health, L"The published health update was not found, or another plugin already changed it.");
    LogLine(L"Health site was not found");
  }

  std::vector<uint8_t*> challenge_hits;
  CollectMatches(base, image_size, kChallenge, sizeof(kChallenge) / sizeof(kChallenge[0]), challenge_hits);
  for (uint8_t* hit : challenge_hits) {
    uint8_t* site = hit + 0x0B;
    if (!RegionHolds(site, sizeof(g_challenge_original))) {
      continue;
    }
    if (site[0] != 0x8B || site[1] != 0x4A || site[2] != 0x34) {
      continue;
    }
    g_challenge_site = site;
    std::memcpy(g_challenge_original, site, sizeof(g_challenge_original));
    break;
  }
  if (g_challenge_site != nullptr) {
    MarkReady(FeatureId::ItemChallenges);
    Logf(L"Item challenge site found at %p", g_challenge_site);
  } else {
    SetReason(FeatureId::ItemChallenges, L"The published item-challenge check was not found.");
    LogLine(L"Item challenge site was not found");
  }

  std::vector<uint8_t*> mirian_hits;
  CollectMatches(base, image_size, kMirian, sizeof(kMirian) / sizeof(kMirian[0]), mirian_hits);
  int mirian_count = 0;
  uint8_t* mirian_site = nullptr;
  for (uint8_t* hit : mirian_hits) {
    uint8_t* site = hit + 5;
    if (!RegionHolds(site, sizeof(g_mirian_original))) {
      continue;
    }
    if (std::memcmp(site, g_mirian_original, sizeof(g_mirian_original)) != 0) {
      continue;
    }
    ++mirian_count;
    mirian_site = site;
  }
  if (mirian_count == 1) {
    g_mirian_site = mirian_site;
    MarkReady(FeatureId::Mirian);
    Logf(L"Mirian site found at %p", g_mirian_site);
  } else {
    SetReason(FeatureId::Mirian, L"The published Mirian update was not found.");
    Logf(L"Mirian site was not found. Matches: %d", mirian_count);
  }
}

const wchar_t* FeatureName(FeatureId id) {
  if (!Valid(id)) {
    return L"Feature";
  }
  return kNames[FeatureIndex(id)];
}

bool InPreview() {
#ifdef FEATURES_PREVIEW
  return true;
#else
  return false;
#endif
}

bool HealthHookSelfTest() {
  return HookSelfTestBody();
}

bool PageReadable(uintptr_t address, size_t bytes) {
  if (address < 0x10000) {
    return false;
  }
  MEMORY_BASIC_INFORMATION info = {};
  if (VirtualQuery(reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info)) {
    return false;
  }
  if (info.State != MEM_COMMIT) {
    return false;
  }
  const DWORD protect = info.Protect & 0xFF;
  const bool readable = protect == PAGE_READONLY || protect == PAGE_READWRITE || protect == PAGE_WRITECOPY ||
                        protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE ||
                        protect == PAGE_EXECUTE_WRITECOPY;
  if (!readable) {
    return false;
  }
  const auto region_end = reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize;
  return address + bytes <= region_end;
}

void PollMirianGrant() {
#ifdef FEATURES_PREVIEW
  return;
#else
  if (!g_mirian_applied) {
    return;
  }
  g_mirian_applied = 0;
  Logf(L"Added 100000 Mirian. It was %d, now %d.", g_mirian_before, g_mirian_after);
  RemoveMirianHook();
#endif
}

void PollPlayerCapture() {
#ifdef FEATURES_PREVIEW
  return;
#else
  static bool logged = false;
  if (logged) {
    return;
  }
  const uintptr_t health = g_seen_health;
  const uintptr_t flag = g_seen_flag;
  if (health == 0 || flag == 0) {
    return;
  }
  logged = true;
  Logf(L"Player hit captured. Health object %p. Flag object %p.", reinterpret_cast<void*>(health),
       reinterpret_cast<void*>(flag));
#endif
}

void ScanFeatures() {
#ifdef FEATURES_PREVIEW
  for (int i = 0; i < kFeatureCount; ++i) {
    g_on[i] = false;
    if (HealthHookSelfTest()) {
      MarkReady(static_cast<FeatureId>(i));
    } else {
      SetReason(static_cast<FeatureId>(i), L"The health hook failed its safety check.");
    }
  }
  LogLine(HealthHookSelfTest() ? L"Preview scan ready. Hook self-test passed."
                                : L"Preview scan failed the hook self-test.");
#else
  ScanReal();
#endif
}

bool FeaturesReady(FeatureId id) {
  return Valid(id) && g_sites[FeatureIndex(id)].ready;
}

const wchar_t* FeatureReason(FeatureId id) {
  if (!Valid(id)) {
    return L"That feature does not exist.";
  }
  return g_sites[FeatureIndex(id)].reason;
}

bool FeatureEnabled(FeatureId id) {
  return Valid(id) && g_on[FeatureIndex(id)];
}

bool SetFeatureEnabled(FeatureId id, bool on, std::wstring& error) {
  error.clear();
  if (!Valid(id)) {
    error = L"That feature does not exist.";
    return false;
  }
  if (!g_sites[FeatureIndex(id)].ready) {
    error = g_sites[FeatureIndex(id)].reason;
    return false;
  }
  if (g_on[FeatureIndex(id)] == on) {
    return true;
  }
#ifdef FEATURES_PREVIEW
  g_on[FeatureIndex(id)] = on;
  return true;
#else
  bool wrote = false;
  if (id == FeatureId::Health) {
    wrote = on ? InstallHealthHook() : RemoveHealthHook();
    if (!wrote) {
      error = on ? L"Could not enable health protection." : L"Could not restore the health update.";
    } else {
      LogLine(on ? L"God mode hook installed. Take a hit. Health should stay up."
                 : L"God mode hook removed.");
    }
  } else if (id == FeatureId::MissionTimers) {
    wrote = SetMission(on, error);
  } else if (id == FeatureId::ItemChallenges) {
    wrote = on ? InstallChallengeHook() : RemoveChallengeHook();
    if (!wrote) {
      error = on ? L"Could not enable item challenges." : L"Could not restore the item-challenge check.";
    }
  } else if (id == FeatureId::Mirian) {
    if (on) {
      g_mirian_applied = 0;
      g_mirian_grant = 1;
    } else {
      g_mirian_grant = 0;
    }
    wrote = on ? InstallMirianHook() : RemoveMirianHook();
    if (!wrote) {
      g_mirian_grant = 0;
      error = on ? L"Could not enable the Mirian update." : L"Could not restore the Mirian update.";
    } else if (on) {
      LogLine(L"Mirian grant armed. The next Mirian change adds 100000.");
    }
  } else {
    wrote = SetDevFlag(id, on, error);
  }
  if (!wrote) {
    Logf(L"%s failed: %s", FeatureName(id), error.c_str());
    return false;
  }
  g_on[FeatureIndex(id)] = on;
  Logf(L"%s %s", FeatureName(id), on ? L"on" : L"off");
  return true;
#endif
}

std::wstring FeatureScanSummary() {
  if (InPreview()) {
    return L"Desktop preview. Numpad 1 opens the menu. Home scans inside the game. Gear stats stay unavailable.";
  }
  const FeatureId ids[] = {FeatureId::Health, FeatureId::Arrows, FeatureId::MissionTimers, FeatureId::ItemChallenges,
                           FeatureId::Mirian};
  const wchar_t* names[] = {L"god mode", L"resources", L"timers", L"item challenges", L"mirian"};
  std::wstring found;
  std::wstring missing;
  for (int i = 0; i < 5; ++i) {
    std::wstring& bucket = FeaturesReady(ids[i]) ? found : missing;
    if (!bucket.empty()) {
      bucket += L", ";
    }
    bucket += names[i];
  }
  std::wstring text;
  if (!found.empty()) {
    text += L"Found: ";
    text += found;
  }
  if (!found.empty() && !missing.empty()) {
    text += L". ";
  }
  if (!missing.empty()) {
    text += L"Missing: ";
    text += missing;
  }
  text += L". Gear stats stay unavailable.";
  return text;
}

void RestoreAllFeatures() {
  std::wstring error;
  for (int i = 0; i < kFeatureCount; ++i) {
    if (g_on[i]) {
      SetFeatureEnabled(static_cast<FeatureId>(i), false, error);
    }
  }
}
