// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_overlay.h"

#include "gow3_button_glyphs.h"
#include "gow3_options_actions.h"
#include "gow3_options_catalog.h"
#include "gow3_options_input.h"
#include "gow3_options_model.h"
#include "gow3_options_values.h"
#include "gow3_options_values_text.h"
#include "gow3_pad_remap.h"
#include "gow3_xam_input_guard.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include <rex/runtime.h>
#include <rex/ui/image_decode.h>
#include <rex/ui/immediate_drawer.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/ui_event.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace rex::glue {
namespace {

using namespace rex::glue::options;

ImFont* g_body_font = nullptr;
ImFont* g_heading_font = nullptr;

// Palette: Crimson Omen / dark steel theme.
constexpr ImU32 kColBackdrop = IM_COL32(6, 7, 10, 218);
constexpr ImU32 kColPanelBg = IM_COL32(16, 18, 23, 248);
constexpr ImU32 kColPanelBorder = IM_COL32(70, 34, 100, 220);
constexpr ImU32 kColHeaderRule = IM_COL32(128, 58, 196, 230);
constexpr ImU32 kColAccent = IM_COL32(136, 62, 210, 255);
constexpr ImU32 kColAccentBright = IM_COL32(176, 112, 246, 255);
constexpr ImU32 kColAccentSoft = IM_COL32(104, 44, 160, 135);
constexpr ImU32 kColRowHover = IM_COL32(30, 24, 40, 220);
constexpr ImU32 kColRowSelected = IM_COL32(44, 26, 66, 235);
constexpr ImU32 kColRowIdle = IM_COL32(22, 25, 32, 195);
constexpr ImU32 kColTextPrimary = IM_COL32(238, 240, 245, 255);
constexpr ImU32 kColTextMuted = IM_COL32(162, 168, 180, 255);
constexpr ImU32 kColTextDim = IM_COL32(118, 124, 136, 255);
constexpr ImU32 kColBadgeNow = IM_COL32(46, 122, 78, 220);
constexpr ImU32 kColBadgeLevel = IM_COL32(168, 118, 32, 230);
constexpr ImU32 kColBadgeRestart = IM_COL32(176, 52, 42, 235);
constexpr ImU32 kColControlBg = IM_COL32(12, 14, 18, 240);
constexpr ImU32 kColControlBorder = IM_COL32(64, 70, 82, 220);

struct HostPadState {
  uint16_t buttons = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;
};

HostPadState ReadHostPad() {
  HostPadState out{};
  auto* rt = rex::Runtime::instance();
  if (!rt || !rt->input_system()) {
    return out;
  }
  auto* input_sys = static_cast<rex::input::InputSystem*>(rt->input_system());
  rex::input::X_INPUT_STATE state{};
  rex::X_RESULT res = X_ERROR_DEVICE_NOT_CONNECTED;
  WithXamInputLock([&]() { res = input_sys->GetState(0, &state); });
  if (res == X_ERROR_SUCCESS) {
    out.buttons = static_cast<uint16_t>(state.gamepad.buttons);
    out.thumb_lx = static_cast<int16_t>(state.gamepad.thumb_lx);
    out.thumb_ly = static_cast<int16_t>(state.gamepad.thumb_ly);
  }
  return out;
}

constexpr std::string_view SingleBitPadName(uint16_t mask) {
  if (mask == 0 || (mask & (mask - 1)) != 0) {
    return {};
  }
  for (const auto& btn : rex::input::pad_remap::kButtons) {
    if (btn.bit == mask) {
      return btn.name;
    }
  }
  return {};
}

void ToggleRuntimeSettings() {
  std::string key_name = "F4";
  if (rex::cvar::GetFlagInfo("bind_settings")) {
    std::string raw = StripSurroundingQuotes(rex::cvar::GetFlagByName("bind_settings"));
    if (!raw.empty()) {
      key_name = std::move(raw);
    }
  }
  rex::ui::VirtualKey vk = rex::ui::ParseVirtualKey(key_name);
  if (vk == rex::ui::VirtualKey::kNone) {
    vk = rex::ui::VirtualKey::kF4;
  }
  rex::ui::KeyEvent ev(nullptr, vk, 1, false, false, false, false, false);
  rex::ui::ProcessKeyEvent(ev);
}

#ifdef _WIN32
bool KeyDown(int vkey) {
  return (GetAsyncKeyState(vkey) & 0x8000) != 0;
}
#endif

RawInput SampleRawInput(const ImGuiIO& io, bool menu_open) {
  RawInput r{};
  // Reading the pad also drains the keyboard/mouse driver's accumulated mouse motion, which the
  // game reads as the right stick. While the menu is closed and mouse look is on, leave it to the
  // game (the Back + Start chord is then unavailable; F10 still opens the menu).
  const bool mouse_look = rex::cvar::Query<bool>("mnk_mode") && rex::cvar::Query<bool>("mnk_mouse");
  const HostPadState pad = (menu_open || !mouse_look) ? ReadHostPad() : HostPadState{};
  r.pad_buttons = pad.buttons;
  r.thumb_lx = pad.thumb_lx;
  r.thumb_ly = pad.thumb_ly;

#ifdef _WIN32
  HWND fg = GetForegroundWindow();
  DWORD fg_pid = 0;
  if (fg) {
    GetWindowThreadProcessId(fg, &fg_pid);
  }
  const bool focused = (fg_pid == GetCurrentProcessId());
  if (focused) {
    r.key_f10 = KeyDown(VK_F10);
    r.key_escape = KeyDown(VK_ESCAPE);
    r.key_up = KeyDown(VK_UP);
    r.key_down = KeyDown(VK_DOWN);
    r.key_left = KeyDown(VK_LEFT);
    r.key_right = KeyDown(VK_RIGHT);
    r.key_enter = KeyDown(VK_RETURN) || KeyDown(VK_SPACE);
    r.key_tab = KeyDown(VK_TAB);
    r.key_shift = KeyDown(VK_SHIFT);
    r.key_delete = KeyDown(VK_DELETE);
    r.key_backspace = KeyDown(VK_BACK);
  }
#endif

  r.key_f10 = r.key_f10 || ImGui::IsKeyDown(ImGuiKey_F10);
  r.key_escape = r.key_escape || ImGui::IsKeyDown(ImGuiKey_Escape);
  r.key_up = r.key_up || ImGui::IsKeyDown(ImGuiKey_UpArrow);
  r.key_down = r.key_down || ImGui::IsKeyDown(ImGuiKey_DownArrow);
  r.key_left = r.key_left || ImGui::IsKeyDown(ImGuiKey_LeftArrow);
  r.key_right = r.key_right || ImGui::IsKeyDown(ImGuiKey_RightArrow);
  r.key_enter = r.key_enter || ImGui::IsKeyDown(ImGuiKey_Enter) ||
                ImGui::IsKeyDown(ImGuiKey_KeypadEnter) || ImGui::IsKeyDown(ImGuiKey_Space);
  r.key_tab = r.key_tab || ImGui::IsKeyDown(ImGuiKey_Tab);
  r.key_shift = r.key_shift || io.KeyShift;
  r.key_delete = r.key_delete || ImGui::IsKeyDown(ImGuiKey_Delete);
  r.key_backspace = r.key_backspace || ImGui::IsKeyDown(ImGuiKey_Backspace);
  r.text_input_active = io.WantTextInput;
  return r;
}

bool IsTabPage(Page id) {
  return id != Page::kPadButtons && id != Page::kKeyBindings;
}

// Sub-pages opened from the Controls tab.
bool IsControlsSubPage(Page id) {
  return id == Page::kPadButtons || id == Page::kKeyBindings;
}

#ifdef _WIN32
// Gears of War 3 recomp: the first key or mouse button down, as a keybind name ("" if none).
// Shift, Ctrl and Alt bind as keys under their generic names (the left / right variants are
// skipped: the keyboard/mouse driver records the generic ones); Escape (cancel), F10 and the
// Windows keys are skipped.
std::string FirstBindableKeyDown() {
  for (int vk = 0x01; vk <= 0xFE; ++vk) {
    if (vk == VK_ESCAPE || vk == VK_F10 || (vk >= VK_LSHIFT && vk <= VK_RMENU) || vk == VK_LWIN ||
        vk == VK_RWIN) {
      continue;
    }
    if (!(GetAsyncKeyState(vk) & 0x8000)) {
      continue;
    }
    std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
    if (!name.empty()) {
      return name;
    }
  }
  return {};
}
#endif

std::vector<std::string> SplitKeys(std::string_view val) {
  std::vector<std::string> keys;
  size_t start = 0;
  while (start < val.size()) {
    size_t comma = val.find(',', start);
    if (comma == std::string_view::npos) {
      comma = val.size();
    }
    std::string_view part = val.substr(start, comma - start);
    while (!part.empty() && (part.front() == ' ' || part.front() == '\t')) {
      part.remove_prefix(1);
    }
    while (!part.empty() && (part.back() == ' ' || part.back() == '\t')) {
      part.remove_suffix(1);
    }
    if (!part.empty()) {
      keys.emplace_back(part);
    }
    start = comma + 1;
  }
  return keys;
}

std::string JoinKeys(const std::vector<std::string>& keys) {
  std::string result;
  for (size_t i = 0; i < keys.size(); ++i) {
    if (i > 0) result += ",";
    result += keys[i];
  }
  return result;
}

std::string GetButtonDisplayName(std::string_view btn, const std::string& style) {
  const bool ps = (style == "playstation");
  if (btn == "a") return ps ? "Cross" : "A";
  if (btn == "b") return ps ? "Circle" : "B";
  if (btn == "x") return ps ? "Square" : "X";
  if (btn == "y") return ps ? "Triangle" : "Y";
  if (btn == "lb") return ps ? "L1" : "LB";
  if (btn == "rb") return ps ? "R1" : "RB";
  if (btn == "ls") return ps ? "L3" : "LS";
  if (btn == "rs") return ps ? "R3" : "RS";
  if (btn == "start" || btn == "options") return ps ? "Options" : "Start";
  if (btn == "back" || btn == "create" || btn == "share") return ps ? "Create" : "Back";
  if (btn == "dpad_up") return "D-Pad Up";
  if (btn == "dpad_down") return "D-Pad Down";
  if (btn == "dpad_left") return "D-Pad Left";
  if (btn == "dpad_right") return "D-Pad Right";
  if (btn.empty() || btn == "none") return "—";
  return std::string(btn);
}

std::string GetKeyDisplayName(std::string_view key) {
  if (key.empty() || key == "none") return "—";
  if (key == "Space") return "Space";
  if (key == "Return") return "Enter";
  if (key == "Escape") return "Esc";
  if (key == "LMB") return "Left Click";
  if (key == "RMB") return "Right Click";
  if (key == "MMB") return "Middle Click";
  return std::string(key);
}

class OptionsOverlayDialog final : public rex::ui::ImGuiDialog {
 public:
  OptionsOverlayDialog(rex::ui::ImGuiDrawer* drawer, rex::PathConfig paths,
                       rex::ui::ImmediateDrawer* immediate_drawer = nullptr)
      : rex::ui::ImGuiDialog(drawer),
        paths_(std::move(paths)),
        immediate_drawer_(immediate_drawer),
        model_(CreateLiveModel(paths_)) {
    SyncTextBuffersFromModel();
  }

  ~OptionsOverlayDialog() override {
    CloseForShutdown();
    glyph_cache_.clear();
  }

  void CloseForShutdown() {
    if (open_) {
      FlushTextBuffersToModel();
      SaveConfigToDisk(paths_.config_path, model_);
      rex::cvar::SetFlagByName("xam_input_block", "false");
      open_ = false;
    }
  }

 protected:
  void OnDraw(ImGuiIO& io) override {
    const RawInput raw = SampleRawInput(io, open_);

    // The game gets its input back once the keys that can close the menu are released: Esc is
    // also Start, and handing it over while still held paused the game as the menu closed.
    if (!open_ && unblock_pending_ && !raw.key_escape && !raw.key_enter && !raw.key_f10) {
      unblock_pending_ = false;
      rex::cvar::SetFlagByName("xam_input_block", "false");
    }

    // Handle the active keyboard / mouse or controller capture on Page::kKeyBindings / Page::kPadButtons
    if (open_ && !capture_cvar_.empty()) {
      if (raw.key_escape && !prev_capture_esc_) {
        capture_cvar_.clear();
      } else if (capture_is_pad_) {
        const uint16_t newly_pressed =
            static_cast<uint16_t>(raw.pad_buttons & ~prev_capture_pad_);
        if (!capture_armed_) {
          capture_armed_ = (raw.pad_buttons == 0);
        } else {
          const std::string btn_name = ResolvePhysicalButtonName(newly_pressed);
          if (!btn_name.empty()) {
            ApplyPadBinding(capture_cvar_, capture_slot_, btn_name, CurrentLang());
            capture_cvar_.clear();
          }
        }
      } else {
#ifdef _WIN32
        HWND fg = GetForegroundWindow();
        DWORD fg_pid = 0;
        if (fg) {
          GetWindowThreadProcessId(fg, &fg_pid);
        }
        const std::string key =
            fg_pid == GetCurrentProcessId() ? FirstBindableKeyDown() : std::string();
        if (!capture_armed_) {
          capture_armed_ = key.empty();
        } else if (!key.empty()) {
          ApplyKeyBinding(capture_cvar_, capture_slot_, key);
          capture_cvar_.clear();
        }
#else
        capture_cvar_.clear();
#endif
      }
      prev_capture_pad_ = raw.pad_buttons;
      prev_capture_esc_ = raw.key_escape;
      // Eat navigation edges while capturing.
      (void)input_.Step(raw, open_);
    } else {
      prev_capture_pad_ = raw.pad_buttons;
      prev_capture_esc_ = raw.key_escape;

      const MenuEdges edges = input_.Step(raw, open_);
      if (edges.toggle_open) {
        SetMenuOpen(!open_);
      }
      if (open_) {
        HandleNavigationEdges(edges);
      }
    }

    if (!open_) {
      return;
    }

    DrawOverlay(io);
  }

 private:
  void SetMenuOpen(bool open) {
    if (open_ == open) {
      return;
    }
    open_ = open;
    if (open_) {
      unblock_pending_ = false;
      rex::cvar::SetFlagByName("xam_input_block", "true");
      model_.RefreshFromReader();
      RefreshDynamicRowChoices(model_, paths_);
      SyncTextBuffersFromModel();
      ClampSelectedRow();
      selected_col_ = 0;
    } else {
      capture_cvar_.clear();
      FlushTextBuffersToModel();
      SaveConfigToDisk(paths_.config_path, model_);
      unblock_pending_ = true;  // OnDraw unblocks once the closing key is released
    }
  }

  void SyncTextBuffersFromModel() {
    for (const auto& page : Catalog()) {
      for (const auto& row : page.rows) {
        if (row.control == Control::kText) {
          std::array<char, 256> buf{};
          std::string val = model_.Get(row.cvar);
          std::snprintf(buf.data(), buf.size(), "%s", val.c_str());
          text_buffers_[row.cvar] = buf;
        }
      }
    }
  }

  void FlushTextBuffersToModel() {
    for (const auto& [cvar, buf] : text_buffers_) {
      std::string val(buf.data());
      if (val != model_.Get(cvar)) {
        model_.Set(cvar, val);
      }
    }
  }

  const PageSpec& ActivePageSpec() const {
    const auto& pages = Catalog();
    for (const auto& p : pages) {
      if (p.id == active_page_) {
        return p;
      }
    }
    return pages.front();
  }

  std::vector<const RowSpec*> VisibleRows(const PageSpec& page) const {
    std::vector<const RowSpec*> rows;
    for (const auto& r : page.rows) {
      if (model_.IsAvailable(r.cvar)) {
        rows.push_back(&r);
      }
    }
    return rows;
  }

  void ClampSelectedRow() {
    auto rows = VisibleRows(ActivePageSpec());
    if (rows.empty()) {
      selected_row_ = 0;
    } else if (selected_row_ >= static_cast<int>(rows.size())) {
      selected_row_ = static_cast<int>(rows.size()) - 1;
    } else if (selected_row_ < 0) {
      selected_row_ = 0;
    }
  }

  void SwitchTab(int delta) {
    std::vector<Page> tabs;
    for (const auto& p : Catalog()) {
      if (IsTabPage(p.id)) {
        tabs.push_back(p.id);
      }
    }
    if (tabs.empty()) {
      return;
    }
    Page current_tab = IsControlsSubPage(active_page_) ? Page::kControls : active_page_;
    int idx = 0;
    for (size_t i = 0; i < tabs.size(); ++i) {
      if (tabs[i] == current_tab) {
        idx = static_cast<int>(i);
        break;
      }
    }
    int n = static_cast<int>(tabs.size());
    int next = ((idx + delta) % n + n) % n;
    active_page_ = tabs[static_cast<size_t>(next)];
    selected_row_ = 0;
    scroll_to_selected_ = true;
  }

  void ActivateRow(const RowSpec& row) {
    if (row.control == Control::kAction) {
      if (std::string_view(row.cvar) == "pad_buttons") {
        active_page_ = Page::kPadButtons;
        selected_row_ = 0;
        selected_col_ = 0;
        scroll_to_selected_ = true;
      } else if (std::string_view(row.cvar) == "key_bindings") {
        active_page_ = Page::kKeyBindings;
        selected_row_ = 0;
        selected_col_ = 0;
        scroll_to_selected_ = true;
      }
      return;
    }
    if (IsControlsSubPage(row.page)) {
      StartCapture(row, selected_col_);
      return;
    }
    model_.Toggle(row.cvar);
    SaveConfigToDisk(paths_.config_path, model_);
  }

  void HandleNavigationEdges(const MenuEdges& edges) {
    if (edges.cancel) {
      if (IsControlsSubPage(active_page_)) {
        active_page_ = Page::kControls;
        selected_row_ = 0;
        selected_col_ = 0;
        scroll_to_selected_ = true;
      } else {
        SetMenuOpen(false);
      }
      return;
    }

    if (edges.switch_page != 0) {
      SwitchTab(edges.switch_page);
      selected_col_ = 0;
    }

    auto rows = VisibleRows(ActivePageSpec());
    if (rows.empty()) {
      return;
    }
    ClampSelectedRow();

    if (edges.move_row != 0) {
      int n = static_cast<int>(rows.size());
      selected_row_ = ((selected_row_ + edges.move_row) % n + n) % n;
      scroll_to_selected_ = true;
    }

    const RowSpec& sel = *rows[static_cast<size_t>(selected_row_)];
    if (edges.adjust != 0) {
      if (active_page_ == Page::kKeyBindings || active_page_ == Page::kPadButtons) {
        if (edges.adjust < 0) {
          selected_col_ = 0;
        } else if (edges.adjust > 0) {
          selected_col_ = 1;
        }
      } else {
        if (sel.control == Control::kToggle) {
          model_.Toggle(sel.cvar);
          SaveConfigToDisk(paths_.config_path, model_);
        } else if (sel.control == Control::kChoice) {
          model_.StepChoice(sel.cvar, edges.adjust);
          SaveConfigToDisk(paths_.config_path, model_);
        } else if (sel.control == Control::kSliderInt || sel.control == Control::kSliderFloat) {
          model_.StepSlider(sel.cvar, edges.adjust);
          SaveConfigToDisk(paths_.config_path, model_);
        }
      }
    }

    if (edges.confirm) {
      if (IsControlsSubPage(active_page_)) {
        StartCapture(sel, selected_col_);
      } else {
        ActivateRow(sel);
      }
    }

    if (edges.clear_slot) {
      if (IsControlsSubPage(active_page_)) {
        ClearSlot(sel, selected_col_, CurrentLang());
      }
    }
  }

  void DrawApplyBadge(Apply apply, bool modified, Lang lang, float scale) {
    const char* text = UiText("apply_now").For(lang);
    ImU32 bg = kColBadgeNow;
    if (apply == Apply::kNextLevel) {
      text = UiText("apply_level").For(lang);
      bg = modified ? kColBadgeLevel : IM_COL32(72, 60, 32, 165);
    } else if (apply == Apply::kRestart) {
      text = UiText("apply_restart").For(lang);
      bg = modified ? kColBadgeRestart : IM_COL32(82, 38, 36, 175);
    } else {
      bg = modified ? kColBadgeNow : IM_COL32(34, 68, 48, 165);
    }

    ImVec2 ts = ImGui::CalcTextSize(text);
    float pad_x = 8.0f * scale;
    float pad_y = 3.0f * scale;
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 sz(ts.x + pad_x * 2.0f, ts.y + pad_y * 2.0f);
    ImGui::GetWindowDrawList()->AddRectFilled(pos, ImVec2(pos.x + sz.x, pos.y + sz.y), bg,
                                              4.0f * scale);
    ImGui::GetWindowDrawList()->AddText(ImVec2(pos.x + pad_x, pos.y + pad_y), kColTextPrimary,
                                        text);
    ImGui::Dummy(sz);
  }

  void DrawToggleWidget(const RowSpec& row, Lang lang, float width, float height, float scale) {
    const bool on = model_.GetBool(row.cvar);
    const char* label = on ? UiText("on").For(lang) : UiText("off").For(lang);

    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    std::string btn_id = std::string("##toggle_") + row.cvar;
    if (ImGui::InvisibleButton(btn_id.c_str(), ImVec2(width, height))) {
      model_.Toggle(row.cvar);
      SaveConfigToDisk(paths_.config_path, model_);
    }
    const bool hovered = ImGui::IsItemHovered();

    ImU32 bg = on ? (hovered ? kColAccentBright : kColAccent) : kColControlBg;
    ImU32 border = on ? kColAccentBright : (hovered ? kColAccent : kColControlBorder);
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), bg, 4.0f * scale);
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), border, 4.0f * scale, 0,
                1.2f * scale);

    ImVec2 ts = ImGui::CalcTextSize(label);
    dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f),
                kColTextPrimary, label);
  }

  void DrawChoiceWidget(const RowSpec& row, Lang lang, float width, float height, float scale) {
    auto choices = model_.ChoicesFor(row, lang);
    std::string cur = model_.Get(row.cvar);
    std::string cur_label = cur;
    for (const auto& c : choices) {
      if (c.value == cur) {
        cur_label = c.label;
        break;
      }
    }

    const float arrow_w = 30.0f * scale;
    const float box_w = std::max(80.0f * scale, width - arrow_w * 2.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 base = ImGui::GetCursorScreenPos();

    // Left arrow "<"
    std::string prev_id = std::string("##prev_") + row.cvar;
    if (ImGui::InvisibleButton(prev_id.c_str(), ImVec2(arrow_w, height))) {
      model_.StepChoice(row.cvar, -1);
      SaveConfigToDisk(paths_.config_path, model_);
    }
    bool prev_hov = ImGui::IsItemHovered();
    dl->AddRectFilled(base, ImVec2(base.x + arrow_w, base.y + height),
                      prev_hov ? kColAccent : kColControlBg, 4.0f * scale);
    dl->AddRect(base, ImVec2(base.x + arrow_w, base.y + height), kColControlBorder, 4.0f * scale);
    ImVec2 lt_sz = ImGui::CalcTextSize("<");
    dl->AddText(ImVec2(base.x + (arrow_w - lt_sz.x) * 0.5f, base.y + (height - lt_sz.y) * 0.5f),
                kColTextPrimary, "<");

    // Center value box (clicking cycles forward)
    ImGui::SameLine(0.0f, 0.0f);
    ImVec2 mid = ImGui::GetCursorScreenPos();
    std::string mid_id = std::string("##mid_") + row.cvar;
    if (ImGui::InvisibleButton(mid_id.c_str(), ImVec2(box_w, height))) {
      model_.StepChoice(row.cvar, +1);
      SaveConfigToDisk(paths_.config_path, model_);
    }
    bool mid_hov = ImGui::IsItemHovered();
    dl->AddRectFilled(mid, ImVec2(mid.x + box_w, mid.y + height),
                      mid_hov ? IM_COL32(28, 32, 42, 245) : kColControlBg, 0.0f);
    dl->AddRect(mid, ImVec2(mid.x + box_w, mid.y + height), kColControlBorder, 0.0f);

    dl->PushClipRect(ImVec2(mid.x + 6.0f * scale, mid.y),
                     ImVec2(mid.x + box_w - 6.0f * scale, mid.y + height), true);
    ImVec2 val_sz = ImGui::CalcTextSize(cur_label.c_str());
    float text_x = mid.x + std::max(6.0f * scale, (box_w - val_sz.x) * 0.5f);
    dl->AddText(ImVec2(text_x, mid.y + (height - val_sz.y) * 0.5f), kColTextPrimary,
                cur_label.c_str());
    dl->PopClipRect();

    // Right arrow ">"
    ImGui::SameLine(0.0f, 0.0f);
    ImVec2 rt = ImGui::GetCursorScreenPos();
    std::string next_id = std::string("##next_") + row.cvar;
    if (ImGui::InvisibleButton(next_id.c_str(), ImVec2(arrow_w, height))) {
      model_.StepChoice(row.cvar, +1);
      SaveConfigToDisk(paths_.config_path, model_);
    }
    bool next_hov = ImGui::IsItemHovered();
    dl->AddRectFilled(rt, ImVec2(rt.x + arrow_w, rt.y + height),
                      next_hov ? kColAccent : kColControlBg, 4.0f * scale);
    dl->AddRect(rt, ImVec2(rt.x + arrow_w, rt.y + height), kColControlBorder, 4.0f * scale);
    ImVec2 gt_sz = ImGui::CalcTextSize(">");
    dl->AddText(ImVec2(rt.x + (arrow_w - gt_sz.x) * 0.5f, rt.y + (height - gt_sz.y) * 0.5f),
                kColTextPrimary, ">");
  }

  void DrawSliderWidget(const RowSpec& row, float width, float height, float scale) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();

    std::string id = std::string("##slider_") + row.cvar;
    ImGui::InvisibleButton(id.c_str(), ImVec2(width, height));
    if (ImGui::IsItemActive()) {
      float t = (ImGui::GetIO().MousePos.x - pos.x) / std::max(1.0f, width);
      model_.SetSliderNormalized(row.cvar, static_cast<double>(t));
      SaveConfigToDisk(paths_.config_path, model_);
    }

    const double norm = model_.SliderNormalized(row.cvar);
    const float fill_w = static_cast<float>(norm) * width;

    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height), kColControlBg, 4.0f * scale);
    if (fill_w > 1.0f) {
      dl->AddRectFilled(pos, ImVec2(pos.x + fill_w, pos.y + height), kColAccent, 4.0f * scale);
    }
    dl->AddRect(pos, ImVec2(pos.x + width, pos.y + height), kColControlBorder, 4.0f * scale);

    std::string val_text = model_.Get(row.cvar);
    if (row.unit && *row.unit) {
      val_text += row.unit;
    }
    ImVec2 ts = ImGui::CalcTextSize(val_text.c_str());
    dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f),
                kColTextPrimary, val_text.c_str());
  }

  void DrawTextWidget(const RowSpec& row, float width, float height, float scale) {
    auto& buf = text_buffers_[row.cvar];
    ImGui::PushItemWidth(width);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f * scale, (height - ImGui::GetFontSize()) * 0.5f));
    ImGui::PushStyleColor(ImGuiCol_FrameBg, ImGui::ColorConvertU32ToFloat4(kColControlBg));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(0.16f, 0.18f, 0.23f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, ImVec4(0.18f, 0.13f, 0.26f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kColControlBorder));

    std::string id = std::string("##text_") + row.cvar;
    if (ImGui::InputText(id.c_str(), buf.data(), buf.size())) {
      model_.Set(row.cvar, buf.data());
      SaveConfigToDisk(paths_.config_path, model_);
    }
    if (ImGui::IsItemDeactivatedAfterEdit()) {
      model_.Set(row.cvar, buf.data());
      SaveConfigToDisk(paths_.config_path, model_);
    }

    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar();
    ImGui::PopItemWidth();
  }

  rex::ui::ImmediateTexture* GetOrLoadGlyphTexture(const std::string& style,
                                                   const std::string& name) {
    if (name.empty() || name == "none" || !immediate_drawer_) {
      return nullptr;
    }
    std::string cache_key = style + ":" + name;
    auto it = glyph_cache_.find(cache_key);
    if (it != glyph_cache_.end()) {
      return it->second.get();
    }

    std::filesystem::path path = FindGlyphFile(style, name);
    if (path.empty()) {
      glyph_cache_[cache_key] = nullptr;
      return nullptr;
    }

    std::ifstream f(path, std::ios::binary);
    if (!f) {
      glyph_cache_[cache_key] = nullptr;
      return nullptr;
    }
    f.seekg(0, std::ios::end);
    const size_t size = static_cast<size_t>(f.tellg());
    if (size == 0) {
      glyph_cache_[cache_key] = nullptr;
      return nullptr;
    }
    f.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(size);
    if (!f.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size))) {
      glyph_cache_[cache_key] = nullptr;
      return nullptr;
    }

    int w = 0, h = 0;
    std::vector<uint8_t> rgba = rex::ui::DecodeImageRGBA(bytes.data(), bytes.size(), w, h);
    if (rgba.empty() || w <= 0 || h <= 0) {
      glyph_cache_[cache_key] = nullptr;
      return nullptr;
    }

    auto tex = immediate_drawer_->CreateTexture(
        static_cast<uint32_t>(w), static_cast<uint32_t>(h),
        rex::ui::ImmediateTextureFilter::kLinear, false, rgba.data());
    rex::ui::ImmediateTexture* raw = tex.get();
    glyph_cache_[cache_key] = std::move(tex);
    return raw;
  }

  void StartCapture(const RowSpec& row, int slot) {
    FlushTextBuffersToModel();
    capture_cvar_ = row.cvar;
    capture_slot_ = slot;
    capture_is_pad_ = (row.page == Page::kPadButtons);
    capture_armed_ = false;
  }

  void ApplyKeyBinding(const std::string& cvar, int slot, const std::string& key) {
    std::string cur = model_.Get(cvar);
    std::vector<std::string> keys = SplitKeys(cur);
    if (slot == 0) {
      if (keys.empty()) {
        keys.push_back(key);
      } else {
        keys[0] = key;
      }
    } else {
      if (keys.empty()) {
        keys.push_back(key);
      } else if (keys.size() == 1) {
        keys.push_back(key);
      } else {
        keys[1] = key;
      }
    }
    std::string joined = JoinKeys(keys);
    model_.Set(cvar, joined);
    SaveConfigToDisk(paths_.config_path, model_);
    SyncTextBuffersFromModel();
    NotifyBindingsChanged();
  }

  void ClearKeyBinding(const std::string& cvar, int slot) {
    std::string cur = model_.Get(cvar);
    std::vector<std::string> keys = SplitKeys(cur);
    if (slot == 0) {
      if (keys.size() <= 1) {
        keys.clear();
      } else {
        keys.erase(keys.begin());
      }
    } else if (slot == 1) {
      if (keys.size() >= 2) {
        keys.erase(keys.begin() + 1);
      }
    }
    std::string joined = JoinKeys(keys);
    model_.Set(cvar, joined);
    SaveConfigToDisk(paths_.config_path, model_);
    SyncTextBuffersFromModel();
    NotifyBindingsChanged();
  }

  std::string ResolvePhysicalButtonName(uint16_t mask) const {
    if (mask == 0 || (mask & (mask - 1)) != 0) {
      return {};
    }
    for (const auto& btn : rex::input::pad_remap::kButtons) {
      if (btn.bit == mask) {
        return std::string(btn.name);
      }
    }
    return {};
  }

  void ApplyPadBinding(const std::string& cvar, int slot, const std::string& btn, Lang lang) {
    if (btn.empty() || btn == "none" || slot < 0 || slot > 1) {
      return;
    }

    // Find if 'btn' is currently bound in any pad mapping row and slot.
    std::string moved_from_cvar;
    int moved_from_slot = -1;
    std::string moved_from_label;

    for (const auto& page : Catalog()) {
      if (page.id == Page::kPadButtons) {
        for (const auto& r : page.rows) {
          std::vector<std::string> r_keys = SplitKeys(model_.Get(r.cvar));
          for (size_t s = 0; s < r_keys.size() && s < 2; ++s) {
            if (r_keys[s] == btn) {
              moved_from_cvar = r.cvar;
              moved_from_slot = static_cast<int>(s);
              moved_from_label = r.label.For(lang);
              break;
            }
          }
          if (!moved_from_cvar.empty()) {
            break;
          }
        }
        break;
      }
    }

    if (moved_from_cvar == cvar && moved_from_slot == slot) {
      return;
    }

    // Target action label.
    std::string target_label;
    for (const auto& page : Catalog()) {
      if (page.id == Page::kPadButtons) {
        for (const auto& r : page.rows) {
          if (r.cvar == cvar) {
            target_label = r.label.For(lang);
            break;
          }
        }
        break;
      }
    }

    // Current keys in target cvar.
    std::vector<std::string> target_keys = SplitKeys(model_.Get(cvar));
    std::string old_target_btn;
    if (slot == 0) {
      if (!target_keys.empty() && target_keys[0] != "none") {
        old_target_btn = target_keys[0];
      }
    } else if (slot == 1) {
      if (target_keys.size() > 1 && target_keys[1] != "none") {
        old_target_btn = target_keys[1];
      }
    }

    if (moved_from_cvar == cvar) {
      // Swapping slots within the same row.
      std::string slots[2] = {"none", "none"};
      if (!target_keys.empty()) slots[0] = target_keys[0];
      if (target_keys.size() > 1) slots[1] = target_keys[1];

      slots[moved_from_slot] = old_target_btn.empty() ? "none" : old_target_btn;
      slots[slot] = btn;

      std::vector<std::string> new_keys;
      if (slots[0] != "none" && !slots[0].empty()) new_keys.push_back(slots[0]);
      if (slots[1] != "none" && !slots[1].empty()) new_keys.push_back(slots[1]);
      model_.Set(cvar, new_keys.empty() ? "none" : JoinKeys(new_keys));
    } else {
      if (!moved_from_cvar.empty()) {
        std::vector<std::string> moved_keys = SplitKeys(model_.Get(moved_from_cvar));
        if (moved_from_slot < static_cast<int>(moved_keys.size())) {
          if (!old_target_btn.empty()) {
            moved_keys[static_cast<size_t>(moved_from_slot)] = old_target_btn;
          } else {
            if (moved_from_slot == 1) {
              moved_keys.erase(moved_keys.begin() + 1);
            } else {
              if (moved_keys.size() > 1) {
                moved_keys.erase(moved_keys.begin());
              } else {
                moved_keys.clear();
              }
            }
          }
        }
        model_.Set(moved_from_cvar, moved_keys.empty() ? "none" : JoinKeys(moved_keys));
      }

      // Update target cvar.
      if (slot == 0) {
        if (target_keys.empty()) {
          target_keys.push_back(btn);
        } else {
          target_keys[0] = btn;
        }
      } else if (slot == 1) {
        if (target_keys.empty()) {
          target_keys.push_back(btn);
        } else if (target_keys.size() == 1) {
          target_keys.push_back(btn);
        } else {
          target_keys[1] = btn;
        }
      }
      model_.Set(cvar, target_keys.empty() ? "none" : JoinKeys(target_keys));
    }

    SaveConfigToDisk(paths_.config_path, model_);
    SyncTextBuffersFromModel();

    std::string style = GetActiveControllerGlyphStyle();
    std::string btn_display = GetButtonDisplayName(btn, style);
    if (!moved_from_label.empty()) {
      status_message_ = "Moved " + btn_display + " from " + moved_from_label + " to " + target_label;
    } else {
      status_message_ = "Bound " + btn_display + " to " + target_label;
    }
    status_time_ = std::chrono::steady_clock::now();
  }

  void ClearPadBinding(const std::string& cvar, int slot, Lang lang) {
    std::string cur = model_.Get(cvar);
    std::vector<std::string> keys = SplitKeys(cur);
    if (slot == 0) {
      if (keys.size() <= 1) {
        keys.clear();
      } else {
        keys.erase(keys.begin());
      }
    } else if (slot == 1) {
      if (keys.size() >= 2) {
        keys.erase(keys.begin() + 1);
      }
    }
    std::string joined = keys.empty() ? "none" : JoinKeys(keys);
    model_.Set(cvar, joined);
    SaveConfigToDisk(paths_.config_path, model_);
    SyncTextBuffersFromModel();

    std::string target_label;
    for (const auto& page : Catalog()) {
      if (page.id == Page::kPadButtons) {
        for (const auto& r : page.rows) {
          if (r.cvar == cvar) {
            target_label = r.label.For(lang);
            break;
          }
        }
        break;
      }
    }
    if (!target_label.empty()) {
      status_message_ = "Cleared " + target_label + (slot == 1 ? " (Secondary)" : " (Primary)");
      status_time_ = std::chrono::steady_clock::now();
    }
  }

  void ClearSlot(const RowSpec& row, int slot, Lang lang) {
    if (row.page == Page::kKeyBindings) {
      ClearKeyBinding(row.cvar, slot);
    } else if (row.page == Page::kPadButtons) {
      ClearPadBinding(row.cvar, slot, lang);
    }
  }

  void DrawBindingsRowWidget(const RowSpec& row, int row_index, Lang lang,
                             float ctrl_w, float ctrl_h, float scale) {
    const float gap = 8.0f * scale;
    const float slot_w = (ctrl_w - gap) * 0.5f;
    const float slot_h = ctrl_h;
    const ImVec2 base_pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    const bool is_row_selected = (row_index == selected_row_);
    const std::string ctrl_style = GetActiveControllerGlyphStyle();

    std::string bound[2];
    std::string display[2];
    rex::ui::ImmediateTexture* tex[2] = {nullptr, nullptr};

    if (row.page == Page::kKeyBindings) {
      std::vector<std::string> keys = SplitKeys(model_.Get(row.cvar));
      if (!keys.empty()) bound[0] = keys[0];
      if (keys.size() > 1) bound[1] = keys[1];

      for (int s = 0; s < 2; ++s) {
        display[s] = GetKeyDisplayName(bound[s]);
        if (!bound[s].empty()) {
          tex[s] = GetOrLoadGlyphTexture("keyboard", bound[s]);
        }
      }
    } else {
      std::vector<std::string> keys = SplitKeys(model_.Get(row.cvar));
      if (!keys.empty() && keys[0] != "none") bound[0] = keys[0];
      if (keys.size() > 1 && keys[1] != "none") bound[1] = keys[1];

      for (int s = 0; s < 2; ++s) {
        display[s] = GetButtonDisplayName(bound[s], ctrl_style);
        if (!bound[s].empty() && bound[s] != "none") {
          tex[s] = GetOrLoadGlyphTexture(ctrl_style, bound[s]);
        }
      }
    }

    for (int col = 0; col < 2; ++col) {
      const ImVec2 s_pos(base_pos.x + col * (slot_w + gap), base_pos.y);
      const ImVec2 s_end(s_pos.x + slot_w, s_pos.y + slot_h);

      ImGui::SetCursorScreenPos(s_pos);
      std::string btn_id = std::string("##slot_") + std::to_string(col) + "_" + row.cvar;
      if (ImGui::InvisibleButton(btn_id.c_str(), ImVec2(slot_w, slot_h))) {
        selected_row_ = row_index;
        selected_col_ = col;
        StartCapture(row, selected_col_);
      }

      const bool slot_hovered = ImGui::IsItemHovered();
      const bool slot_focused = is_row_selected && (selected_col_ == col);

      ImU32 slot_bg = slot_focused ? kColRowSelected : (slot_hovered ? kColRowHover : kColControlBg);
      ImU32 slot_border = slot_focused ? kColAccentBright : (slot_hovered ? kColAccent : kColControlBorder);
      dl->AddRectFilled(s_pos, s_end, slot_bg, 4.0f * scale);
      dl->AddRect(s_pos, s_end, slot_border, 4.0f * scale, 0, slot_focused ? 1.8f * scale : 1.0f * scale);

      dl->PushClipRect(ImVec2(s_pos.x + 4.0f * scale, s_pos.y),
                       ImVec2(s_end.x - 4.0f * scale, s_end.y), true);

      if (tex[col] != nullptr) {
        float aspect = (tex[col]->height > 0)
                           ? (static_cast<float>(tex[col]->width) / static_cast<float>(tex[col]->height))
                           : 1.0f;
        float glyph_h = std::clamp(slot_h - 6.0f * scale, 24.0f * scale, 30.0f * scale);
        float glyph_w = glyph_h * aspect;
        if (glyph_w > slot_w - 12.0f * scale) {
          glyph_w = slot_w - 12.0f * scale;
          glyph_h = glyph_w / aspect;
        }
        float img_x = s_pos.x + (slot_w - glyph_w) * 0.5f;
        float img_y = s_pos.y + (slot_h - glyph_h) * 0.5f;

        dl->AddImage(reinterpret_cast<ImTextureID>(tex[col]),
                     ImVec2(img_x, img_y), ImVec2(img_x + glyph_w, img_y + glyph_h));
      } else if (bound[col].empty() || display[col] == "—") {
        const float dash_w = 14.0f * scale;
        const float mid_x = s_pos.x + slot_w * 0.5f;
        const float mid_y = s_pos.y + slot_h * 0.5f;
        dl->AddLine(ImVec2(mid_x - dash_w * 0.5f, mid_y),
                    ImVec2(mid_x + dash_w * 0.5f, mid_y),
                    kColTextDim, 2.0f * scale);
      } else {
        ImVec2 txt_sz = ImGui::CalcTextSize(display[col].c_str());
        float txt_x = s_pos.x + (slot_w - txt_sz.x) * 0.5f;
        float txt_y = s_pos.y + (slot_h - txt_sz.y) * 0.5f;
        dl->AddText(ImVec2(txt_x, txt_y), kColTextPrimary, display[col].c_str());
      }

      dl->PopClipRect();
    }
  }

  void DrawActionWidget(const RowSpec& row, Lang lang, float width, float height, float scale) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();

    std::string id = std::string("##action_") + row.cvar;
    if (ImGui::InvisibleButton(id.c_str(), ImVec2(width, height))) {
      ActivateRow(row);
    }
    bool hov = ImGui::IsItemHovered();
    dl->AddRectFilled(pos, ImVec2(pos.x + width, pos.y + height),
                      hov ? kColAccentBright : kColAccent, 4.0f * scale);
    const char* txt = "Configure  >";
    ImVec2 ts = ImGui::CalcTextSize(txt);
    dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f),
                kColTextPrimary, txt);
    (void)lang;
  }

  void DrawSavesActionsPanel(Lang lang, float scale) {
    ImGui::Dummy(ImVec2(0.0f, 8.0f * scale));

    std::filesystem::path active_dir = paths_.user_data_root;
    const std::string configured = model_.Get("user_data_root");
    std::filesystem::path target_dir =
        configured.empty() ? options_actions::DefaultSaveDir() : rex::to_path(configured);

    ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s: %s",
                       UiText("current_save_folder").For(lang),
                       rex::path_to_utf8(active_dir).c_str());

    ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kColAccent));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                          ImGui::ColorConvertU32ToFloat4(kColAccentBright));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::ColorConvertU32ToFloat4(kColAccent));

    if (ImGui::Button(UiText("browse_folder").For(lang), ImVec2(180.0f * scale, 34.0f * scale))) {
      std::string picked = options_actions::PickFolder(target_dir);
      if (!picked.empty()) {
        model_.Set("user_data_root", picked);
        RefreshDynamicRowChoices(model_, paths_);
        SaveConfigToDisk(paths_.config_path, model_);
      }
    }

    ImGui::SameLine(0.0f, 12.0f * scale);

    if (ImGui::Button(UiText("open_save_folder").For(lang),
                      ImVec2(240.0f * scale, 34.0f * scale))) {
      options_actions::OpenFolder(target_dir.empty() ? active_dir : target_dir);
    }

    ImGui::PopStyleColor(3);
  }

  void DrawOverlay(ImGuiIO& io) {
    const Lang lang = CurrentLang();
    const float disp_w = io.DisplaySize.x > 0.0f ? io.DisplaySize.x : 1280.0f;
    const float disp_h = io.DisplaySize.y > 0.0f ? io.DisplaySize.y : 720.0f;
    const float scale = std::clamp(disp_h / 900.0f, 0.80f, 1.45f);

    if (g_body_font) {
      ImGui::PushFont(g_body_font);
    }

    // Full-screen dim backdrop.
    ImGui::GetBackgroundDrawList()->AddRectFilled(ImVec2(0.0f, 0.0f), ImVec2(disp_w, disp_h),
                                                  kColBackdrop);

    const float panel_w = std::clamp(disp_w * 0.84f, 760.0f, 1180.0f * scale);
    const float panel_h = std::clamp(disp_h * 0.86f, 520.0f, 820.0f * scale);
    const ImVec2 panel_pos((disp_w - panel_w) * 0.5f, (disp_h - panel_h) * 0.5f);

    ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_w, panel_h), ImGuiCond_Always);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24.0f * scale, 20.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.5f * scale);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kColPanelBg));
    ImGui::PushStyleColor(ImGuiCol_Border, ImGui::ColorConvertU32ToFloat4(kColPanelBorder));

    constexpr ImGuiWindowFlags kWinFlags =
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    if (ImGui::Begin("##Gow3PcOptions", nullptr, kWinFlags)) {
      ImDrawList* win_dl = ImGui::GetWindowDrawList();

      // Header title & subtitle.
      if (g_heading_font) {
        ImGui::PushFont(g_heading_font);
      }
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextPrimary), "%s  //  %s",
                         UiText("subtitle").For(lang), UiText("title").For(lang));
      if (g_heading_font) {
        ImGui::PopFont();
      }

      ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));

      // Top category tabs.
      {
        const float tab_h = 34.0f * scale;
        bool first = true;
        for (const auto& p : Catalog()) {
          if (!IsTabPage(p.id)) {
            continue;
          }
          if (!first) {
            ImGui::SameLine(0.0f, 8.0f * scale);
          }
          first = false;

          const bool active =
              (p.id == active_page_) ||
              (IsControlsSubPage(active_page_) && p.id == Page::kControls);
          const char* tab_label = p.title.For(lang);
          ImVec2 tsz = ImGui::CalcTextSize(tab_label);
          float tab_w = tsz.x + 28.0f * scale;

          ImVec2 tpos = ImGui::GetCursorScreenPos();
          std::string tab_id = std::string("##tab_") + tab_label;
          if (ImGui::InvisibleButton(tab_id.c_str(), ImVec2(tab_w, tab_h))) {
            active_page_ = p.id;
            selected_row_ = 0;
            scroll_to_selected_ = true;
          }
          bool thov = ImGui::IsItemHovered();
          ImU32 tbg = active ? kColAccent : (thov ? kColRowHover : kColRowIdle);
          win_dl->AddRectFilled(tpos, ImVec2(tpos.x + tab_w, tpos.y + tab_h), tbg, 4.0f * scale);
          if (active) {
            win_dl->AddRect(tpos, ImVec2(tpos.x + tab_w, tpos.y + tab_h), kColAccentBright,
                            4.0f * scale, 0, 1.5f * scale);
          }
          win_dl->AddText(
              ImVec2(tpos.x + (tab_w - tsz.x) * 0.5f, tpos.y + (tab_h - tsz.y) * 0.5f),
              kColTextPrimary, tab_label);
        }
      }

      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));
      {
        ImVec2 rpos = ImGui::GetCursorScreenPos();
        win_dl->AddLine(rpos, ImVec2(rpos.x + ImGui::GetContentRegionAvail().x, rpos.y),
                        kColHeaderRule, 2.0f * scale);
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

      const PageSpec& page = ActivePageSpec();

      // Sub-page back button for Controller Buttons and Keyboard & Mouse Bindings.
      if (IsControlsSubPage(active_page_)) {
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kColRowIdle));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(kColAccent));
        if (ImGui::Button(UiText("back_to_controls").For(lang),
                          ImVec2(190.0f * scale, 30.0f * scale))) {
          active_page_ = Page::kControls;
          selected_row_ = 0;
          scroll_to_selected_ = true;
        }
        ImGui::PopStyleColor(2);
        ImGui::SameLine(0.0f, 14.0f * scale);
      }

      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s",
                         page.subtitle.For(lang));

      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

      // Header row for Primary and Secondary columns on bindings sub-pages.
      if (IsControlsSubPage(active_page_)) {
        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float ctrl_w = std::clamp(avail_w * 0.52f, 320.0f * scale, 480.0f * scale);
        const float gap = 8.0f * scale;
        const float slot_w = (ctrl_w - gap) * 0.5f;
        const float hdr_h = 22.0f * scale;

        ImVec2 hdr_pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Action header on left
        const char* act_hdr = "GAME ACTION";
        dl->AddText(ImVec2(hdr_pos.x + 14.0f * scale, hdr_pos.y + (hdr_h - ImGui::GetFontSize()) * 0.5f),
                    kColTextDim, act_hdr);

        // Primary header
        const char* pri_hdr = "PRIMARY";
        ImVec2 pri_sz = ImGui::CalcTextSize(pri_hdr);
        float pri_x = (hdr_pos.x + avail_w) - ctrl_w - 12.0f * scale;
        dl->AddText(ImVec2(pri_x + (slot_w - pri_sz.x) * 0.5f, hdr_pos.y + (hdr_h - pri_sz.y) * 0.5f),
                    kColTextDim, pri_hdr);

        // Secondary header
        const char* sec_hdr = "SECONDARY";
        ImVec2 sec_sz = ImGui::CalcTextSize(sec_hdr);
        float sec_x = pri_x + slot_w + gap;
        dl->AddText(ImVec2(sec_x + (slot_w - sec_sz.x) * 0.5f, hdr_pos.y + (hdr_h - sec_sz.y) * 0.5f),
                    kColTextDim, sec_hdr);

        ImGui::Dummy(ImVec2(0.0f, hdr_h + 4.0f * scale));
      }

      // Scrollable rows child region.
      const float footer_h = 148.0f * scale;
      const float list_h = std::max(160.0f, ImGui::GetContentRegionAvail().y - footer_h);

      ImGui::BeginChild("##OptionsRows", ImVec2(0.0f, list_h), false);
      {
        auto rows = VisibleRows(page);
        ClampSelectedRow();

        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float row_h = 46.0f * scale;
        const bool is_bindings_subpage = IsControlsSubPage(active_page_);
        const float ctrl_w = is_bindings_subpage
                                 ? std::clamp(avail_w * 0.52f, 320.0f * scale, 480.0f * scale)
                                 : std::clamp(avail_w * 0.40f, 240.0f * scale, 380.0f * scale);
        const float ctrl_h = 32.0f * scale;

        for (size_t i = 0; i < rows.size(); ++i) {
          const RowSpec& row = *rows[i];
          const bool selected = (static_cast<int>(i) == selected_row_);
          const bool modified = model_.IsModifiedFromBoot(row.cvar);

          ImGui::PushID(row.cvar);
          ImVec2 row_pos = ImGui::GetCursorScreenPos();
          ImVec2 row_end(row_pos.x + avail_w, row_pos.y + row_h);

          const bool row_hovered =
              ImGui::IsMouseHoveringRect(row_pos, row_end, true);
          if (row_hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            selected_row_ = static_cast<int>(i);
          }

          ImU32 row_bg = selected ? kColRowSelected : (row_hovered ? kColRowHover : kColRowIdle);
          ImDrawList* dl = ImGui::GetWindowDrawList();
          dl->AddRectFilled(row_pos, row_end, row_bg, 5.0f * scale);
          if (selected) {
            dl->AddRectFilled(row_pos, ImVec2(row_pos.x + 4.0f * scale, row_end.y),
                              kColAccentBright, 2.0f * scale);
          }

          if (selected && scroll_to_selected_) {
            ImGui::SetScrollHereY(0.5f);
            scroll_to_selected_ = false;
          }

          // Row label on the left.
          const char* label_txt = row.label.For(lang);
          ImVec2 label_sz = ImGui::CalcTextSize(label_txt);
          dl->AddText(
              ImVec2(row_pos.x + 14.0f * scale, row_pos.y + (row_h - label_sz.y) * 0.5f),
              modified ? kColAccentBright : kColTextPrimary, label_txt);

          // Control widget on the right.
          ImGui::SetCursorScreenPos(
              ImVec2(row_end.x - ctrl_w - 12.0f * scale, row_pos.y + (row_h - ctrl_h) * 0.5f));

          if (is_bindings_subpage) {
            DrawBindingsRowWidget(row, static_cast<int>(i), lang, ctrl_w, ctrl_h, scale);
          } else {
            switch (row.control) {
              case Control::kToggle:
                DrawToggleWidget(row, lang, ctrl_w, ctrl_h, scale);
                break;
              case Control::kChoice:
                DrawChoiceWidget(row, lang, ctrl_w, ctrl_h, scale);
                break;
              case Control::kSliderInt:
              case Control::kSliderFloat:
                DrawSliderWidget(row, ctrl_w, ctrl_h, scale);
                break;
              case Control::kText:
                DrawTextWidget(row, ctrl_w, ctrl_h, scale);
                break;
              case Control::kAction:
                DrawActionWidget(row, lang, ctrl_w, ctrl_h, scale);
                break;
            }
          }

          ImGui::SetCursorScreenPos(ImVec2(row_pos.x, row_end.y + 5.0f * scale));
          ImGui::Dummy(ImVec2(0.0f, 0.0f));  // ImGui needs an item after a cursor move
          ImGui::PopID();
        }

        if (active_page_ == Page::kSaves) {
          DrawSavesActionsPanel(lang, scale);
        }
      }
      ImGui::EndChild();

      // Selected row description + Apply badge panel.
      ImGui::Dummy(ImVec2(0.0f, 4.0f * scale));
      {
        ImVec2 rpos = ImGui::GetCursorScreenPos();
        win_dl->AddLine(rpos, ImVec2(rpos.x + ImGui::GetContentRegionAvail().x, rpos.y),
                        kColControlBorder, 1.0f);
      }
      ImGui::Dummy(ImVec2(0.0f, 6.0f * scale));

      auto rows = VisibleRows(page);
      if (!rows.empty()) {
        ClampSelectedRow();
        const RowSpec& sel = *rows[static_cast<size_t>(selected_row_)];
        DrawApplyBadge(sel.apply, model_.IsModifiedFromBoot(sel.cvar), lang, scale);
        ImGui::SameLine(0.0f, 12.0f * scale);
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextMuted), "%s",
                           sel.help.For(lang));
        ImGui::PopTextWrapPos();
      }

      // Pending restart / next-level notice.
      if (model_.HasPendingRestart()) {
        ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccentBright), "%s",
                           UiText("pending_restart").For(lang));
      } else if (model_.HasPendingNextLevel()) {
        ImGui::Dummy(ImVec2(0.0f, 2.0f * scale));
        ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.32f, 1.0f), "%s",
                           UiText("pending_level").For(lang));
      }

      // Bottom action bar.
      ImGui::SetCursorPosY(panel_h - 56.0f * scale);
      ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kColRowIdle));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::ColorConvertU32ToFloat4(kColAccent));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                            ImGui::ColorConvertU32ToFloat4(kColAccentBright));

      if (ImGui::Button(UiText("close").For(lang), ImVec2(120.0f * scale, 32.0f * scale))) {
        SetMenuOpen(false);
      }

      ImGui::SameLine(0.0f, 10.0f * scale);
      if (ImGui::Button(UiText("reset_page").For(lang), ImVec2(190.0f * scale, 32.0f * scale))) {
        model_.ResetPageToDefaults(active_page_);
        SyncTextBuffersFromModel();
        RefreshDynamicRowChoices(model_, paths_);
        SaveConfigToDisk(paths_.config_path, model_);
        if (active_page_ == Page::kKeyBindings) {
          NotifyBindingsChanged();
        }
      }

      ImGui::SameLine(0.0f, 10.0f * scale);
      if (ImGui::Button(UiText("advanced").For(lang), ImVec2(180.0f * scale, 32.0f * scale))) {
        SetMenuOpen(false);
        ToggleRuntimeSettings();
      }

      if (model_.HasPendingRestart()) {
        ImGui::SameLine(0.0f, 10.0f * scale);
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::ColorConvertU32ToFloat4(kColBadgeRestart));
        if (ImGui::Button(UiText("restart_now").For(lang),
                          ImVec2(180.0f * scale, 32.0f * scale))) {
          FlushTextBuffersToModel();
          SaveConfigToDisk(paths_.config_path, model_);
          rex::cvar::SetFlagByName("xam_input_block", "false");
          options_actions::RestartGame();
        }
        ImGui::PopStyleColor();
      }

      ImGui::PopStyleColor(3);

      // Navigation hint bar.
      ImGui::SameLine(0.0f, 18.0f * scale);
      const auto now_time = std::chrono::steady_clock::now();
      const bool has_status = !status_message_.empty() &&
          std::chrono::duration_cast<std::chrono::seconds>(now_time - status_time_).count() < 4;
      if (has_status) {
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccentBright), "%s",
                           status_message_.c_str());
      } else {
        const char* hint = IsControlsSubPage(active_page_) ? UiText("hint_bar_bindings").For(lang)
                                                           : UiText("hint_bar").For(lang);
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextDim), "%s", hint);
      }

      // Controller button / key capture modal overlay. Drawn on the foreground list: on the
      // window's own list the scrolling rows (a child window, drawn later) covered it.
      if (!capture_cvar_.empty()) {
        ImDrawList* fg_dl = ImGui::GetForegroundDrawList();
        fg_dl->AddRectFilled(panel_pos, ImVec2(panel_pos.x + panel_w, panel_pos.y + panel_h),
                             IM_COL32(0, 0, 0, 195), 8.0f * scale);
        const char* prompt =
            UiText(capture_is_pad_ ? "pad_press_button" : "key_press_key").For(lang);
        ImVec2 psz = ImGui::CalcTextSize(prompt);
        ImVec2 box_min((disp_w - psz.x) * 0.5f - 28.0f * scale,
                       (disp_h - psz.y) * 0.5f - 20.0f * scale);
        ImVec2 box_max((disp_w + psz.x) * 0.5f + 28.0f * scale,
                       (disp_h + psz.y) * 0.5f + 20.0f * scale);
        fg_dl->AddRectFilled(box_min, box_max, kColPanelBg | IM_COL32_A_MASK, 6.0f * scale);
        fg_dl->AddRect(box_min, box_max, kColAccentBright, 6.0f * scale, 0, 2.0f * scale);
        fg_dl->AddText(ImVec2((disp_w - psz.x) * 0.5f, (disp_h - psz.y) * 0.5f),
                       kColTextPrimary | IM_COL32_A_MASK, prompt);
      }
    }
    ImGui::End();

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (g_body_font) {
      ImGui::PopFont();
    }
  }

  rex::PathConfig paths_;
  rex::ui::ImmediateDrawer* immediate_drawer_ = nullptr;
  Model model_;
  InputTracker input_;
  bool open_ = false;
  bool unblock_pending_ = false;
  Page active_page_ = Page::kDisplay;
  int selected_row_ = 0;
  int selected_col_ = 0;
  bool scroll_to_selected_ = false;
  std::string capture_cvar_;
  int capture_slot_ = 0;
  bool capture_is_pad_ = false;
  bool capture_armed_ = false;
  uint16_t prev_capture_pad_ = 0;
  bool prev_capture_esc_ = false;
  std::string status_message_;
  std::chrono::steady_clock::time_point status_time_{};
  std::unordered_map<std::string, std::unique_ptr<rex::ui::ImmediateTexture>> glyph_cache_;
  std::unordered_map<std::string, std::array<char, 256>> text_buffers_;
};

OptionsOverlayDialog* g_active_overlay = nullptr;

}  // namespace

void AddOptionsFonts(ImFontAtlas* atlas) {
  if (!atlas) {
    return;
  }
#ifdef _WIN32
  static const ImWchar kOptionsGlyphRanges[] = {
      0x0020, 0x00FF,  // Basic Latin + Latin Supplement
      0x2000, 0x206F,  // General Punctuation (includes U+2014 em-dash, U+2026 ellipsis)
      0,
  };
  wchar_t win_dir[MAX_PATH]{};
  if (GetWindowsDirectoryW(win_dir, MAX_PATH) > 0) {
    const std::filesystem::path fonts = std::filesystem::path(win_dir) / L"Fonts";
    const std::filesystem::path segoe = fonts / L"segoeui.ttf";
    const std::filesystem::path segoe_sb = fonts / L"seguisb.ttf";
    std::error_code ec;
    if (std::filesystem::is_regular_file(segoe, ec)) {
      g_body_font =
          atlas->AddFontFromFileTTF(rex::path_to_utf8(segoe).c_str(), 17.0f, nullptr, kOptionsGlyphRanges);
    }
    if (std::filesystem::is_regular_file(segoe_sb, ec)) {
      g_heading_font =
          atlas->AddFontFromFileTTF(rex::path_to_utf8(segoe_sb).c_str(), 21.0f, nullptr, kOptionsGlyphRanges);
    } else if (g_body_font) {
      g_heading_font = g_body_font;
    }
  }
#else
#endif
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateOptionsOverlay(rex::ui::ImGuiDrawer* drawer,
                                                           const rex::PathConfig& paths,
                                                           rex::ui::ImmediateDrawer* immediate_drawer) {
  if (!drawer) {
    return nullptr;
  }
  auto dlg = std::make_unique<OptionsOverlayDialog>(drawer, paths, immediate_drawer);
  g_active_overlay = dlg.get();
  return dlg;
}

void ClosePcOptionsForShutdown() {
  if (g_active_overlay) {
    g_active_overlay->CloseForShutdown();
    g_active_overlay = nullptr;
  }
}

}  // namespace rex::glue
