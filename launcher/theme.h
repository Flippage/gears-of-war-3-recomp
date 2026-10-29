#pragma once
// The launcher's look (mockup A): colours, fonts and the few custom widgets every page uses.
#include <windows.h>

#include <filesystem>

#include <imgui.h>

namespace theme {

inline constexpr ImVec4 Rgb(unsigned rgb) {
  return ImVec4(((rgb >> 16) & 0xFF) / 255.0f, ((rgb >> 8) & 0xFF) / 255.0f, (rgb & 0xFF) / 255.0f, 1.0f);
}

inline constexpr ImVec4 kBackground = Rgb(0x0E0F11), kSidebar = Rgb(0x121417), kPanel = Rgb(0x15171A),
                        kLine = Rgb(0x1C1F23), kText = Rgb(0xD7D9DC), kMuted = Rgb(0x7B8088),
                        kAccent = Rgb(0xD23B3B), kSuccess = Rgb(0x5FBF7A);
// Shades from the mockup: headings, the active nav item, outlines, pills, the switch track when off.
inline constexpr ImVec4 kTitle = Rgb(0xF2F2F2), kNavActive = Rgb(0x1D2025), kOutline = Rgb(0x2C3036),
                        kPill = Rgb(0x1B1E22), kPillText = Rgb(0x9AA0A8), kTrackOff = Rgb(0x2A2E34);

// Unscaled sizes (multiplied by the DPI scale through style.FontScaleDpi).
inline constexpr float kBodySize = 16.0f, kPageTitleSize = 40.0f;

}  // namespace theme

// Teko from <launcher dir>\assets\fonts (else Bahnschrift), Segoe UI from %WINDIR%\Fonts (else
// ImGui's default font). Call once, before the first frame.
void LoadFonts(const std::filesystem::path& launcher_dir);
// Colours from the global constraints, sizes times dpi_scale. Call again on a DPI change.
void ApplyTheme(float dpi_scale);
ImFont* HeadingFont();
ImFont* BodyFont();
// Sizes in unscaled pixels: x times the current DPI scale.
float Dp(float x);

bool NavItem(const char* label, bool active);  // crimson 2 px left edge when active
bool PrimaryButton(const char* label, ImVec2 size);  // accent fill, white text
bool GhostButton(const char* label, ImVec2 size);
bool Toggle(const char* id, bool* v);
void Pill(const char* text);
// A page heading in Teko capitals.
void PageTitle(const char* text, const ImVec4& colour = theme::kTitle);
// Drag area, minimise and close along the top of the window.
void TitleBar(HWND hwnd);
