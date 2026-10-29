#include "pages_game.h"

#include <algorithm>
#include <string>
#include <system_error>
#include <vector>

#include "app.h"
#include "setup/finder.h"
#include "setup/path_util.h"
#include "setup/settings_store.h"
#include "setup/stfs.h"
#include "theme.h"

namespace {

constexpr const char* kDot = " \xC2\xB7 ";  // " · "

struct PlayInfo {
  std::string content;   // "Title Update 6 · RAAM's Shadow · ..."
  std::string renderer;  // "Native · 2x"
};
PlayInfo g_play;

// Display names of the Gears 3 DLC packages in <game>\DLC, sorted.
std::vector<std::string> InstalledDlc(const std::filesystem::path& game) {
  using namespace gow3::setup;
  std::vector<std::string> names;
  std::error_code ec;
  for (std::filesystem::directory_iterator it(game / "DLC", ec), end; !ec && it != end; it.increment(ec)) {
    if (!it->is_regular_file(ec)) continue;
    const auto h = ReadStfsHeader(it->path());
    if (!h || h->title_id != kGow3TitleId || h->content_type != kDlcType) continue;
    names.push_back(h->display_name.empty() ? Utf8(it->path().filename()) : h->display_name);
  }
  std::sort(names.begin(), names.end());
  return names;
}

std::string RendererText(const std::filesystem::path& toml) {
  const gow3::setup::SettingsFile f = gow3::setup::LoadSettings(toml);
  auto value = [&](const char* key, const char* def) {
    const auto it = f.values.find(key);
    return it == f.values.end() ? std::string(def) : it->second;
  };
  std::string renderer = value("gow3_renderer", "native");
  if (renderer == "native") renderer = "Native";
  else if (renderer == "rtv") renderer = "RTV";
  else if (renderer == "rov") renderer = "ROV";
  return renderer + kDot + value("resolution_scale", "1") + "x";
}

// The tail of `text` that fits in max_width, with a leading ellipsis when cut.
std::string FitLeft(const std::string& text, float max_width) {
  if (ImGui::CalcTextSize(text.c_str()).x <= max_width) return text;
  const std::string ellipsis = "\xE2\x80\xA6";
  for (size_t i = 1; i < text.size(); ++i) {
    if ((static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) continue;  // inside a UTF-8 sequence
    const std::string cut = ellipsis + text.substr(i);
    if (ImGui::CalcTextSize(cut.c_str()).x <= max_width) return cut;
  }
  return ellipsis;
}

// A label on the left, a muted value (or a pill) on the right, a line below.
void Row(const char* label, const std::string& value, bool pill = false) {
  using namespace theme;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const float w = ImGui::GetContentRegionAvail().x, h = Dp(42.0f), fs = ImGui::GetFontSize();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddText(ImVec2(p.x, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(kText), label);
  if (pill) {
    ImGui::PushFont(nullptr, 13.0f);
    const ImVec2 size(ImGui::CalcTextSize(value.c_str()).x + Dp(20.0f), ImGui::GetFontSize() + Dp(6.0f));
    ImGui::PopFont();
    ImGui::SetCursorScreenPos(ImVec2(p.x + w - size.x, p.y + (h - size.y) * 0.5f));
    Pill(value.c_str());
  } else {
    const float label_w = ImGui::CalcTextSize(label).x + Dp(24.0f);
    const std::string shown = FitLeft(value, w - label_w);
    const float vw = ImGui::CalcTextSize(shown.c_str()).x;
    dl->AddText(ImVec2(p.x + w - vw, p.y + (h - fs) * 0.5f), ImGui::GetColorU32(kMuted), shown.c_str());
  }
  dl->AddLine(ImVec2(p.x, p.y + h - Dp(1.0f)), ImVec2(p.x + w, p.y + h - Dp(1.0f)), ImGui::GetColorU32(kLine),
              Dp(1.0f));
  ImGui::SetCursorScreenPos(p);
  ImGui::Dummy(ImVec2(w, h));
}

}  // namespace

void RefreshPlayPage(const App& app) {
  using gow3::setup::Build;
  g_play = {};
  if (app.state.build == Build::kTu6) {
    g_play.content = "Title Update 6";
    if (!app.game.empty()) {
      for (const std::string& dlc : InstalledDlc(app.game)) g_play.content += kDot + dlc;
    }
  } else {
    g_play.content = "Base game";
  }
  g_play.renderer = RendererText(app.dir / "gow3.toml");
}

void DrawPlayPage(App& app) {
  using namespace theme;
  const bool ready = app.CanPlay();
  PageTitle(ready ? "READY" : "SETUP NEEDED", ready ? kTitle : kAccent);
  std::string line = g_play.content;
  if (app.game.empty()) line = "Choose your game folder on the Game files page.";
  else if (!ready) line = "gow3.exe is missing from the launcher's folder.";
  ImGui::TextColored(kMuted, "%s", line.c_str());
  ImGui::Dummy(ImVec2(0, Dp(10.0f)));

  Row("Game folder", app.game.empty() ? std::string("Not set") : gow3::setup::Utf8(app.game));
  Row("Build", app.state.build == gow3::setup::Build::kTu6 ? "TU6" : "BASE", true);
  Row("Renderer", g_play.renderer);

  const float button_h = Dp(54.0f);
  const float bottom = ImGui::GetWindowHeight() - ImGui::GetStyle().WindowPadding.y;
  if (!app.error.empty()) {
    ImGui::SetCursorPosY(bottom - button_h - ImGui::GetTextLineHeightWithSpacing() - Dp(4.0f));
    ImGui::TextColored(kAccent, "%s", app.error.c_str());
  }
  ImGui::SetCursorPosY(bottom - button_h);
  ImGui::BeginDisabled(!ready);
  ImGui::PushFont(HeadingFont(), 30.0f);
  if (PrimaryButton("PLAY", ImVec2(0, button_h))) app.StartGameAndQuit();
  ImGui::PopFont();
  ImGui::EndDisabled();
}
