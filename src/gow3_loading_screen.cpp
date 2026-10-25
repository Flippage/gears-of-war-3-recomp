// gow3 - ReXGlue Recompiled Project
//
// The startup shader loading screen, see gow3_loading_screen.h. Design by Gemini (from our brief),
// fitted to Dear ImGui 1.92 (font sizes) by Claude.

#include "gow3_loading_screen.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>

#include <imgui.h>

namespace rex::glue {

namespace {

// PC Options (F10) palette.
constexpr ImU32 kColAccent = IM_COL32(136, 62, 210, 255);
constexpr ImU32 kColAccentBright = IM_COL32(176, 112, 246, 255);
constexpr ImU32 kColTextPrimary = IM_COL32(238, 240, 245, 255);
constexpr ImU32 kColTextDim = IM_COL32(118, 124, 136, 255);

// Cog, track and mechanical finishes.
constexpr ImU32 kColCogBody = IM_COL32(48, 52, 62, 255);
constexpr ImU32 kColCogEdge = IM_COL32(96, 104, 120, 255);
constexpr ImU32 kColCogDark = IM_COL32(18, 20, 25, 255);
constexpr ImU32 kColCogBolt = IM_COL32(76, 83, 96, 255);
constexpr ImU32 kColTrackBg = IM_COL32(14, 16, 22, 255);
constexpr ImU32 kColTrackBorder = IM_COL32(36, 40, 50, 255);

constexpr float kTwoPi = 6.28318530718f;

}  // namespace

void DrawShaderLoadingScreen(ImGuiIO& io, uint32_t done, uint32_t total, float elapsed_s,
                             ImFont* title_font, ImFont* body_font) {
  const float disp_w = io.DisplaySize.x > 0.0f ? io.DisplaySize.x : 1280.0f;
  const float disp_h = io.DisplaySize.y > 0.0f ? io.DisplaySize.y : 720.0f;
  const float scale = disp_h / 1080.0f;

  ImDrawList* const dl = ImGui::GetForegroundDrawList();

  ImFont* const font_title = title_font ? title_font : ImGui::GetFont();
  ImFont* const font_body = body_font ? body_font : ImGui::GetFont();

  // Sizes in pixels at 1080p, scaled to the window.
  const float title_sz = std::max(56.0f * scale, 12.0f);
  const float counter_sz = std::max(40.0f * scale, 10.0f);
  const float hint_sz = std::max(24.0f * scale, 9.0f);

  const char* const title_text = "PREPARING SHADERS";
  const ImVec2 title_box = font_title->CalcTextSizeA(title_sz, 1e10f, 0.0f, title_text);

  char counter_text[64] = {};
  ImVec2 counter_box(0.0f, 0.0f);
  if (total > 0) {
    const unsigned pct =
        std::min(static_cast<unsigned>(static_cast<double>(done) / total * 100.0), 100u);
    std::snprintf(counter_text, sizeof(counter_text), "%u / %u  \xC2\xB7  %u%%", done, total, pct);
    counter_box = font_title->CalcTextSizeA(counter_sz, 1e10f, 0.0f, counter_text);
  }

  const char* const hint_text = "This can take a few minutes.";
  const ImVec2 hint_box = font_body->CalcTextSizeA(hint_sz, 1e10f, 0.0f, hint_text);

  const float r_outer = 44.0f * scale;
  const float cog_diam = r_outer * 2.0f;
  const float bar_w = disp_w * 0.60f;
  const float bar_h = std::max(disp_h / 70.0f, 4.0f);

  const float gap_title_cog = 26.0f * scale;
  const float gap_cog_bar = 28.0f * scale;
  const float gap_bar_counter = 16.0f * scale;
  const float gap_counter_hint = total > 0 ? 8.0f * scale : 0.0f;
  const float h_counter = total > 0 ? counter_box.y : 0.0f;

  const float total_h = title_box.y + gap_title_cog + cog_diam + gap_cog_bar + bar_h +
                        gap_bar_counter + h_counter + gap_counter_hint + hint_box.y;
  float cur_y = (disp_h - total_h) * 0.5f;

  // 1. Title.
  dl->AddText(font_title, title_sz, ImVec2((disp_w - title_box.x) * 0.5f, cur_y), kColTextPrimary,
              title_text);
  cur_y += title_box.y + gap_title_cog;

  // 2. Cog wheel, spinning.
  const ImVec2 cog_center(disp_w * 0.5f, cur_y + r_outer);
  const float rot_angle = elapsed_s * 0.75f;
  const float r_root = 34.0f * scale;
  const float r_inner = 23.5f * scale;
  const float r_hub = 14.0f * scale;
  const float r_axle = 6.0f * scale;
  const int num_teeth = 12;
  const float tooth_step = kTwoPi / static_cast<float>(num_teeth);
  const float w_tip = tooth_step * 0.16f;
  const float w_base = tooth_step * 0.28f;
  auto polar = [&](float angle, float radius) {
    return ImVec2(cog_center.x + std::cos(angle) * radius, cog_center.y + std::sin(angle) * radius);
  };

  dl->AddCircleFilled(cog_center, r_root, kColCogBody, 36);
  for (int i = 0; i < num_teeth; ++i) {
    const float mid_a = rot_angle + i * tooth_step;
    dl->AddQuadFilled(polar(mid_a - w_base, r_root), polar(mid_a - w_tip, r_outer),
                      polar(mid_a + w_tip, r_outer), polar(mid_a + w_base, r_root), kColCogBody);
  }
  for (int i = 0; i < num_teeth; ++i) {
    const float mid_a = rot_angle + i * tooth_step;
    dl->PathLineTo(polar(mid_a - w_base, r_root));
    dl->PathLineTo(polar(mid_a - w_tip, r_outer));
    dl->PathLineTo(polar(mid_a + w_tip, r_outer));
    dl->PathLineTo(polar(mid_a + w_base, r_root));
    dl->PathLineTo(polar(mid_a + tooth_step * 0.5f, r_root));
  }
  dl->PathStroke(kColCogEdge, ImDrawFlags_Closed, std::max(1.0f, 1.2f * scale));

  dl->AddCircle(cog_center, 27.5f * scale, kColCogDark, 32, 1.0f * scale);
  dl->AddCircle(cog_center, 19.5f * scale, kColCogDark, 32, 1.0f * scale);
  for (int b = 0; b < 6; ++b) {
    const ImVec2 b_pos = polar(rot_angle + b * (kTwoPi / 6.0f), r_inner);
    const float b_r = 3.0f * scale;
    dl->AddCircleFilled(b_pos, b_r, kColCogDark, 16);
    dl->AddCircle(b_pos, b_r, kColCogEdge, 16, 1.0f * scale);
    dl->AddCircleFilled(b_pos, b_r * 0.45f, kColCogBolt, 8);
  }
  dl->AddCircleFilled(cog_center, r_hub, kColCogBody, 24);
  dl->AddCircle(cog_center, r_hub, kColCogEdge, 24, 1.2f * scale);
  dl->AddCircleFilled(cog_center, r_axle, kColCogDark, 20);
  dl->AddCircle(cog_center, r_axle, kColCogEdge, 20, 1.0f * scale);
  cur_y += cog_diam + gap_cog_bar;

  // 3. Progress bar.
  const float bar_x = (disp_w - bar_w) * 0.5f;
  const float bar_y = cur_y;
  const float rounding = std::min(bar_h * 0.5f, 3.0f * scale);
  dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), kColTrackBg,
                    rounding);
  dl->AddRect(ImVec2(bar_x, bar_y), ImVec2(bar_x + bar_w, bar_y + bar_h), kColTrackBorder,
              rounding, 0, 1.0f * scale);

  // Eased towards done / total, never backwards.
  static float s_shown_fraction = 0.0f;
  if (total == 0 || elapsed_s <= io.DeltaTime) {
    s_shown_fraction = 0.0f;
  }
  if (total > 0) {
    const float target = std::clamp(static_cast<float>(done) / static_cast<float>(total), 0.0f, 1.0f);
    if (target > s_shown_fraction) {
      const float t = 1.0f - std::exp(-7.0f * std::max(io.DeltaTime, 0.0f));
      s_shown_fraction = std::min(target, s_shown_fraction + (target - s_shown_fraction) * t);
    }
    const float fill_w = bar_w * s_shown_fraction;
    if (fill_w > 0.5f) {
      dl->AddRectFilled(ImVec2(bar_x, bar_y), ImVec2(bar_x + fill_w, bar_y + bar_h), kColAccent,
                        rounding);
      dl->AddLine(ImVec2(bar_x + 1.0f, bar_y + 1.0f), ImVec2(bar_x + fill_w - 1.0f, bar_y + 1.0f),
                  IM_COL32(255, 255, 255, 45), 1.0f);
      dl->AddLine(ImVec2(bar_x + fill_w, bar_y), ImVec2(bar_x + fill_w, bar_y + bar_h),
                  kColAccentBright, std::max(2.0f * scale, 1.5f));
    }
  } else {
    // Shader count not known yet: a segment slides back and forth.
    const float seg_w = bar_w * 0.18f;
    const float slide = 0.5f + 0.5f * std::sin(elapsed_s * 2.6f);
    const float seg_x = bar_x + (bar_w - seg_w) * slide;
    dl->AddRectFilled(ImVec2(seg_x, bar_y), ImVec2(seg_x + seg_w, bar_y + bar_h), kColAccent,
                      rounding);
    dl->AddRect(ImVec2(seg_x, bar_y), ImVec2(seg_x + seg_w, bar_y + bar_h), kColAccentBright,
                rounding, 0, 1.2f * scale);
  }
  cur_y += bar_h + gap_bar_counter;

  // 4. "done / total · NN%".
  if (total > 0) {
    dl->AddText(font_title, counter_sz, ImVec2((disp_w - counter_box.x) * 0.5f, cur_y),
                kColTextPrimary, counter_text);
    cur_y += h_counter + gap_counter_hint;
  }

  // 5. Hint.
  dl->AddText(font_body, hint_sz, ImVec2((disp_w - hint_box.x) * 0.5f, cur_y), kColTextDim,
              hint_text);
}

}  // namespace rex::glue
