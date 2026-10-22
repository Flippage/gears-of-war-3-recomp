// Adapted from gears-judgement-recomp (BSD-3-Clause).
#include "gow3_utf8_guard.h"

#include <windows.h>

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <string>

#include <rex/logging.h>
#include <rex/memory/mapped_memory.h>
#include <rex/system/kernel_state.h>

namespace {

// uint32_t utf8::next(std::_String_view_iterator<char>& it, end)
//
// The iterator reference arrives as the address of the caller's pointer slot, so
// only that first qword is written back and trailing debug-iterator fields stay
// intact. Verified against the original body at rexruntime RVA 0x9f370, which
// loads (%rcx), compares it with %rdx and stores through %rcx.
constexpr const char* kNextExport =
    "??$next@V?$_String_view_iterator@U?$char_traits@D@std@@@std@@@utf8@@"
    "YA_UAEAV?$_String_view_iterator@U?$char_traits@D@std@@@std@@V12@@Z";

std::atomic<uint32_t> g_repairs{0};

__declspec(noinline) void NoteRepair(const char* p, const char* end, uint8_t lead) {
  uint32_t n = g_repairs.fetch_add(1, std::memory_order_relaxed) + 1;
  if (n > 24) {
    return;
  }

  uint8_t* membase = nullptr;
  if (auto* ks = rex::system::kernel_state()) {
    if (auto* mem = ks->memory()) {
      membase = mem->virtual_membase();
    }
  }

  const auto* q = reinterpret_cast<const uint8_t*>(p);
  size_t span = static_cast<size_t>(end > p ? (end - p) : 0);
  if (span > 24) {
    span = 24;
  }
  std::string hex, ascii;
  for (size_t i = 0; i < span; ++i) {
    fmt::format_to(std::back_inserter(hex), "{:02x} ", q[i]);
    ascii += (q[i] >= 0x20 && q[i] < 0x7F) ? static_cast<char>(q[i]) : '.';
  }

  static uintptr_t runtime_base = [] {
    HMODULE m = GetModuleHandleA("rexruntime.dll");
    return m ? reinterpret_cast<uintptr_t>(m) : 0u;
  }();
  uintptr_t ret = reinterpret_cast<uintptr_t>(_ReturnAddress());

  REXLOG_WARN(
      "UTF8GUARD invalid sequence #{}: lead {:#02x} decoded as one byte. "
      "host={:#x} guest={:#x} remaining={} bytes=[{}] ascii='{}' caller=rexruntime+{:#x}",
      n, lead, reinterpret_cast<uintptr_t>(p),
      membase ? reinterpret_cast<uintptr_t>(p) - reinterpret_cast<uintptr_t>(membase) : 0u, span,
      hex, ascii, runtime_base ? ret - runtime_base : ret);
}

uint32_t __fastcall Gow3Utf8Next(const char** slot, const char* end) {
  const char* p = *slot;
  if (p >= end) {
    return 0;
  }
  uint8_t c = static_cast<uint8_t>(p[0]);
  if (c < 0x80) {
    *slot = p + 1;
    return c;
  }

  size_t extra;
  uint32_t cp;
  uint32_t lower;
  if ((c & 0xE0) == 0xC0) {
    extra = 1;
    cp = c & 0x1Fu;
    lower = 0x80;
  } else if ((c & 0xF0) == 0xE0) {
    extra = 2;
    cp = c & 0x0Fu;
    lower = 0x800;
  } else if ((c & 0xF8) == 0xF0) {
    extra = 3;
    cp = c & 0x07u;
    lower = 0x10000;
  } else {
    NoteRepair(p, end, c);
    *slot = p + 1;
    return c;
  }

  size_t i = 1;
  for (; i <= extra; ++i) {
    const char* q = p + i;
    if (q >= end) {
      break;
    }
    uint8_t b = static_cast<uint8_t>(*q);
    if ((b & 0xC0) != 0x80) {
      break;
    }
    cp = (cp << 6) | (b & 0x3Fu);
  }
  // Overlong, surrogate, out-of-range or truncated: hand the lead byte back as
  // a one-byte codepoint so callers keep byte-for-byte fidelity and every
  // decode loop still advances.
  if (i <= extra || cp < lower || cp > 0x10FFFFu || (cp >= 0xD800u && cp <= 0xDFFFu)) {
    NoteRepair(p, end, c);
    *slot = p + 1;
    return c;
  }
  *slot = p + 1 + extra;
  return cp;
}

bool PatchFunction(const char* mangled, void* replacement, const char* label) {
  HMODULE mod = GetModuleHandleA("rexruntime.dll");
  if (!mod) {
    REXLOG_WARN("UTFGUARD rexruntime.dll not loaded, {} not installed", label);
    return false;
  }
  void* target = reinterpret_cast<void*>(GetProcAddress(mod, mangled));
  if (!target) {
    REXLOG_WARN("UTFGUARD {} not found in rexruntime.dll", label);
    return false;
  }
  // FF 25 00000000 imm64 -> jmp qword ptr [rip+0]; 14 bytes.
  static_assert(sizeof(void*) == 8);
  uint8_t* p = static_cast<uint8_t*>(target);
  DWORD old = 0;
  if (!VirtualProtect(p, 16, PAGE_EXECUTE_READWRITE, &old)) {
    REXLOG_WARN("UTFGUARD VirtualProtect failed for {} (gle={})", label, GetLastError());
    return false;
  }
  p[0] = 0xFF;
  p[1] = 0x25;
  std::memset(p + 2, 0, 4);
  std::memcpy(p + 6, &replacement, 8);
  DWORD tmp = 0;
  VirtualProtect(p, 16, old, &tmp);
  FlushInstructionCache(GetCurrentProcess(), p, 14);
  REXLOG_INFO("UTFGUARD {} <- {:#x} patched (target={:#x})", label,
              reinterpret_cast<uintptr_t>(replacement), reinterpret_cast<uintptr_t>(p));
  return true;
}

}  // namespace

namespace rex::glue {

void InstallUtf8Guard() {
  static bool installed = false;
  if (installed) {
    return;
  }
  installed = true;
  PatchFunction(kNextExport, reinterpret_cast<void*>(&Gow3Utf8Next), "utf8::next");
}

}  // namespace rex::glue
