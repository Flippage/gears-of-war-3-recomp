#include "pages_settings.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include <imgui.h>
#include <rex/ui/keybinds.h>

#include "app.h"
#include "gow3_keybind_text.h"
#include "gow3_language.h"
#include "gow3_options_actions.h"
#include "pages_game.h"
#include "setup/path_util.h"
#include "setup/settings_store.h"
#include "theme.h"

namespace {

namespace opt = rex::glue::options;
namespace actions = rex::glue::options_actions;
using gow3::setup::FromUtf8;
using opt::Control;
using opt::Page;

constexpr ImVec4 kHint = theme::Rgb(0x646A72), kRowLine = theme::Rgb(0x1A1D21), kKeycap = theme::Rgb(0x3A3F46);

// The key-binding slot waiting for a key, and what SettingsCaptureMessage got for it (applied on
// the next frame, where the Model is at hand).
struct Capture {
  std::string cvar;
  int slot = 0;
  enum class Result { kWaiting, kKey, kClear, kCancel } result = Result::kWaiting;
  std::string key;
} g_capture;

std::map<std::string, std::array<char, 256>> g_text;  // kText rows' edit buffers
enum class SaveStatus { kNone, kSaved, kFailed } g_save = SaveStatus::kNone;

ImU32 Col(const ImVec4& c) { return ImGui::GetColorU32(c); }

std::string Upper(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'a' && c <= 'z') c = char(c - 'a' + 'A');
  }
  return out;
}

// Group headers (the catalog has none): the row a group starts at.
const char* GroupAt(std::string_view cvar) {
  static constexpr std::pair<std::string_view, const char*> kGroups[] = {
      {"window_mode", "WINDOW"},          {"vsync", "FRAME RATE"},
      {"gow3_widescreen", "VIEW"},        {"gow3_language", "LANGUAGE"},
      {"gow3_renderer", "RENDERING"},     {"gow3_black_shading_fix", "GAME FIXES"},
      {"gow3_button_prompts", "INPUT"},   {"mnk_mouse", "MOUSE"},
      {"pad_right_stick_deadzone", "CONTROLLER"}, {"net_link_active", "SYSTEM LINK"},
      {"user_data_root", "SAVE FOLDER"},  {"gow3_unlock_insane", "CAMPAIGN"},
  };
  for (const auto& [key, name] : kGroups) {
    if (key == cvar) return name;
  }
  return nullptr;
}

// Monitors, GPUs and save folders, as RefreshDynamicRowChoices fills them in the game.
void RefreshDynamicChoices(opt::Model& m) {
  std::vector<opt::DynamicChoice> monitors;
  for (const auto& mon : actions::ListMonitors()) monitors.push_back({std::to_string(mon.index), mon.label});
  if (!monitors.empty()) m.SetDynamicChoices("monitor", std::move(monitors));

  std::vector<opt::DynamicChoice> gpus{{"-1", "Automatic"}};
  for (const auto& g : actions::ListGpus()) gpus.push_back({std::to_string(g.index), g.label});
  m.SetDynamicChoices("d3d12_adapter", std::move(gpus));

  const std::string configured = m.Get("user_data_root");
  const std::filesystem::path current = configured.empty() ? actions::DefaultSaveDir() : FromUtf8(configured);
  std::vector<opt::DynamicChoice> folders;
  for (const auto& f : actions::ListSaveFolders(current)) folders.push_back({f.value, f.label});
  if (!folders.empty()) m.SetDynamicChoices("user_data_root", std::move(folders));

  // Game Language: the languages noted for the game folder (the catalog's row was filtered once,
  // possibly before a game folder was set).
  std::vector<opt::DynamicChoice> languages;
  for (const opt::Choice& c : opt::AllLanguageChoices()) {
    if (std::string_view(c.value) == "auto" || rex::glue::GameHasLanguage(c.value)) {
      languages.push_back({c.value, c.label.For()});
    }
  }
  m.SetDynamicChoices("gow3_language", std::move(languages));
}

void NoteGameLanguages(const App& app) {
  if (app.game.empty()) return;
  for (const std::string& code : rex::glue::ScanGameLanguages(app.game)) rex::glue::NoteGameLanguage(code);
}

void SyncTextBuffers(const opt::Model& m) {
  for (const auto& page : opt::Catalog()) {
    for (const auto& row : page.rows) {
      if (row.control != Control::kText) continue;
      std::array<char, 256> buf{};
      std::snprintf(buf.data(), buf.size(), "%s", m.Get(row.cvar).c_str());
      g_text[row.cvar] = buf;
    }
  }
}

void Load(App& app) {
  if (app.settings) return;
  NoteGameLanguages(app);
  app.settings_file = gow3::setup::LoadSettings(app.dir / "gow3.toml");
  app.settings.emplace(gow3::setup::MakeSettingsModel(app.settings_file));
  RefreshDynamicChoices(*app.settings);
  SyncTextBuffers(*app.settings);
}

void Save(App& app) {
  if (!app.settings_file.readable) return;  // read-only: the file is never written
  const std::filesystem::path toml = app.dir / "gow3.toml";
  // A missing gow3.toml starts from the release's default (config\gow3.default.toml), then gets the
  // changed lines; without that copy only the changed lines are written.
  std::error_code ec;
  const bool fresh = !std::filesystem::exists(toml, ec) && !ec &&
                     gow3::setup::CopyDefaultSettings(app.dir / "config" / "gow3.default.toml", toml);
  g_save = gow3::setup::SaveSettings(toml, *app.settings) ? SaveStatus::kSaved : SaveStatus::kFailed;
  if (fresh) {  // the Model reads the file's values (the default's, and the ones just saved)
    app.settings_file = gow3::setup::LoadSettings(toml);
    app.settings->RefreshFromReader();
  }
  RefreshPlayPage(app);  // the renderer line
}

// --- Bindings (two slots per row, as in the in-game menu) ----------------------------------------

std::array<std::string, 2> Slots(const opt::Model& m, const char* cvar) {
  std::array<std::string, 2> s;
  const std::vector<std::string> keys = opt::SplitKeys(m.Get(cvar));
  for (size_t i = 0; i < keys.size() && i < 2; ++i) s[i] = keys[i] == "none" ? "" : keys[i];
  return s;
}

// Empty slots drop out; a pad row with nothing left is "none", a key row "". Keys after the second
// (written by hand in gow3.toml) are kept, as the in-game menu keeps them.
void SetSlots(opt::Model& m, const opt::RowSpec& row, const std::array<std::string, 2>& s) {
  std::vector<std::string> keys;
  for (const std::string& k : s) {
    if (!k.empty()) keys.push_back(k);
  }
  const std::vector<std::string> old = opt::SplitKeys(m.Get(row.cvar));
  for (size_t i = 2; i < old.size(); ++i) {
    if (old[i] != "none") keys.push_back(old[i]);
  }
  const std::string joined = opt::JoinKeys(keys);
  m.Set(row.cvar, joined.empty() && row.page == Page::kPadButtons ? "none" : joined);
}

void SetKeySlot(opt::Model& m, const opt::RowSpec& row, int slot, const std::string& key) {
  std::array<std::string, 2> s = Slots(m, row.cvar);
  if (slot == 1 && s[0].empty()) slot = 0;  // a second key with no first one becomes the first
  s[slot] = key;
  SetSlots(m, row, s);
}

// A controller button bound elsewhere moves here; that slot takes this slot's old button.
void SetPadSlot(opt::Model& m, const opt::RowSpec& row, int slot, const std::string& btn) {
  if (btn.empty() || btn == "none") {
    SetKeySlot(m, row, slot, "");
    return;
  }
  const std::string old = Slots(m, row.cvar)[slot];
  for (const auto& page : opt::Catalog()) {
    if (page.id != Page::kPadButtons) continue;
    for (const auto& other : page.rows) {
      std::array<std::string, 2> s = Slots(m, other.cvar);
      for (int i = 0; i < 2; ++i) {
        if (s[i] != btn || (&other == &row && i == slot)) continue;
        s[i] = old;
        SetSlots(m, other, s);
        i = 2;
      }
    }
  }
  SetKeySlot(m, row, slot, btn);
}

// --- Widgets ------------------------------------------------------------------------------------

// A keycap: the key's name, crimson while it waits for a key. True when clicked.
bool Keycap(const char* id, const std::string& key, bool waiting, float width) {
  using namespace theme;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 size(width, ImGui::GetFrameHeight());
  const bool pressed = ImGui::InvisibleButton(id, size);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 q(p.x + size.x, p.y + size.y);
  const ImVec4 edge = waiting ? kAccent : ImGui::IsItemHovered() ? kMuted : kKeycap;
  dl->AddRectFilled(p, q, Col(edge), Dp(4.0f));  // the edge, 2 px at the bottom
  dl->AddRectFilled(ImVec2(p.x + Dp(1.0f), p.y + Dp(1.0f)), ImVec2(q.x - Dp(1.0f), q.y - Dp(2.0f)), Col(kBackground),
                    Dp(3.0f));
  const char* text = waiting ? "Press a key" : key.empty() ? "-" : key.c_str();
  const ImVec2 ts = ImGui::CalcTextSize(text);
  dl->PushClipRect(p, q, true);
  dl->AddText(ImVec2(p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f - Dp(1.0f)),
              Col(waiting ? kAccent : key.empty() ? kMuted : kText), text);
  dl->PopClipRect();
  return pressed;
}

// A thin crimson track with a white knob; true when the value moved.
bool Slider(App& app, const opt::RowSpec& row, float width) {
  using namespace theme;
  opt::Model& m = *app.settings;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float h = ImGui::GetFrameHeight();
  ImGui::InvisibleButton(row.cvar, ImVec2(width, h));
  bool changed = false;
  if (ImGui::IsItemActive()) {
    const std::string before = m.Get(row.cvar);
    m.SetSliderNormalized(row.cvar, (ImGui::GetIO().MousePos.x - p.x) / std::max(1.0f, width));
    changed = m.Get(row.cvar) != before;
  }
  const float t = static_cast<float>(m.SliderNormalized(row.cvar));
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float cy = p.y + h * 0.5f, r = Dp(6.0f), x0 = p.x + r, x1 = p.x + width - r;
  const float kx = x0 + (x1 - x0) * t;
  dl->AddRectFilled(ImVec2(x0, cy - Dp(2.0f)), ImVec2(x1, cy + Dp(2.0f)), Col(kTrackOff), Dp(2.0f));
  dl->AddRectFilled(ImVec2(x0, cy - Dp(2.0f)), ImVec2(kx, cy + Dp(2.0f)), Col(kAccent), Dp(2.0f));
  dl->AddCircleFilled(ImVec2(kx, cy), r, Col(ImVec4(1, 1, 1, 1)));
  return changed;
}

std::string SliderText(const opt::Model& m, const opt::RowSpec& row) {
  char buf[64];
  if (row.control == Control::kSliderInt) std::snprintf(buf, sizeof(buf), "%d", m.GetInt(row.cvar));
  else std::snprintf(buf, sizeof(buf), "%.2f", m.GetFloat(row.cvar));
  return buf + std::string(row.unit ? row.unit : "");
}

// A combo over the row's choices; the current value's label (or the value itself) as preview.
bool ChoiceCombo(const char* id, const std::vector<opt::DynamicChoice>& choices, const std::string& current,
                 std::string* picked, float width, bool short_preview = false) {
  std::string preview = current;
  for (const auto& c : choices) {
    if (c.value == current) preview = c.label;
  }
  // "LB (Left Bumper)" -> "LB" where the combo is narrow.
  if (short_preview) preview = preview.substr(0, preview.find(" ("));
  // Long labels (monitors, save folders) end in "..." before the arrow.
  const float room = width - ImGui::GetStyle().FramePadding.x * 2 - Dp(20.0f);
  if (ImGui::CalcTextSize(preview.c_str()).x > room) {
    while (!preview.empty() && ImGui::CalcTextSize((preview + "...").c_str()).x > room) {
      char c;  // a whole UTF-8 character at a time
      do {
        c = preview.back();
        preview.pop_back();
      } while (!preview.empty() && (c & 0xC0) == 0x80);
    }
    preview += "...";
  }
  bool changed = false;
  ImGui::SetNextItemWidth(width);
  const bool open = ImGui::BeginCombo(id, preview.c_str(), ImGuiComboFlags_NoArrowButton);
  {  // a small muted arrow instead of ImGui's arrow button
    const ImVec2 q = ImGui::GetItemRectMax();
    const float a = Dp(4.0f), cx = q.x - Dp(12.0f), cy = (ImGui::GetItemRectMin().y + q.y) * 0.5f;
    ImGui::GetWindowDrawList()->AddTriangleFilled(ImVec2(cx - a, cy - a * 0.5f), ImVec2(cx + a, cy - a * 0.5f),
                                                  ImVec2(cx, cy + a * 0.5f), Col(theme::kMuted));
  }
  if (open) {
    for (const auto& c : choices) {
      if (ImGui::Selectable(c.label.c_str(), c.value == current) && c.value != current) {
        *picked = c.value;
        changed = true;
      }
    }
    ImGui::EndCombo();
  }
  return changed;
}

// --- Rows ---------------------------------------------------------------------------------------

void GroupHeader(const char* name) {
  ImGui::Dummy(ImVec2(0, Dp(6.0f)));
  ImGui::PushFont(nullptr, 12.0f);
  ImGui::TextColored(theme::kAccent, "%s", name);
  ImGui::PopFont();
}

void DrawControl(App& app, const opt::RowSpec& row, float width) {
  using namespace theme;
  opt::Model& m = *app.settings;
  const float right = ImGui::GetCursorScreenPos().x + width;
  auto place = [&](float w) { ImGui::SetCursorScreenPos(ImVec2(right - w, ImGui::GetCursorScreenPos().y)); };
  ImGui::PushID(row.cvar);
  switch (row.control) {
    case Control::kToggle: {
      bool v = m.GetBool(row.cvar);
      place(Dp(36.0f));
      ImGui::SetCursorScreenPos(ImVec2(ImGui::GetCursorScreenPos().x,
                                       ImGui::GetCursorScreenPos().y + (ImGui::GetFrameHeight() - Dp(20.0f)) * 0.5f));
      if (Toggle("##toggle", &v)) {
        m.Toggle(row.cvar);
        Save(app);
      }
      break;
    }
    case Control::kChoice: {
      if (row.page == Page::kPadButtons) {
        const auto choices = m.ChoicesFor(row, opt::Lang::kEn);
        const std::array<std::string, 2> s = Slots(m, row.cvar);
        const float gap = Dp(8.0f), w = (width - gap) * 0.5f;
        for (int i = 0; i < 2; ++i) {
          std::string picked;
          if (i) ImGui::SameLine(0, gap);
          ImGui::PushID(i);
          if (ChoiceCombo("##slot", choices, s[i].empty() ? "none" : s[i], &picked, w, true)) {
            SetPadSlot(m, row, i, picked);
            Save(app);
          }
          ImGui::PopID();
        }
        break;
      }
      std::string picked;
      if (ChoiceCombo("##choice", m.ChoicesFor(row, opt::Lang::kEn), m.Get(row.cvar), &picked, width)) {
        m.Set(row.cvar, picked);
        if (std::string_view(row.cvar) == "user_data_root") RefreshDynamicChoices(m);
        Save(app);
      }
      break;
    }
    case Control::kSliderInt:
    case Control::kSliderFloat: {
      const float text_w = Dp(56.0f);
      if (Slider(app, row, width - text_w)) Save(app);
      ImGui::SameLine(0, 0);
      const std::string text = SliderText(m, row);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + text_w - ImGui::CalcTextSize(text.c_str()).x);
      ImGui::AlignTextToFramePadding();
      ImGui::TextColored(kMuted, "%s", text.c_str());
      break;
    }
    case Control::kText: {
      if (row.page == Page::kKeyBindings) {
        const std::array<std::string, 2> s = Slots(m, row.cvar);
        float w[2];
        for (int i = 0; i < 2; ++i) {
          const bool waiting = g_capture.cvar == row.cvar && g_capture.slot == i;
          const char* text = waiting ? "Press a key" : s[i].empty() ? "-" : s[i].c_str();
          w[i] = std::max(Dp(44.0f), ImGui::CalcTextSize(text).x + Dp(16.0f));
        }
        const float gap = Dp(6.0f);
        place(w[0] + gap + w[1]);
        for (int i = 0; i < 2; ++i) {
          if (i) ImGui::SameLine(0, gap);
          ImGui::PushID(i);
          const bool waiting = g_capture.cvar == row.cvar && g_capture.slot == i;
          if (Keycap("##key", s[i], waiting, w[i]) && !waiting) g_capture = {row.cvar, i};
          ImGui::PopID();
        }
        break;
      }
      auto& buf = g_text[row.cvar];
      ImGui::SetNextItemWidth(width);
      if (ImGui::InputText("##text", buf.data(), buf.size())) {
        m.Set(row.cvar, buf.data());
        Save(app);
      }
      break;
    }
    case Control::kAction: break;
  }
  ImGui::PopID();
}

// The label with its help underneath on the left, the control on the right, a line below.
void DrawRow(App& app, const opt::RowSpec& row) {
  const ImVec2 top = ImGui::GetCursorScreenPos();
  const float full = ImGui::GetContentRegionAvail().x - Dp(12.0f);  // room for the scroll bar
  const float ctrl_w = Dp(240.0f), left_w = full - ctrl_w - Dp(24.0f);
  const float pad = Dp(7.0f);
  ImGui::SetCursorScreenPos(ImVec2(top.x, top.y + pad));
  ImGui::BeginGroup();
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + left_w);
  ImGui::TextUnformatted(row.label.For());
  if (const char* help = row.help.For(); *help) {
    ImGui::PushFont(nullptr, 13.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, Dp(2.0f)));
    ImGui::TextColored(kHint, "%s", help);
    ImGui::PopStyleVar();
    ImGui::PopFont();
  }
  ImGui::PopTextWrapPos();
  ImGui::EndGroup();
  const float left_h = ImGui::GetItemRectSize().y, ctrl_h = ImGui::GetFrameHeight();
  ImGui::SetCursorScreenPos(ImVec2(top.x + full - ctrl_w, top.y + pad + std::max(0.0f, (left_h - ctrl_h) * 0.5f)));
  DrawControl(app, row, ctrl_w);
  const float bottom = top.y + pad * 2 + std::max(left_h, ctrl_h);
  ImGui::GetWindowDrawList()->AddLine(ImVec2(top.x, bottom), ImVec2(top.x + full, bottom), Col(kRowLine), Dp(1.0f));
  ImGui::SetCursorScreenPos(ImVec2(top.x, bottom + Dp(1.0f)));
  ImGui::Dummy(ImVec2(full, 0));
}

void DrawSaveFolderBrowse(App& app) {
  opt::Model& m = *app.settings;
  const float full = ImGui::GetContentRegionAvail().x - Dp(12.0f), w = Dp(120.0f);
  ImGui::Dummy(ImVec2(0, Dp(2.0f)));
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + full - w);
  if (GhostButton("Browse...", ImVec2(w, 0))) {
    const std::string configured = m.Get("user_data_root");
    const std::string picked =
        actions::PickFolder(configured.empty() ? actions::DefaultSaveDir() : FromUtf8(configured));
    if (!picked.empty()) {
      m.Set("user_data_root", picked);
      RefreshDynamicChoices(m);
      Save(app);
    }
  }
}

void DrawSection(App& app, const opt::PageSpec& spec, bool header) {
  if (header) GroupHeader(Upper(spec.title.For()).c_str());
  for (const auto& row : spec.rows) {
    if (row.control == Control::kAction || !app.settings->IsAvailable(row.cvar)) continue;
    if (const char* group = GroupAt(row.cvar)) GroupHeader(group);
    DrawRow(app, row);
    if (std::string_view(row.cvar) == "user_data_root") DrawSaveFolderBrowse(app);
  }
}

void ApplyCapture(App& app) {
  using R = Capture::Result;
  if (g_capture.cvar.empty() || g_capture.result == R::kWaiting) return;
  const opt::RowSpec* row = opt::FindRow(g_capture.cvar);
  if (row && g_capture.result != R::kCancel) {
    opt::Model& m = *app.settings;
    if (g_capture.result == R::kKey) {
      SetKeySlot(m, *row, g_capture.slot, g_capture.key);
    } else {  // clear: the second key moves up when the first goes
      std::array<std::string, 2> s = Slots(m, row->cvar);
      s[g_capture.slot].clear();
      SetSlots(m, *row, s);
    }
    Save(app);
  }
  g_capture = {};
}

const char* TitleOf(Page page) {
  switch (page) {
    case Page::kDisplay: return "DISPLAY";
    case Page::kGraphics: return "GRAPHICS";
    case Page::kControls: return "CONTROLS";
    case Page::kNetwork: return "NETWORK";
    case Page::kSaves: return "SAVES";
    default: return "";
  }
}

void DrawFooter(App& app, Page page) {
  using namespace theme;
  const float play_w = Dp(120.0f), play_h = Dp(38.0f);
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float full = ImGui::GetContentRegionAvail().x;
  ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + full, p.y), Col(kLine), Dp(1.0f));
  ImGui::Dummy(ImVec2(0, Dp(10.0f)));
  const float y = ImGui::GetCursorPosY();

  std::string text = "Saved to gow3.toml";
  ImVec4 colour = kMuted;
  if (!app.settings_file.readable) {
    text = "gow3.toml could not be read: " + app.settings_file.error + ". Settings are read-only.";
    colour = kAccent;
  } else if (!g_capture.cvar.empty()) {
    text = "Press a key or mouse button. Esc cancels, Delete clears.";
  } else if (g_save == SaveStatus::kFailed) {
    text = "gow3.toml could not be saved.";
    colour = kAccent;
  } else if (g_save == SaveStatus::kNone && page == Page::kControls) {
    text = "Click a key to rebind";
  }
  ImGui::PushFont(nullptr, 14.0f);
  ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + full - play_w - Dp(16.0f));
  const float text_h = ImGui::CalcTextSize(text.c_str(), nullptr, false, full - play_w - Dp(16.0f)).y;
  ImGui::SetCursorPosY(y + std::max(0.0f, (play_h - text_h) * 0.5f));
  ImGui::TextColored(colour, "%s", text.c_str());
  ImGui::PopTextWrapPos();
  ImGui::PopFont();

  ImGui::SetCursorPos(ImVec2(ImGui::GetCursorStartPos().x + full - play_w, y));
  ImGui::BeginDisabled(!app.CanPlay() || GameFilesBusy());
  ImGui::PushFont(HeadingFont(), 24.0f);
  if (PrimaryButton("PLAY", ImVec2(play_w, play_h))) app.StartGameAndQuit();
  ImGui::PopFont();
  ImGui::EndDisabled();
}

}  // namespace

void DrawSettingsPage(App& app, Page page) {
  Load(app);
  ApplyCapture(app);
  PageTitle(TitleOf(page));

  const float footer_h = Dp(60.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(Dp(8.0f), Dp(4.0f)));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, Dp(1.0f));
  ImGui::PushStyleColor(ImGuiCol_Border, theme::kTrackOff);
  ImGui::BeginChild("##rows", ImVec2(0, ImGui::GetContentRegionAvail().y - footer_h));
  ImGui::BeginDisabled(!app.settings_file.readable);
  for (const auto& spec : opt::Catalog()) {
    if (spec.id == page) DrawSection(app, spec, false);
  }
  if (page == Page::kControls) {
    for (Page sub : {Page::kPadButtons, Page::kKeyBindings}) {
      for (const auto& spec : opt::Catalog()) {
        if (spec.id == sub) DrawSection(app, spec, true);
      }
    }
  }
  ImGui::EndDisabled();
  ImGui::Dummy(ImVec2(0, Dp(8.0f)));
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(2);
  DrawFooter(app, page);
}

bool SettingsCaptureMessage(UINT msg, WPARAM wparam, LPARAM lparam) {
  using R = Capture::Result;
  if (g_capture.cvar.empty() || g_capture.result != R::kWaiting) return false;
  auto done = [](R result, std::string key = {}) {
    g_capture.result = result;
    g_capture.key = std::move(key);
    return true;
  };
  int vk = 0;
  switch (msg) {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
      if (lparam & (1 << 30)) return true;  // auto-repeat of a key held when the slot was clicked
      vk = static_cast<int>(wparam);
      break;
    case WM_KEYUP:
    case WM_SYSKEYUP:
    case WM_CHAR:
    case WM_SYSCHAR: return true;
    case WM_LBUTTONDOWN: vk = VK_LBUTTON; break;
    case WM_RBUTTONDOWN: vk = VK_RBUTTON; break;
    case WM_MBUTTONDOWN: vk = VK_MBUTTON; break;
    case WM_XBUTTONDOWN: vk = GET_XBUTTON_WPARAM(wparam) == XBUTTON1 ? VK_XBUTTON1 : VK_XBUTTON2; break;
    case WM_MOUSEWHEEL: return done(R::kKey, GET_WHEEL_DELTA_WPARAM(wparam) > 0 ? "WheelUp" : "WheelDown");
    case WM_KILLFOCUS: done(R::kCancel); return false;
    default: return false;
  }
  if (vk == VK_ESCAPE) return done(R::kCancel);
  if (vk == VK_DELETE || vk == VK_BACK) return done(R::kClear);
  if (vk == VK_F10 || vk == VK_LWIN || vk == VK_RWIN) return true;  // as FirstBindableKeyDown skips them
  std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
  if (name.empty()) return true;
  return done(R::kKey, std::move(name));
}

void SettingsGameChanged(App& app) {
  NoteGameLanguages(app);
  if (app.settings) RefreshDynamicChoices(*app.settings);
}