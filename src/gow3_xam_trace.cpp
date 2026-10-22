// gow3 - ReXGlue Recompiled Project
//
// Dev tool: GOW3_XAM_TRACE=1 logs every guest call to the save/content, device and sign-in XAM
// exports with its arguments (r3..r8) and result, plus the content root name for
// XamContentCreateEx. Uses the export-thunk rewrite of gow3_xam_input_guard.cpp: rexruntime's
// `__imp__Xam...` export is a jmp to the body that every guest caller funnels through.

#include "gow3_xam_trace.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>

#include <rex/logging.h>
#include <rex/ppc.h>

namespace {

constexpr size_t kThunkSize = 16;

struct Target {
  const char* export_name;
  PPCFunc* body;
};

Target kTargets[] = {
    {"__imp__XamContentCreateEx", nullptr},          {"__imp__XamContentClose", nullptr},
    {"__imp__XamContentDelete", nullptr},            {"__imp__XamContentCreateEnumerator", nullptr},
    {"__imp__XamContentGetDeviceData", nullptr},     {"__imp__XamContentGetDeviceState", nullptr},
    {"__imp__XamContentGetCreator", nullptr},        {"__imp__XamShowDeviceSelectorUI", nullptr},
    {"__imp__XamUserGetSigninState", nullptr},       {"__imp__XamUserGetSigninInfo", nullptr},
    {"__imp__XamUserGetXUID", nullptr},              {"__imp__XamUserGetDeviceContext", nullptr},
    {"__imp__XamUserReadProfileSettings", nullptr},  {"__imp__XamUserWriteProfileSettings", nullptr},
};
constexpr size_t kCount = sizeof(kTargets) / sizeof(kTargets[0]);

std::string GuestString(uint8_t* base, uint32_t addr) {
  if (!addr) return "<null>";
  std::string s;
  for (uint32_t i = 0; i < 64; ++i) {
    const char c = static_cast<char>(base[addr + i]);
    if (!c) break;
    s += (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  return s;
}

template <size_t I>
void TraceWrap(PPCContext& ctx, uint8_t* base) {
  const uint32_t a[6] = {ctx.r3.u32, ctx.r4.u32, ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32};
  std::string extra;
  if constexpr (I == 0) {
    // XamContentCreateEx(user, root_name, XCONTENT_DATA*, flags, disposition*, license*, ...);
    // XCONTENT_DATA: device_id, content_type, display_name[128] (UTF-16), file_name[42] at 0x108.
    extra = " root='" + GuestString(base, a[1]) + "'";
    if (a[2]) {
      uint32_t device = 0, type = 0;
      std::memcpy(&device, base + a[2], 4);
      std::memcpy(&type, base + a[2] + 4, 4);
      extra += fmt::format(" device={:#x} type={:#x} name='{}'", _byteswap_ulong(device),
                           _byteswap_ulong(type), GuestString(base, a[2] + 0x108));
    }
  }
  kTargets[I].body(ctx, base);
  REXLOG_INFO("XAMTRACE {}({:#x}, {:#x}, {:#x}, {:#x}, {:#x}, {:#x}){} -> {:#x}",
              kTargets[I].export_name + 6, a[0], a[1], a[2], a[3], a[4], a[5], extra, ctx.r3.u32);
}

template <size_t... I>
constexpr auto MakeWrappers(std::index_sequence<I...>) {
  return std::array<PPCFunc*, sizeof...(I)>{&TraceWrap<I>...};
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

}  // namespace

namespace rex::glue {

void InstallXamTrace() {
  char gate[8]{};
  if (!GetEnvironmentVariableA("GOW3_XAM_TRACE", gate, sizeof(gate)) || std::strcmp(gate, "1") != 0) {
    return;
  }
  HMODULE mod = GetModuleHandleA("rexruntime.dll");
  if (!mod) return;
  static const auto wrappers = MakeWrappers(std::make_index_sequence<kCount>{});
  size_t hits = 0;
  for (size_t i = 0; i < kCount; ++i) {
    auto* thunk = reinterpret_cast<uint8_t*>(GetProcAddress(mod, kTargets[i].export_name));
    if (!thunk || !(kTargets[i].body = ResolveThunkBody(thunk))) {
      REXLOG_WARN("XAMTRACE {} not hooked", kTargets[i].export_name);
      continue;
    }
    DWORD old = 0;
    if (!VirtualProtect(thunk, kThunkSize, PAGE_EXECUTE_READWRITE, &old)) continue;
    thunk[0] = 0xFF;
    thunk[1] = 0x25;
    std::memset(thunk + 2, 0, 4);
    void* replacement = reinterpret_cast<void*>(wrappers[i]);
    std::memcpy(thunk + 6, &replacement, 8);
    DWORD tmp = 0;
    VirtualProtect(thunk, kThunkSize, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), thunk, kThunkSize);
    ++hits;
  }
  REXLOG_INFO("XAMTRACE hooked {}/{} content/device/sign-in exports", hits, kCount);
}

}  // namespace rex::glue
