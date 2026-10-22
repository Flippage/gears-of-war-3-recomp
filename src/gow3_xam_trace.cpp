// gow3 - ReXGlue Recompiled Project
//
// Dev tool: GOW3_XAM_TRACE=1 logs the guest's save/content, device and sign-in XAM calls with their
// arguments (r3..r8) and result, plus the content root name for XamContentCreateEx, and the file
// calls (NtCreateFile/NtOpenFile/NtWriteFile/NtSetInformationFile/NtFlushBuffersFile/NtClose) on
// anything but the game disc - save devices, profile, cache - with paths, lengths and offsets.
// Uses the export-thunk rewrite of gow3_xam_input_guard.cpp: rexruntime's `__imp__...` export is a
// jmp to the body that every guest caller funnels through.

#include "gow3_xam_trace.h"

#include <windows.h>

#include <array>
#include <cstdint>
#include <mutex>
#include <unordered_map>
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

// The first entries have dedicated argument decoding in TraceWrap (keep their order).
enum : size_t {
  kContentCreateEx,
  kNtCreateFile,
  kNtOpenFile,
  kNtWriteFile,
  kNtSetInformationFile,
  kNtFlushBuffersFile,
  kNtClose,
};
Target kTargets[] = {
    {"__imp__XamContentCreateEx", nullptr},          {"__imp__NtCreateFile", nullptr},
    {"__imp__NtOpenFile", nullptr},                  {"__imp__NtWriteFile", nullptr},
    {"__imp__NtSetInformationFile", nullptr},        {"__imp__NtFlushBuffersFile", nullptr},
    {"__imp__NtClose", nullptr},
    {"__imp__XamContentClose", nullptr},             {"__imp__XamContentFlush", nullptr},
    {"__imp__XamContentDelete", nullptr},            {"__imp__XamContentCreateEnumerator", nullptr},
    {"__imp__XamContentGetDeviceData", nullptr},     {"__imp__XamContentGetDeviceState", nullptr},
    {"__imp__XamContentGetCreator", nullptr},        {"__imp__XamShowDeviceSelectorUI", nullptr},
    {"__imp__XamUserGetSigninState", nullptr},       {"__imp__XamUserGetSigninInfo", nullptr},
    {"__imp__XamUserGetXUID", nullptr},              {"__imp__XamUserReadProfileSettings", nullptr},
    {"__imp__XamUserWriteProfileSettings", nullptr},
};
constexpr size_t kCount = sizeof(kTargets) / sizeof(kTargets[0]);

// Handles opened on anything but the game disc, so the file calls on save and profile data can be
// logged without the game's constant reads of D:\ and game:\.
std::mutex g_handles_mutex;
std::unordered_map<uint32_t, std::string> g_handles;

uint32_t Be32(uint8_t* base, uint32_t addr) {
  uint32_t v = 0;
  std::memcpy(&v, base + addr, 4);
  return _byteswap_ulong(v);
}

// X_OBJECT_ATTRIBUTES { root_directory, name_ptr -> X_ANSI_STRING { u16 length, u16 max, buffer } }.
std::string ObjectPath(uint8_t* base, uint32_t attributes) {
  if (!attributes) return "<null>";
  const uint32_t name = Be32(base, attributes + 4);
  if (!name) return "<no name>";
  uint16_t length = 0;
  std::memcpy(&length, base + name, 2);
  length = _byteswap_ushort(length);
  const uint32_t buffer = Be32(base, name + 4);
  std::string s;
  for (uint32_t i = 0; i < length && i < 260; ++i) {
    const char c = static_cast<char>(base[buffer + i]);
    s += (c >= 0x20 && c < 0x7F) ? c : '?';
  }
  return s;
}

bool IsDiscPath(const std::string& p) {
  auto starts = [&](const char* x) { return _strnicmp(p.c_str(), x, std::strlen(x)) == 0; };
  return starts("d:") || starts("game:") || starts("\\Device\\Harddisk0\\Partition1") ||
         starts("\\Device\\Cdrom0");
}

std::string HandlePath(uint32_t handle) {
  std::lock_guard<std::mutex> lock(g_handles_mutex);
  auto it = g_handles.find(handle);
  return it == g_handles.end() ? std::string() : it->second;
}

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
  if constexpr (I == kNtCreateFile || I == kNtOpenFile) {
    // NtCreateFile(handle_out r3, access r4, attributes r5, io_status r6, alloc_size r7,
    //              file_attributes r8, share r9, disposition r10, options ...);
    // NtOpenFile(handle_out r3, access r4, attributes r5, io_status r6, share r7, options r8).
    const std::string path = ObjectPath(base, a[2]);
    const uint32_t disposition = ctx.r10.u32;
    kTargets[I].body(ctx, base);
    if (!IsDiscPath(path)) {
      const uint32_t handle = ctx.r3.u32 == 0 && a[0] ? Be32(base, a[0]) : 0;
      if (handle) {
        std::lock_guard<std::mutex> lock(g_handles_mutex);
        g_handles[handle] = path;
      }
      REXLOG_INFO("XAMTRACE {} '{}' access={:#x} disposition={} -> {:#x} handle={:#x}",
                  kTargets[I].export_name + 6, path, a[1], I == kNtCreateFile ? disposition : 0,
                  ctx.r3.u32, handle);
    }
    return;
  } else if constexpr (I == kNtWriteFile || I == kNtSetInformationFile ||
                       I == kNtFlushBuffersFile || I == kNtClose) {
    // NtWriteFile(handle r3, event r4, apc r5, apc_ctx r6, io_status r7, buffer r8, length r9,
    //             byte_offset* r10); NtSetInformationFile(handle r3, io r4, info r5, length r6,
    //             class r7); NtFlushBuffersFile(handle r3, io r4); NtClose(handle r3).
    const std::string path = HandlePath(a[0]);
    const uint32_t length = ctx.r9.u32, offset_ptr = ctx.r10.u32;
    const uint32_t info = a[2], info_class = a[4];
    kTargets[I].body(ctx, base);
    if (path.empty()) return;
    if constexpr (I == kNtWriteFile) {
      const uint64_t offset = offset_ptr ? (uint64_t(Be32(base, offset_ptr)) << 32) |
                                               Be32(base, offset_ptr + 4)
                                         : ~0ull;
      REXLOG_INFO("XAMTRACE NtWriteFile '{}' length={:#x} offset={:#x} -> {:#x}", path, length,
                  offset, ctx.r3.u32);
    } else if constexpr (I == kNtSetInformationFile) {
      REXLOG_INFO("XAMTRACE NtSetInformationFile '{}' class={} value={:#x} -> {:#x}", path,
                  info_class, info ? Be32(base, info + 4) : 0, ctx.r3.u32);
    } else if constexpr (I == kNtFlushBuffersFile) {
      REXLOG_INFO("XAMTRACE NtFlushBuffersFile '{}' -> {:#x}", path, ctx.r3.u32);
    } else {
      REXLOG_INFO("XAMTRACE NtClose '{}' -> {:#x}", path, ctx.r3.u32);
      std::lock_guard<std::mutex> lock(g_handles_mutex);
      g_handles.erase(a[0]);
    }
    return;
  }
  std::string extra;
  if constexpr (I == kContentCreateEx) {
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
