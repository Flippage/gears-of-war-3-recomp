// gow3 - ReXGlue Recompiled Project
//
// Native mouse look, see gow3_mouse_aim.h. A window listener banks captured mouse motion on the UI
// thread; a hook on UPlayerInput::InputAxis (sub_8273B518, reached only through the vtables of
// PlayerInput and its Gears subclasses) sends the banked counts as MouseX / MouseY axis events for
// player 1, once per frame, before the original runs. The game's input config binds MouseX to
// "Count bXAxis | Axis aMouseX" and MouseY to "Count bYAxis | Axis aMouseY" (MouseSensitivity 60,
// mouse smoothing on), the same PC path Unreal Engine 3 uses on Windows.
//
// Gears 3 addresses (base disc 0.0.0.11), found in out/image/image.bin:
//   0x82146AA8 / 0x82146AB8  "MouseX" / "MouseY" (UTF-16), turned into FNames at start-up by
//                            sub_824F10F0 and stored in 0x834440C8 / 0x834440D0.
//   0x82147044 / 0x82147068  "XboxTypeS_RightX" / "XboxTypeS_RightY" -> 0x83444430 / 0x83444438.
//   0x8273B518               UPlayerInput::InputAxis(this r3, ControllerId r4, Key r5,
//                            Delta f1, DeltaTime f2, bGamepad r6): counts mouse samples for
//                            Key == MouseX (+368 / +372), stores the key at +232, continues into
//                            UInput::InputAxis (0x8273AFC0). Vtable entry 0x821897A4.
// Adapted from the Gears of War 2 port's gow2_mouse_aim.cpp (BSD-3-Clause, Santiago Quiroz
// Upegui and the gow2-recomp contributors), which injects through the viewport client instead.

#include "gow3_mouse_aim.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>
#include <rex/system/function_dispatcher.h>
#include <rex/ui/window.h>
#include <rex/ui/window_listener.h>

REXCVAR_DEFINE_DOUBLE(gow3_mouse_scale, 1.0, "input",
                      "Mouse sensitivity: multiplier on the mouse counts sent to the game.")
    .range(0.05, 10);
REXCVAR_DEFINE_BOOL(gow3_mouse_invert_y, false, "input", "Invert native mouse-look Y.");
REXCVAR_DEFINE_BOOL(gow3_mouse_aim_log, false, "input",
                    "Log native mouse-look stats once per second while the mouse is used.");

namespace rex::glue {

namespace {

constexpr uint32_t kInputAxisFn = 0x8273B518;
constexpr uint32_t kInputAxisSlot = 0x821897A4;
constexpr uint32_t kMouseXName = 0x834440C8;
constexpr uint32_t kMouseYName = 0x834440D0;
constexpr uint32_t kMouseXString = 0x82146AA8;

uint32_t LoadGuestU32(const uint8_t* base, uint32_t addr) {
  const uint8_t* p = base + addr;
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3];
}

// An FName as the guest loads it with ld: index in the upper half, number in the lower.
uint64_t LoadFNameValue(const uint8_t* base, uint32_t addr) {
  return (uint64_t(LoadGuestU32(base, addr)) << 32) | LoadGuestU32(base, addr + 4);
}

bool GuestUtf16Is(const uint8_t* base, uint32_t addr, const char* text) {
  for (uint32_t i = 0;; ++i) {
    const uint16_t c = uint16_t((base[addr + 2 * i] << 8) | base[addr + 2 * i + 1]);
    if (c != uint8_t(text[i])) return false;
    if (!c) return true;
  }
}

int64_t SteadyTicksNow() {
  return static_cast<int64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
}

constexpr int64_t kLogWindowTicks =
    std::chrono::steady_clock::duration(std::chrono::seconds(1)).count();

std::mutex g_bank_mutex;
double g_bank_dx = 0;
double g_bank_dy = 0;

struct MouseCounts {
  double dx = 0;
  double dy = 0;
};

MouseCounts DrainMouseBank() {
  std::lock_guard<std::mutex> lock(g_bank_mutex);
  MouseCounts out{g_bank_dx, g_bank_dy};
  g_bank_dx = 0;
  g_bank_dy = 0;
  return out;
}

class MouseBankListener : public rex::ui::WindowInputListener {
 public:
  void OnMouseMove(rex::ui::MouseEvent& e) override {
    // The PC Options menu (and other host UI) blocks game input while it is open.
    if (rex::cvar::Query<bool>("xam_input_block")) {
      DrainMouseBank();
      return;
    }
    rex::ui::Window* window = e.target();
    if (!window || !window->IsMouseCaptureRequested()) return;
    if (!rex::input::mnk::IsMouseLookActive()) return;
    if (e.dx() == 0 && e.dy() == 0) return;
    std::lock_guard<std::mutex> lock(g_bank_mutex);
    g_bank_dx += e.dx();
    g_bank_dy += e.dy();
  }
};

MouseBankListener g_mouse_listener;
PPCFunc* g_input_axis_orig = nullptr;
std::atomic<bool> g_installed{false};
std::atomic<bool> g_hooks_installed{false};
thread_local bool g_injecting = false;

bool SafeInvoke(PPCFunc* fn, PPCContext* call, uint8_t* base) {
  __try {
    fn(*call, base);
    return true;
  } __except (EXCEPTION_EXECUTE_HANDLER) {
    return false;
  }
}

// One MouseX / MouseY axis event through the original InputAxis, on the caller's object.
bool SendMouseAxis(const PPCContext& ctx, uint8_t* base, uint64_t key, double value) {
  PPCContext call = ctx;
  call.r5.u64 = key;
  call.r6.u64 = 0;  // bGamepad
  call.f1.f64 = double(float(value));
  return SafeInvoke(g_input_axis_orig, &call, base);
}

void MaybeLog(double x, double y, bool sent) {
  if (!REXCVAR_GET(gow3_mouse_aim_log)) return;
  static double acc_x = 0;
  static double acc_y = 0;
  static uint64_t frames = 0;
  static uint64_t failed = 0;
  static int64_t window_start = 0;
  if (x != 0 || y != 0) {
    acc_x += x;
    acc_y += y;
    ++frames;
    if (!sent) ++failed;
  }
  const int64_t now = SteadyTicksNow();
  if (window_start == 0) window_start = now;
  if (now - window_start < kLogWindowTicks) return;
  if (frames) {
    REXLOG_INFO("MOUSEAIM sent x={:.1f} y={:.1f} frames={} failed={}", acc_x, acc_y, frames,
                failed);
  }
  acc_x = acc_y = 0;
  frames = failed = 0;
  window_start = now;
}

void InputAxisHook(PPCContext& ctx, uint8_t* base) {
  // Player 1, once per frame (the first axis event that finds banked motion drains it).
  if (!g_injecting && ctx.r4.u32 == 0) {
    if (rex::cvar::Query<bool>("mnk_mouse")) {
      const MouseCounts counts = DrainMouseBank();
      if (counts.dx != 0 || counts.dy != 0) {
        const double scale = REXCVAR_GET(gow3_mouse_scale);
        const double x = counts.dx * scale;
        double y = -counts.dy * scale;
        if (REXCVAR_GET(gow3_mouse_invert_y)) y = -y;
        bool sent = true;
        g_injecting = true;
        if (x != 0) sent &= SendMouseAxis(ctx, base, LoadFNameValue(base, kMouseXName), x);
        if (y != 0) sent &= SendMouseAxis(ctx, base, LoadFNameValue(base, kMouseYName), y);
        g_injecting = false;
        MaybeLog(x, y, sent);
      }
    }
  }
  g_input_axis_orig(ctx, base);
}

}  // namespace

bool NativeMouseAimInstalled() {
  return g_hooks_installed.load(std::memory_order_acquire);
}

void PrepareNativeMouseAim() {
  // The mouse never drives the right stick; InstallNativeMouseAim turns that back on only if the
  // hook cannot be installed.
  rex::cvar::SetFlagByName("mnk_mouse_stick", "false");
  // Mouse buttons, unless the player bound these: left click fires (right trigger), right click
  // aims (left trigger), middle click zooms (right stick click), as in Gears of War on PC.
  struct DefaultBind {
    const char* name;
    const char* value;
  };
  for (const DefaultBind& bind : {DefaultBind{"keybind_right_trigger", "E,O,LMB"},
                                  DefaultBind{"keybind_left_trigger", "Q,I,RMB"},
                                  DefaultBind{"keybind_rstick_press", "K,MMB"}}) {
    if (rex::cvar::GetFlagSource(bind.name) == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName(bind.name, bind.value);
    }
  }
}

void InstallNativeMouseAim(rex::ui::Window* window) {
  if (g_installed.exchange(true, std::memory_order_acq_rel)) return;
  rex::Runtime* runtime = rex::Runtime::instance();
  rex::runtime::FunctionDispatcher* dispatcher =
      runtime ? runtime->function_dispatcher() : nullptr;
  uint8_t* base = runtime ? runtime->virtual_membase() : nullptr;
  const char* why = nullptr;
  if (!runtime || !dispatcher || !base || !window) {
    why = "runtime, dispatcher, memory or window missing";
  } else if (LoadGuestU32(base, kInputAxisSlot) != kInputAxisFn) {
    why = "PlayerInput InputAxis vtable entry mismatch";
  } else if (!GuestUtf16Is(base, kMouseXString, "MouseX")) {
    why = "MouseX name string mismatch";
  } else if (!(g_input_axis_orig = dispatcher->GetFunction(kInputAxisFn))) {
    why = "InputAxis missing from the dispatcher";
  } else if (!dispatcher->SetFunction(kInputAxisFn, &InputAxisHook)) {
    why = "hook install failed";
  }
  if (why) {
    rex::cvar::SetFlagByName("mnk_mouse_stick", "true");
    REXLOG_WARN("MOUSEAIM native mouse look off ({}); the mouse drives the right stick", why);
    return;
  }
  window->AddInputListener(&g_mouse_listener, 1);
  g_hooks_installed.store(true, std::memory_order_release);
  REXLOG_INFO("MOUSEAIM native mouse look installed (UPlayerInput::InputAxis {:08X})",
              kInputAxisFn);
}

}  // namespace rex::glue
