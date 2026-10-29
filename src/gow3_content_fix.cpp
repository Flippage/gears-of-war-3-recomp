// gow3 - ReXGlue Recompiled Project
//
// Save-device mount fix: the root name ("SG0_0", "savedrive0") the guest passes to
// XamContentCreateEx / XamContentCreate (Title Update 6) / XamContentClose lives in a stack buffer, and the call is asynchronous
// (it returns ERROR_IO_PENDING). rexruntime reads the name when the deferred work runs, by which
// time the guest has reused that stack: the device got mounted under garbage
// (bytes 83 41 00 00 ff 3a at the very root-name address, logged by the UTF-8 guard), every
// "SG0_0:\GearsCheckpoint0.sav" open failed with "no device", and the game reported
// "Error Loading Checkpoints".
//
// Fix: copy the root name and the XCONTENT_DATA into a ring of persistent guest buffers and
// hand those to the runtime instead. Same export-thunk rewrite as gow3_xam_input_guard.cpp.

#include "gow3_content_fix.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>

namespace {

constexpr size_t kThunkSize = 16;
// One slot holds a root name (32 bytes) and an XCONTENT_DATA (0x134 bytes, rounded up).
constexpr uint32_t kNameSize = 0x20;
constexpr uint32_t kContentDataSize = 0x134;
constexpr uint32_t kSlotSize = 0x160;
// Far more than can be in flight at once; a slot is reused only after 63 newer calls.
constexpr uint32_t kSlots = 64;

uint32_t g_ring = 0;  // guest address of the slot ring
std::atomic<uint32_t> g_next{0};
PPCFunc* g_create_body = nullptr;
PPCFunc* g_create_plain_body = nullptr;
PPCFunc* g_close_body = nullptr;

uint32_t NextSlot() { return g_ring + (g_next.fetch_add(1, std::memory_order_relaxed) % kSlots) * kSlotSize; }

// Copies a NUL-terminated guest string (at most kNameSize - 1 chars) into the slot.
void CopyName(uint8_t* base, uint32_t dst, uint32_t src) {
  uint32_t i = 0;
  for (; i < kNameSize - 1 && base[src + i]; ++i) base[dst + i] = base[src + i];
  base[dst + i] = 0;
}

// Root name r4 and XCONTENT_DATA r5 into a slot: the arguments XamContentCreateEx and
// XamContentCreate share.
void CopyCreateArgs(PPCContext& ctx, uint8_t* base) {
  static std::atomic<int> logged{0};
  if (logged.fetch_add(1, std::memory_order_relaxed) < 8) {
    char name[24] = {};
    for (int i = 0; ctx.r4.u32 && i < 23 && base[ctx.r4.u32 + i]; ++i) {
      const char c = char(base[ctx.r4.u32 + i]);
      name[i] = (c >= 32 && c < 127) ? c : '?';
    }
    REXLOG_INFO("CONTENTFIX create user={} root_name@{:08X} '{}' data@{:08X} flags={:#x} ring={}",
                ctx.r3.u32, ctx.r4.u32, name, ctx.r5.u32, ctx.r6.u32, g_ring != 0);
  }
  if (g_ring) {
    const uint32_t slot = NextSlot();
    if (ctx.r4.u32) {
      CopyName(base, slot, ctx.r4.u32);
      ctx.r4.u64 = slot;
    }
    if (ctx.r5.u32) {
      std::memcpy(base + slot + kNameSize, base + ctx.r5.u32, kContentDataSize);
      ctx.r5.u64 = slot + kNameSize;
    }
  }
}

// XamContentCreateEx(user r3, root_name r4, XCONTENT_DATA* r5, flags r6, disposition* r7,
//                    license_mask* r8, cache_size r9, content_size r10, overlapped on the stack)
void CreateWrap(PPCContext& ctx, uint8_t* base) {
  CopyCreateArgs(ctx, base);
  g_create_body(ctx, base);
}

// XamContentCreate(user r3, root_name r4, XCONTENT_DATA* r5, flags r6, disposition* r7,
//                  license_mask* r8, overlapped r9): what Title Update 6 calls for its saves.
void CreatePlainWrap(PPCContext& ctx, uint8_t* base) {
  CopyCreateArgs(ctx, base);
  g_create_plain_body(ctx, base);
}

// XamContentClose(root_name r3, overlapped r4)
void CloseWrap(PPCContext& ctx, uint8_t* base) {
  if (g_ring && ctx.r3.u32) {
    const uint32_t slot = NextSlot();
    CopyName(base, slot, ctx.r3.u32);
    ctx.r3.u64 = slot;
  }
  g_close_body(ctx, base);
}

PPCFunc* ResolveThunkBody(uint8_t* thunk) {
  if (thunk[0] == 0xE9) {
    int32_t rel = 0;
    std::memcpy(&rel, thunk + 1, 4);
    return reinterpret_cast<PPCFunc*>(thunk + 5 + rel);
  }
  if (thunk[0] == 0xFF && thunk[1] == 0x25) {
    int32_t rel = 0;
    std::memcpy(&rel, thunk + 2, 4);
    void* target = nullptr;
    std::memcpy(&target, thunk + 6 + rel, 8);
    return reinterpret_cast<PPCFunc*>(target);
  }
  return nullptr;
}

PPCFunc* Hook(HMODULE mod, const char* name, PPCFunc* wrapper) {
  auto* thunk = reinterpret_cast<uint8_t*>(GetProcAddress(mod, name));
  PPCFunc* body = thunk ? ResolveThunkBody(thunk) : nullptr;
  DWORD old = 0;
  if (!body || !VirtualProtect(thunk, kThunkSize, PAGE_EXECUTE_READWRITE, &old)) {
    REXLOG_WARN("CONTENTFIX {} not hooked", name);
    return nullptr;
  }
  thunk[0] = 0xFF;
  thunk[1] = 0x25;
  std::memset(thunk + 2, 0, 4);
  void* replacement = reinterpret_cast<void*>(wrapper);
  std::memcpy(thunk + 6, &replacement, 8);
  DWORD tmp = 0;
  VirtualProtect(thunk, kThunkSize, old, &tmp);
  FlushInstructionCache(GetCurrentProcess(), thunk, kThunkSize);
  return body;
}

}  // namespace

namespace rex::glue {

void InstallContentFix() {
  static bool installed = false;
  if (installed) return;
  installed = true;
  HMODULE mod = GetModuleHandleA("rexruntime.dll");
  if (!mod) return;
  g_create_body = Hook(mod, "__imp__XamContentCreateEx", &CreateWrap);
  g_create_plain_body = Hook(mod, "__imp__XamContentCreate", &CreatePlainWrap);
  g_close_body = Hook(mod, "__imp__XamContentClose", &CloseWrap);
  REXLOG_INFO("CONTENTFIX hooked create_ex={} create={} close={} (ring allocated on first guest launch)",
              g_create_body != nullptr, g_create_plain_body != nullptr, g_close_body != nullptr);
}

void AllocateContentFixRing() {
  if (g_ring) return;
  auto* ks = rex::system::kernel_state();
  if (!ks || !ks->memory()) return;
  g_ring = ks->memory()->SystemHeapAlloc(kSlots * kSlotSize);
  REXLOG_INFO("CONTENTFIX {} root-name slots at guest {:#x}", kSlots, g_ring);
}

}  // namespace rex::glue
