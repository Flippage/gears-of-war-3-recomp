// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_overlay.h"

#include "gow3_options_actions.h"
#include "gow3_options_catalog.h"
#include "gow3_options_input.h"
#include "gow3_options_model.h"
#include "gow3_options_values.h"
#include "gow3_options_values_text.h"
#include "gow3_xam_input_guard.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include <imgui.h>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>
#include "gow3_pad_remap.h"
#include <rex/runtime.h>
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
  rex::X_RESULT res = WithXamInputLock([&]() { return input_sys->GetState(0, &state); });
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

class OptionsOverlayDialog final : public rex::ui::ImGuiDialog {
 public:
  OptionsOverlayDialog(rex::ui::ImGuiDrawer* drawer, rex::PathConfig paths)
      : rex::ui::ImGuiDialog(drawer),
        paths_(std::move(paths)),
        model_(CreateLiveModel(paths_)) {
    SyncTextBuffersFromModel();
  }

  ~OptionsOverlayDialog() override { CloseForShutdown(); }

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

    // Handle the active keyboard / mouse capture on Page::kKeyBindings: wait until nothing is
    // held (the click or Enter that started it), then take the next key or mouse button.
    if (open_ && !key_capture_cvar_.empty()) {
      if (raw.key_escape && !prev_capture_esc_) {
        key_capture_cvar_.clear();
      } else {
#ifdef _WIN32
        HWND fg = GetForegroundWindow();
        DWORD fg_pid = 0;
        if (fg) {
          GetWindowThreadProcessId(fg, &fg_pid);
        }
        const std::string key =
            fg_pid == GetCurrentProcessId() ? FirstBindableKeyDown() : std::string();
        if (!key_capture_armed_) {
          key_capture_armed_ = key.empty();
        } else if (!key.empty()) {
          model_.Set(key_capture_cvar_, key);
          SaveConfigToDisk(paths_.config_path, model_);
          SyncTextBuffersFromModel();
          key_capture_cvar_.clear();
        }
#else
        key_capture_cvar_.clear();
#endif
      }
      prev_capture_pad_ = raw.pad_buttons;
      prev_capture_esc_ = raw.key_escape;
      // Eat navigation edges while capturing a key.
      (void)input_.Step(raw, open_);
    } else if (open_ && !pad_capture_cvar_.empty()) {
      if (raw.key_escape && !prev_capture_esc_) {
        pad_capture_cvar_.clear();
      } else {
        const uint16_t newly_pressed =
            static_cast<uint16_t>(raw.pad_buttons & ~prev_capture_pad_);
        const std::string_view btn_name = SingleBitPadName(newly_pressed);
        if (!btn_name.empty()) {
          model_.Set(pad_capture_cvar_, btn_name);
          SaveConfigToDisk(paths_.config_path, model_);
          pad_capture_cvar_.clear();
        }
      }
      prev_capture_pad_ = raw.pad_buttons;
      prev_capture_esc_ = raw.key_escape;
      // Eat navigation edges while capturing a button.
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
    } else {
      pad_capture_cvar_.clear();
      key_capture_cvar_.clear();
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
        scroll_to_selected_ = true;
      } else if (std::string_view(row.cvar) == "key_bindings") {
        active_page_ = Page::kKeyBindings;
        selected_row_ = 0;
        scroll_to_selected_ = true;
      }
      return;
    }
    if (row.page == Page::kPadButtons) {
      pad_capture_cvar_ = row.cvar;
      return;
    }
    if (row.page == Page::kKeyBindings) {
      StartKeyCapture(row.cvar);
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
        scroll_to_selected_ = true;
      } else {
        SetMenuOpen(false);
      }
      return;
    }

    if (edges.switch_page != 0) {
      SwitchTab(edges.switch_page);
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

    if (edges.confirm) {
      ActivateRow(sel);
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

    const bool is_pad_row = (row.page == Page::kPadButtons);
    const float remap_w = is_pad_row ? 76.0f * scale : 0.0f;
    const float gap = is_pad_row ? 6.0f * scale : 0.0f;
    const float arrow_w = 30.0f * scale;
    const float box_w = std::max(80.0f * scale, width - remap_w - gap - arrow_w * 2.0f);

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

    if (is_pad_row) {
      ImGui::SameLine(0.0f, gap);
      ImVec2 rp = ImGui::GetCursorScreenPos();
      std::string remap_id = std::string("##remap_") + row.cvar;
      if (ImGui::InvisibleButton(remap_id.c_str(), ImVec2(remap_w, height))) {
        pad_capture_cvar_ = row.cvar;
      }
      bool rp_hov = ImGui::IsItemHovered();
      dl->AddRectFilled(rp, ImVec2(rp.x + remap_w, rp.y + height),
                        rp_hov ? kColAccentBright : kColAccentSoft, 4.0f * scale);
      dl->AddRect(rp, ImVec2(rp.x + remap_w, rp.y + height), kColAccent, 4.0f * scale);
      const char* remap_txt = UiText("pad_remap").For(lang);
      ImVec2 rsz = ImGui::CalcTextSize(remap_txt);
      dl->AddText(ImVec2(rp.x + (remap_w - rsz.x) * 0.5f, rp.y + (height - rsz.y) * 0.5f),
                  kColTextPrimary, remap_txt);
    }
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

  void StartKeyCapture(const char* cvar) {
    FlushTextBuffersToModel();
    key_capture_cvar_ = cvar;
    key_capture_armed_ = false;
  }

  void DrawTextWidget(const RowSpec& row, float width, float height, float scale) {
    auto& buf = text_buffers_[row.cvar];
    const bool is_key_row = (row.page == Page::kKeyBindings);
    const float set_w = is_key_row ? 76.0f * scale : 0.0f;
    const float set_gap = is_key_row ? 6.0f * scale : 0.0f;
    width = std::max(80.0f * scale, width - set_w - set_gap);
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

    if (is_key_row) {
      ImDrawList* dl = ImGui::GetWindowDrawList();
      ImGui::SameLine(0.0f, set_gap);
      ImVec2 sp = ImGui::GetCursorScreenPos();
      std::string set_id = std::string("##keyset_") + row.cvar;
      if (ImGui::InvisibleButton(set_id.c_str(), ImVec2(set_w, height))) {
        StartKeyCapture(row.cvar);
      }
      bool sp_hov = ImGui::IsItemHovered();
      dl->AddRectFilled(sp, ImVec2(sp.x + set_w, sp.y + height),
                        sp_hov ? kColAccentBright : kColAccentSoft, 4.0f * scale);
      dl->AddRect(sp, ImVec2(sp.x + set_w, sp.y + height), kColAccent, 4.0f * scale);
      const char* set_txt = UiText("key_set").For(Lang::kEn);
      ImVec2 ssz = ImGui::CalcTextSize(set_txt);
      dl->AddText(ImVec2(sp.x + (set_w - ssz.x) * 0.5f, sp.y + (height - ssz.y) * 0.5f),
                  kColTextPrimary, set_txt);
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

      // Scrollable rows child region.
      const float footer_h = 148.0f * scale;
      const float list_h = std::max(160.0f, ImGui::GetContentRegionAvail().y - footer_h);

      ImGui::BeginChild("##OptionsRows", ImVec2(0.0f, list_h), false);
      {
        auto rows = VisibleRows(page);
        ClampSelectedRow();

        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float row_h = 46.0f * scale;
        const float ctrl_w = std::clamp(avail_w * 0.40f, 240.0f * scale, 380.0f * scale);
        const float ctrl_h = 30.0f * scale;

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
      ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColTextDim), "%s",
                         UiText("hint_bar").For(lang));

      // Controller button / key capture modal overlay.
      if (!pad_capture_cvar_.empty() || !key_capture_cvar_.empty()) {
        win_dl->AddRectFilled(panel_pos, ImVec2(panel_pos.x + panel_w, panel_pos.y + panel_h),
                              IM_COL32(0, 0, 0, 195), 8.0f * scale);
        const char* prompt =
            UiText(key_capture_cvar_.empty() ? "pad_press_button" : "key_press_key").For(lang);
        ImVec2 psz = ImGui::CalcTextSize(prompt);
        ImVec2 box_min((disp_w - psz.x) * 0.5f - 28.0f * scale,
                       (disp_h - psz.y) * 0.5f - 20.0f * scale);
        ImVec2 box_max((disp_w + psz.x) * 0.5f + 28.0f * scale,
                       (disp_h + psz.y) * 0.5f + 20.0f * scale);
        win_dl->AddRectFilled(box_min, box_max, kColPanelBg, 6.0f * scale);
        win_dl->AddRect(box_min, box_max, kColAccentBright, 6.0f * scale, 0, 2.0f * scale);
        win_dl->AddText(ImVec2((disp_w - psz.x) * 0.5f, (disp_h - psz.y) * 0.5f), kColTextPrimary,
                        prompt);
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
  Model model_;
  InputTracker input_;
  bool open_ = false;
  bool unblock_pending_ = false;
  Page active_page_ = Page::kDisplay;
  int selected_row_ = 0;
  bool scroll_to_selected_ = false;
  std::string pad_capture_cvar_;
  // Gears of War 3 recomp: keybind_* row waiting for a key, and whether the keys that started the
  // capture have been released.
  std::string key_capture_cvar_;
  bool key_capture_armed_ = false;
  uint16_t prev_capture_pad_ = 0;
  bool prev_capture_esc_ = false;
  std::unordered_map<std::string, std::array<char, 256>> text_buffers_;
};

OptionsOverlayDialog* g_active_overlay = nullptr;

}  // namespace

void AddOptionsFonts(ImFontAtlas* atlas) {
  if (!atlas) {
    return;
  }
#ifdef _WIN32
  wchar_t win_dir[MAX_PATH]{};
  if (GetWindowsDirectoryW(win_dir, MAX_PATH) > 0) {
    const std::filesystem::path fonts = std::filesystem::path(win_dir) / L"Fonts";
    const std::filesystem::path segoe = fonts / L"segoeui.ttf";
    const std::filesystem::path segoe_sb = fonts / L"seguisb.ttf";
    std::error_code ec;
    if (std::filesystem::is_regular_file(segoe, ec)) {
      g_body_font =
          atlas->AddFontFromFileTTF(rex::path_to_utf8(segoe).c_str(), 17.0f);
    }
    if (std::filesystem::is_regular_file(segoe_sb, ec)) {
      g_heading_font =
          atlas->AddFontFromFileTTF(rex::path_to_utf8(segoe_sb).c_str(), 21.0f);
    } else if (g_body_font) {
      g_heading_font = g_body_font;
    }
  }
#else
#endif
}

std::unique_ptr<rex::ui::ImGuiDialog> CreateOptionsOverlay(rex::ui::ImGuiDrawer* drawer,
                                                           const rex::PathConfig& paths) {
  if (!drawer) {
    return nullptr;
  }
  auto dlg = std::make_unique<OptionsOverlayDialog>(drawer, paths);
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
