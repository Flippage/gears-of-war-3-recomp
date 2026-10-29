#include "theme.h"

#include <string>
#include <system_error>
#include <utility>

namespace {

ImFont* g_heading = nullptr;
ImFont* g_body = nullptr;
float g_scale = 1.0f;

ImU32 Col(const ImVec4& c) { return ImGui::GetColorU32(c); }  // honours BeginDisabled's alpha

ImVec4 Mix(const ImVec4& a, const ImVec4& b, float t) {
  return ImVec4(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, 1.0f);
}

ImFont* AddFont(ImFontAtlas* atlas, const std::filesystem::path& file, float size) {
  std::error_code ec;
  if (!std::filesystem::is_regular_file(file, ec)) return nullptr;
  const std::u8string u8 = file.u8string();
  return atlas->AddFontFromFileTTF(std::string(u8.begin(), u8.end()).c_str(), size);
}

void CentredText(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col, const char* text) {
  const ImVec2 ts = ImGui::CalcTextSize(text);
  dl->AddText(ImVec2(min.x + (max.x - min.x - ts.x) * 0.5f, min.y + (max.y - min.y - ts.y) * 0.5f), col, text);
}

}  // namespace

void LoadFonts(const std::filesystem::path& launcher_dir) {
  ImFontAtlas* atlas = ImGui::GetIO().Fonts;
  std::filesystem::path system_fonts;
  wchar_t win_dir[MAX_PATH]{};
  if (GetWindowsDirectoryW(win_dir, MAX_PATH) > 0) system_fonts = std::filesystem::path(win_dir) / L"Fonts";
  // The first font added is ImGui's default: the body font.
  if (!system_fonts.empty()) g_body = AddFont(atlas, system_fonts / L"segoeui.ttf", theme::kBodySize);
  if (!g_body) g_body = atlas->AddFontDefault();
  g_heading = AddFont(atlas, launcher_dir / L"assets" / L"fonts" / L"Teko-Variable.ttf", theme::kPageTitleSize);
  if (!g_heading && !system_fonts.empty()) {
    g_heading = AddFont(atlas, system_fonts / L"bahnschrift.ttf", theme::kPageTitleSize);
  }
  if (!g_heading) g_heading = g_body;
}

ImFont* HeadingFont() { return g_heading; }
ImFont* BodyFont() { return g_body; }
float Dp(float x) { return x * g_scale; }

void ApplyTheme(float dpi_scale) {
  using namespace theme;
  g_scale = dpi_scale;
  ImGuiStyle& s = ImGui::GetStyle();
  s = ImGuiStyle();
  s.WindowPadding = ImVec2(0, 0);
  s.WindowBorderSize = s.ChildBorderSize = s.FrameBorderSize = 0.0f;
  s.PopupBorderSize = 1.0f;
  s.WindowRounding = s.ChildRounding = 0.0f;
  s.FrameRounding = 5.0f;
  s.PopupRounding = 6.0f;
  s.GrabRounding = 4.0f;
  s.ScrollbarRounding = 5.0f;
  s.ScrollbarSize = 10.0f;
  s.FramePadding = ImVec2(9.0f, 5.0f);
  s.ItemSpacing = ImVec2(8.0f, 8.0f);
  s.ScaleAllSizes(dpi_scale);
  s.FontSizeBase = kBodySize;
  s.FontScaleDpi = dpi_scale;

  ImVec4* c = s.Colors;
  c[ImGuiCol_Text] = kText;
  c[ImGuiCol_TextDisabled] = kMuted;
  c[ImGuiCol_WindowBg] = kBackground;
  c[ImGuiCol_ChildBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_PopupBg] = kPanel;
  c[ImGuiCol_Border] = kOutline;
  c[ImGuiCol_FrameBg] = kPanel;
  c[ImGuiCol_FrameBgHovered] = kPill;
  c[ImGuiCol_FrameBgActive] = kNavActive;
  c[ImGuiCol_Button] = kPanel;
  c[ImGuiCol_ButtonHovered] = kPill;
  c[ImGuiCol_ButtonActive] = kNavActive;
  c[ImGuiCol_Header] = kNavActive;
  c[ImGuiCol_HeaderHovered] = kPill;
  c[ImGuiCol_HeaderActive] = kNavActive;
  c[ImGuiCol_CheckMark] = kAccent;
  c[ImGuiCol_SliderGrab] = kAccent;
  c[ImGuiCol_SliderGrabActive] = Mix(kAccent, kTitle, 0.2f);
  c[ImGuiCol_Separator] = kLine;
  c[ImGuiCol_ScrollbarBg] = ImVec4(0, 0, 0, 0);
  c[ImGuiCol_ScrollbarGrab] = kOutline;
  c[ImGuiCol_ScrollbarGrabHovered] = kTrackOff;
  c[ImGuiCol_ScrollbarGrabActive] = kMuted;
  c[ImGuiCol_TextSelectedBg] = ImVec4(kAccent.x, kAccent.y, kAccent.z, 0.35f);
  c[ImGuiCol_NavCursor] = kAccent;
}

bool NavItem(const char* label, bool active) {
  using namespace theme;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 size(ImGui::GetContentRegionAvail().x, Dp(34.0f));
  ImGui::PushID(label);
  const bool pressed = ImGui::InvisibleButton("##nav", size);
  ImGui::PopID();
  const bool hovered = ImGui::IsItemHovered();
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 q(p.x + size.x, p.y + size.y);
  if (active || hovered) dl->AddRectFilled(p, q, Col(active ? kNavActive : kPanel), Dp(4.0f));
  if (active) dl->AddRectFilled(p, ImVec2(p.x + Dp(2.0f), q.y), Col(kAccent));
  const float fs = ImGui::GetFontSize();
  dl->AddText(ImVec2(p.x + Dp(12.0f), p.y + (size.y - fs) * 0.5f), Col(active || hovered ? kText : kMuted), label);
  return pressed;
}

bool PrimaryButton(const char* label, ImVec2 size) {
  using namespace theme;
  if (size.x <= 0.0f) size.x = ImGui::GetContentRegionAvail().x;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(label, size);
  const ImVec4 fill = ImGui::IsItemActive()    ? Mix(kAccent, kBackground, 0.15f)
                      : ImGui::IsItemHovered() ? Mix(kAccent, kTitle, 0.12f)
                                               : kAccent;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 q(p.x + size.x, p.y + size.y);
  dl->AddRectFilled(p, q, Col(fill), Dp(6.0f));
  CentredText(dl, p, q, Col(ImVec4(1, 1, 1, 1)), label);
  return pressed;
}

bool GhostButton(const char* label, ImVec2 size) {
  using namespace theme;
  if (size.x <= 0.0f) size.x = ImGui::CalcTextSize(label).x + Dp(24.0f);
  if (size.y <= 0.0f) size.y = ImGui::GetFrameHeight();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const bool pressed = ImGui::InvisibleButton(label, size);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const ImVec2 q(p.x + size.x, p.y + size.y);
  if (ImGui::IsItemHovered()) dl->AddRectFilled(p, q, Col(ImGui::IsItemActive() ? kNavActive : kPill), Dp(6.0f));
  dl->AddRect(p, q, Col(kOutline), Dp(6.0f), 0, Dp(1.0f));
  CentredText(dl, p, q, Col(Rgb(0xC9CCD1)), label);
  return pressed;
}

bool Toggle(const char* id, bool* v) {
  using namespace theme;
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 size(Dp(36.0f), Dp(20.0f));
  const bool pressed = ImGui::InvisibleButton(id, size);
  if (pressed) *v = !*v;
  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float r = size.y * 0.5f;
  ImVec4 track = *v ? kAccent : kTrackOff;
  if (ImGui::IsItemHovered()) track = Mix(track, kTitle, 0.1f);
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Col(track), r);
  const float knob_x = *v ? p.x + size.x - r : p.x + r;
  dl->AddCircleFilled(ImVec2(knob_x, p.y + r), r - Dp(3.0f), Col(ImVec4(1, 1, 1, 1)));
  return pressed;
}

void Pill(const char* text) {
  using namespace theme;
  ImGui::PushFont(nullptr, theme::kSmallSize);
  const ImVec2 ts = ImGui::CalcTextSize(text);
  const ImVec2 pad(Dp(10.0f), Dp(3.0f));
  const ImVec2 p = ImGui::GetCursorScreenPos();
  const ImVec2 size(ts.x + pad.x * 2, ts.y + pad.y * 2);
  ImDrawList* dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), Col(kPill), size.y * 0.5f);
  dl->AddText(ImVec2(p.x + pad.x, p.y + pad.y), Col(kPillText), text);
  ImGui::Dummy(size);
  ImGui::PopFont();
}

void PageTitle(const char* text, const ImVec4& colour) {
  ImGui::PushFont(HeadingFont(), theme::kPageTitleSize);
  ImGui::TextColored(colour, "%s", text);
  ImGui::PopFont();
}

void TitleBar(HWND hwnd) {
  using namespace theme;
  // Its own window above the page, so the strip takes the mouse before the sidebar and page do.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = Dp(36.0f), bw = Dp(46.0f);
  ImGui::SetNextWindowPos(vp->Pos);
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));
  ImGui::Begin("##titlebar", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing);
  const ImVec2 o = ImGui::GetWindowPos();
  ImGui::SetCursorScreenPos(o);
  ImGui::InvisibleButton("##drag", ImVec2(vp->Size.x - 2 * bw, h));
  if (ImGui::IsItemActivated()) {
    // Windows runs the move loop; it swallows the button release, so tell ImGui afterwards.
    ReleaseCapture();
    SendMessageW(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
    ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left, false);
  }

  ImDrawList* dl = ImGui::GetWindowDrawList();
  const float icon = Dp(5.0f), line = Dp(1.0f);
  auto caption_button = [&](const char* id, float x, const ImVec4& hover_fill) {
    const ImVec2 p(o.x + x, o.y);
    ImGui::SetCursorScreenPos(p);
    const bool pressed = ImGui::InvisibleButton(id, ImVec2(bw, h));
    const bool hovered = ImGui::IsItemHovered();
    if (hovered) dl->AddRectFilled(p, ImVec2(p.x + bw, p.y + h), Col(hover_fill));
    return std::pair{pressed, hovered};
  };
  const float min_x = vp->Size.x - 2 * bw, close_x = vp->Size.x - bw;
  const auto [min_pressed, min_hovered] = caption_button("##minimise", min_x, kPill);
  const ImVec2 mc(o.x + min_x + bw * 0.5f, o.y + h * 0.5f);
  dl->AddLine(ImVec2(mc.x - icon, mc.y), ImVec2(mc.x + icon, mc.y), Col(min_hovered ? kText : kMuted), line);
  const auto [close_pressed, close_hovered] = caption_button("##close", close_x, kAccent);
  const ImVec2 cc(o.x + close_x + bw * 0.5f, o.y + h * 0.5f);
  const ImU32 x_col = Col(close_hovered ? ImVec4(1, 1, 1, 1) : kMuted);
  dl->AddLine(ImVec2(cc.x - icon, cc.y - icon), ImVec2(cc.x + icon, cc.y + icon), x_col, line);
  dl->AddLine(ImVec2(cc.x - icon, cc.y + icon), ImVec2(cc.x + icon, cc.y - icon), x_col, line);
  ImGui::End();

  if (min_pressed) ShowWindow(hwnd, SW_MINIMIZE);
  if (close_pressed) PostMessageW(hwnd, WM_CLOSE, 0, 0);
}
