// gow3 - ReXGlue Recompiled Project
//
// Campaign unlocks, see gow3_unlocks.h.
//
// Gears 3 addresses (base disc 0.0.0.11):
//   0x8251A2B0  UOnlinePlayerStorage::execGetProfileSettingValueInt (native table entry at
//               0x83396738, name "UOnlinePlayerStorageexecGetProfileSettingValueInt"). It reads the
//               setting id from the script stack and calls the C++ GetProfileSettingValueInt(this r3,
//               Id r4, int* Value r5) through the object's vtable (+336). The first call resolves
//               that vtable entry, and the hook goes on it from then on.

#include "gow3_unlocks.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <unordered_map>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>

REXCVAR_DEFINE_BOOL(gow3_unlock_insane, true, "GameFixes",
                    "Unlock Insane difficulty without finishing the campaign first.");
REXCVAR_DEFINE_BOOL(gow3_profile_log, false, "GameFixes",
                    "Log each profile setting the game's script reads (id, value), once per change.");

namespace rex::glue {

namespace {

constexpr uint32_t kExecGetProfileSettingValueInt = 0x8251A2B0;
constexpr uint32_t kGetValueIntVtableOffset = 336;
// GearProfileSettings (vtable 0x82247140): setting 82 is read when the campaign difficulty screen
// opens, and holds the unlockables as bits; Insane is unlockable 0 (Coalesced:
// [InsaneDifficulty GearUnlockableResourceProvider] UnlockableId=0).
constexpr uint32_t kGearProfileSettingsVtable = 0x82247140;
constexpr int32_t kUnlockablesSetting = 82;
constexpr uint32_t kInsaneUnlockBit = 1u << 0;

void StoreGuestU32(uint8_t* base, uint32_t addr, uint32_t value) {
  uint8_t* p = base + addr;
  p[0] = uint8_t(value >> 24);
  p[1] = uint8_t(value >> 16);
  p[2] = uint8_t(value >> 8);
  p[3] = uint8_t(value);
}

PPCFunc* g_exec_orig = nullptr;
PPCFunc* g_get_int_orig = nullptr;
std::atomic<bool> g_inner_resolved{false};

uint32_t LoadGuestU32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

void GetValueIntHook(PPCContext& ctx, uint8_t* base) {
  const uint32_t self = ctx.r3.u32;
  const int32_t id = ctx.r4.s32;
  const uint32_t value_addr = ctx.r5.u32;
  g_get_int_orig(ctx, base);
  if (id == kUnlockablesSetting && value_addr && REXCVAR_GET(gow3_unlock_insane) &&
      LoadGuestU32(base, self) == kGearProfileSettingsVtable) {
    const uint32_t unlocks = LoadGuestU32(base, value_addr);
    if (!(unlocks & kInsaneUnlockBit)) {
      StoreGuestU32(base, value_addr, unlocks | kInsaneUnlockBit);
      static std::atomic<int> logged{0};
      if (logged.fetch_add(1) < 4) {
        REXLOG_INFO("UNLOCKS Insane unlocked (unlockables 0x{:08X} -> 0x{:08X})", unlocks,
                    unlocks | kInsaneUnlockBit);
      }
    }
  }
  if (!REXCVAR_GET(gow3_profile_log) || !value_addr) return;
  static std::mutex mutex;
  static std::unordered_map<uint64_t, uint32_t> seen;
  const uint32_t value = LoadGuestU32(base, value_addr);
  const uint64_t key = (uint64_t(LoadGuestU32(base, self)) << 32) | uint32_t(id);
  std::lock_guard<std::mutex> lock(mutex);
  auto it = seen.find(key);
  if (it != seen.end() && it->second == value) return;
  if (seen.size() >= 2048) return;
  seen[key] = value;
  REXLOG_INFO("PROFILE get int id={} value={} (0x{:08X}) found={} object={:08X} vtable={:08X}", id,
              int32_t(value), value, ctx.r3.u32 & 1, self, LoadGuestU32(base, self));
}

void ExecHook(PPCContext& ctx, uint8_t* base) {
  if (!g_inner_resolved.load(std::memory_order_acquire)) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    if (!g_inner_resolved.load(std::memory_order_relaxed)) {
      const uint32_t vtable = LoadGuestU32(base, ctx.r3.u32);
      const uint32_t fn = vtable ? LoadGuestU32(base, vtable + kGetValueIntVtableOffset) : 0;
      auto* dispatcher = rex::Runtime::instance()->function_dispatcher();
      g_get_int_orig = fn ? dispatcher->GetFunction(fn) : nullptr;
      if (g_get_int_orig && dispatcher->SetFunction(fn, &GetValueIntHook)) {
        REXLOG_INFO("UNLOCKS GetProfileSettingValueInt at {:08X} hooked (vtable {:08X})", fn,
                    vtable);
      } else {
        REXLOG_WARN("UNLOCKS GetProfileSettingValueInt not hooked (vtable {:08X}, entry {:08X})",
                    vtable, fn);
      }
      g_inner_resolved.store(true, std::memory_order_release);
    }
  }
  g_exec_orig(ctx, base);
}

}  // namespace

void InstallUnlockHooks() {
  rex::Runtime* runtime = rex::Runtime::instance();
  auto* dispatcher = runtime ? runtime->function_dispatcher() : nullptr;
  if (!dispatcher) return;
  g_exec_orig = dispatcher->GetFunction(kExecGetProfileSettingValueInt);
  if (!g_exec_orig || !dispatcher->SetFunction(kExecGetProfileSettingValueInt, &ExecHook)) {
    REXLOG_WARN("UNLOCKS execGetProfileSettingValueInt not hooked");
    return;
  }
  REXLOG_INFO("UNLOCKS profile setting hook installed");
}

}  // namespace rex::glue
