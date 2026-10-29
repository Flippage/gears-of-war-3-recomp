// gow3 - ReXGlue Recompiled Project
//
// Native mouse look, see gow3_mouse_aim.h. A window listener banks captured mouse motion on the UI
// thread; a hook on UGameViewportClient::InputAxis (sub_82614F38) sends the banked counts as
// MouseX / MouseY axis events for player 1, once per frame, before the original runs, so the
// Scaleform menus and PlayerInput both get them. Without that hook (gow3_menu_mouse_input off) the
// events go to UPlayerInput::InputAxis (sub_8273B518, reached only through the vtables of
// PlayerInput and its Gears subclasses) instead. The game's input config binds MouseX to
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
#include "gow3_addresses.h"
#include "gow3_button_glyphs.h"

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
REXCVAR_DEFINE_BOOL(gow3_menu_mouse_input, true, "input",
                    "Send mouse motion through the viewport client, so the Scaleform menus get it "
                    "too (off: to PlayerInput only, gameplay look).");
REXCVAR_DEFINE_BOOL(gow3_mouse_aim_log, false, "input",
                    "Log native mouse-look stats once per second while the mouse is used.");

namespace rex::glue {

namespace {

// Addresses per executable: gow3_addresses.h (base disc values in the comments below).
constexpr uint32_t kInputAxisFn = addr::kInputAxisFn;
constexpr uint32_t kInputAxisSlot = addr::kInputAxisSlot;
constexpr uint32_t kMouseXName = addr::kMouseXName;
constexpr uint32_t kMouseYName = addr::kMouseYName;
constexpr uint32_t kMouseXString = addr::kMouseXString;
// Diagnostics for menu mouse (gow3_mouse_aim_log): the player-level InputAxis dispatch, a virtual
// (vtable 0x821617F8 + 0x24C) that walks the local player's PlayerController Interactions
// (+1148 data, +1152 count; the player from GEngine 0x834290BC, GamePlayers +1184 / +1188, actor
// +64) and calls each one's InputAxis (+300).
constexpr uint32_t kControllerAxisFn = addr::kControllerAxisFn;
constexpr uint32_t kControllerAxisSlot = addr::kControllerAxisSlot;
constexpr uint32_t kEnginePtr = addr::kEnginePtr;
// UGameViewportClient::InputAxis, entered through its FViewportClient interface (this r3 = the
// object + 60; Viewport r4, ControllerId r5, Key r6, Delta f1, DeltaTime f2, bGamepad r9). It
// offers each axis event to the GlobalInteractions (interface +16 data, +20 count) in turn and
// calls their InputAxis (+300): the Scaleform GFxInteraction, which moves the menus' pointer by
// the [GFxUI.GamepadMouse] axes, and the player manager that passes it on to PlayerInput
// (0x826149D0). Mouse motion sent here reaches both; sent to PlayerInput it skipped the menus.
constexpr uint32_t kViewportAxisFn = addr::kViewportAxisFn;

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
    if (std::abs(e.dx()) > 3 || std::abs(e.dy()) > 3) {
      rex::glue::NotifyKeyboardMouseInput();
    }
    std::lock_guard<std::mutex> lock(g_bank_mutex);
    g_bank_dx += e.dx();
    g_bank_dy += e.dy();
  }
};

MouseBankListener g_mouse_listener;
PPCFunc* g_input_axis_orig = nullptr;
PPCFunc* g_controller_axis_orig = nullptr;
PPCFunc* g_viewport_axis_orig = nullptr;
std::atomic<bool> g_viewport_hooked{false};
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

// gow3_mouse_aim_log: each distinct caller of UPlayerInput::InputAxis once (return address,
// controller id, key FName), to trace the input chain up to the viewport client.
void MaybeLogCaller(const PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(gow3_mouse_aim_log)) return;
  static std::mutex mutex;
  static uint32_t seen[32];
  static uint32_t seen_count = 0;
  const uint32_t lr = uint32_t(ctx.lr);
  std::lock_guard<std::mutex> lock(mutex);
  for (uint32_t i = 0; i < seen_count; ++i) {
    if (seen[i] == lr) return;
  }
  if (seen_count >= 32) return;
  seen[seen_count++] = lr;
  REXLOG_INFO("MOUSEAIM caller lr={:08X} this={:08X} vtable={:08X} controller={} key={:016X}", lr,
              ctx.r3.u32, LoadGuestU32(base, ctx.r3.u32), ctx.r4.u32, ctx.r5.u64);
}

// Logs each distinct caller of the player-level dispatch, and once the local players'
// PlayerController Interactions by vtable, to see where the Scaleform menus take their input.
void ControllerAxisHook(PPCContext& ctx, uint8_t* base) {
  static std::mutex mutex;
  static uint32_t seen[16];
  static uint32_t seen_count = 0;
  static bool listed = false;
  {
    std::lock_guard<std::mutex> lock(mutex);
    const uint32_t lr = uint32_t(ctx.lr);
    bool known = false;
    for (uint32_t i = 0; i < seen_count; ++i) known |= seen[i] == lr;
    if (!known && seen_count < 16) {
      seen[seen_count++] = lr;
      REXLOG_INFO("MOUSEAIM dispatch lr={:08X} this={:08X} vtable={:08X} controller={} key={:016X}",
                  lr, ctx.r3.u32, LoadGuestU32(base, ctx.r3.u32), ctx.r4.u32, ctx.r5.u64);
    }
    if (!listed) {
      const uint32_t engine = LoadGuestU32(base, kEnginePtr);
      const uint32_t players = engine ? LoadGuestU32(base, engine + 1184) : 0;
      const uint32_t player_count = engine ? LoadGuestU32(base, engine + 1188) : 0;
      for (uint32_t p = 0; players && p < player_count && p < 4; ++p) {
        const uint32_t player = LoadGuestU32(base, players + 4 * p);
        const uint32_t actor = player ? LoadGuestU32(base, player + 64) : 0;
        if (!actor) continue;
        listed = true;
        const uint32_t data = LoadGuestU32(base, actor + 1148);
        const uint32_t count = LoadGuestU32(base, actor + 1152);
        REXLOG_INFO("MOUSEAIM player {} actor={:08X} vtable={:08X} interactions={}", p, actor,
                    LoadGuestU32(base, actor), count);
        for (uint32_t i = 0; data && i < count && i < 16; ++i) {
          const uint32_t object = LoadGuestU32(base, data + 4 * i);
          REXLOG_INFO("MOUSEAIM   interaction {} object={:08X} vtable={:08X}", i, object,
                      object ? LoadGuestU32(base, object) : 0);
        }
      }
    }
  }
  g_controller_axis_orig(ctx, base);
}

// The banked mouse motion for this frame, scaled and signed for the game (y up).
bool TakeMouseMotion(double* x, double* y) {
  if (!rex::cvar::Query<bool>("mnk_mouse")) return false;
  const MouseCounts counts = DrainMouseBank();
  if (counts.dx == 0 && counts.dy == 0) return false;
  const double scale = REXCVAR_GET(gow3_mouse_scale);
  *x = counts.dx * scale;
  *y = -counts.dy * scale;
  if (REXCVAR_GET(gow3_mouse_invert_y)) *y = -*y;
  return true;
}

// One MouseX / MouseY axis event through the original viewport client InputAxis.
bool SendViewportMouseAxis(const PPCContext& ctx, uint8_t* base, uint64_t key, double value) {
  PPCContext call = ctx;
  call.r6.u64 = key;
  call.r9.u64 = 0;  // bGamepad
  call.f1.f64 = double(float(value));
  return SafeInvoke(g_viewport_axis_orig, &call, base);
}

// gow3_mouse_aim_log: the viewport client's GlobalInteractions, once, by vtable.
void MaybeLogGlobalInteractions(const PPCContext& ctx, uint8_t* base) {
  if (!REXCVAR_GET(gow3_mouse_aim_log)) return;
  static std::atomic<bool> listed{false};
  if (listed.exchange(true)) return;
  const uint32_t iface = ctx.r3.u32;
  const uint32_t data = LoadGuestU32(base, iface + 16);
  const uint32_t count = LoadGuestU32(base, iface + 20);
  REXLOG_INFO("MOUSEAIM viewport client {:08X} vtable={:08X} global interactions={}", iface - 60,
              LoadGuestU32(base, iface - 60), count);
  for (uint32_t i = 0; data && i < count && i < 16; ++i) {
    const uint32_t object = LoadGuestU32(base, data + 4 * i);
    REXLOG_INFO("MOUSEAIM   global interaction {} object={:08X} vtable={:08X}", i, object,
                object ? LoadGuestU32(base, object) : 0);
  }
}

void ViewportAxisHook(PPCContext& ctx, uint8_t* base) {
  // Player 1, once per frame (the first axis event that finds banked motion drains it).
  if (!g_injecting && ctx.r5.u32 == 0) {
    MaybeLogGlobalInteractions(ctx, base);
    double x = 0, y = 0;
    if (TakeMouseMotion(&x, &y)) {
      bool sent = true;
      g_injecting = true;
      if (x != 0) sent &= SendViewportMouseAxis(ctx, base, LoadFNameValue(base, kMouseXName), x);
      if (y != 0) sent &= SendViewportMouseAxis(ctx, base, LoadFNameValue(base, kMouseYName), y);
      g_injecting = false;
      MaybeLog(x, y, sent);
    }
  }
  g_viewport_axis_orig(ctx, base);
}

void InputAxisHook(PPCContext& ctx, uint8_t* base) {
  if (!g_injecting) MaybeLogCaller(ctx, base);
  // Fallback when the viewport client hook is not installed: player 1, once per frame.
  if (!g_injecting && ctx.r4.u32 == 0 && !g_viewport_hooked.load(std::memory_order_acquire)) {
    double x = 0, y = 0;
    if (TakeMouseMotion(&x, &y)) {
      bool sent = true;
      g_injecting = true;
      if (x != 0) sent &= SendMouseAxis(ctx, base, LoadFNameValue(base, kMouseXName), x);
      if (y != 0) sent &= SendMouseAxis(ctx, base, LoadFNameValue(base, kMouseYName), y);
      g_injecting = false;
      MaybeLog(x, y, sent);
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
  // A PC layout, for the bindings the player has not set: left click fires (right trigger), right
  // click aims (left trigger), Space is cover / run (A), F melee (B), E interact (X), Q point of
  // interest (Y), R reloads, C is Tac/Com, middle click or Z zooms, Shift is the left stick click,
  // 1-4 are the D-pad weapon slots, Return or Esc pauses (Start) and Tab is Back. WASD and the
  // arrow keys keep the runtime's defaults.
  struct DefaultBind {
    const char* name;
    const char* value;
  };
  for (const DefaultBind& bind : {DefaultBind{"keybind_right_trigger", "LMB"},
                                  DefaultBind{"keybind_left_trigger", "RMB"},
                                  DefaultBind{"keybind_a", "Space"},
                                  DefaultBind{"keybind_b", "F"},
                                  DefaultBind{"keybind_x", "E"},
                                  DefaultBind{"keybind_y", "Q"},
                                  DefaultBind{"keybind_right_shoulder", "R"},
                                  DefaultBind{"keybind_left_shoulder", "C"},
                                  DefaultBind{"keybind_rstick_press", "Z,MMB"},
                                  DefaultBind{"keybind_lstick_press", "Shift"},
                                  DefaultBind{"keybind_dpad_left", "1"},
                                  DefaultBind{"keybind_dpad_right", "2"},
                                  DefaultBind{"keybind_dpad_down", "3"},
                                  DefaultBind{"keybind_dpad_up", "4"},
                                  DefaultBind{"keybind_start", "Return,Escape"},
                                  DefaultBind{"keybind_back", "Tab"}}) {
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
  } else if (!kInputAxisSlot || !kMouseXString || !kMouseXName || !kMouseYName) {
    why = "addresses not mapped for this executable";
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
  if (REXCVAR_GET(gow3_mouse_aim_log) && LoadGuestU32(base, kControllerAxisSlot) == kControllerAxisFn &&
      (g_controller_axis_orig = dispatcher->GetFunction(kControllerAxisFn)) != nullptr) {
    dispatcher->SetFunction(kControllerAxisFn, &ControllerAxisHook);
  }
  if (REXCVAR_GET(gow3_menu_mouse_input) &&
      (g_viewport_axis_orig = dispatcher->GetFunction(kViewportAxisFn)) != nullptr &&
      dispatcher->SetFunction(kViewportAxisFn, &ViewportAxisHook)) {
    g_viewport_hooked.store(true, std::memory_order_release);
  }
  g_hooks_installed.store(true, std::memory_order_release);
  REXLOG_INFO("MOUSEAIM native mouse look installed (UPlayerInput::InputAxis {:08X}; sent through {})",
              kInputAxisFn,
              g_viewport_hooked.load() ? "the viewport client, menus included" : "PlayerInput");
}

}  // namespace rex::glue
