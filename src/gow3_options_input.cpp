// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_input.h"

namespace rex::glue::options {
namespace {

constexpr int16_t kStickDeadzone = 16000;

int StickDirection(int16_t v) {
  if (v > kStickDeadzone) {
    return +1;
  }
  if (v < -kStickDeadzone) {
    return -1;
  }
  return 0;
}

bool Pressed(uint16_t now, uint16_t prev, uint16_t mask) {
  return (now & mask) != 0 && (prev & mask) == 0;
}

}  // namespace

MenuEdges InputTracker::Step(const RawInput& now, bool menu_is_open) {
  MenuEdges out{};

  // Open/close chord: Back + Start held together on the controller, or F10 on keyboard.
  constexpr uint16_t kChord = kMaskBack | kMaskStart;
  const bool chord_down = (now.pad_buttons & kChord) == kChord;
  if (chord_down && !chord_latched_) {
    out.toggle_open = true;
    chord_latched_ = true;
  } else if (!chord_down) {
    chord_latched_ = false;
  }

  if (now.key_f10 && !prev_.key_f10) {
    out.toggle_open = true;
  }

  const int stick_y = StickDirection(now.thumb_ly);
  const int stick_x = StickDirection(now.thumb_lx);

  if (menu_is_open) {
    // Cancel (close menu): Esc on keyboard (unless typing in a text field) or B on pad.
    if ((now.key_escape && !prev_.key_escape && !now.text_input_active) ||
        Pressed(now.pad_buttons, prev_.pad_buttons, kMaskB)) {
      out.cancel = true;
    }

    if (!now.text_input_active) {
      // Confirm: Enter/Space or A.
      if ((now.key_enter && !prev_.key_enter) ||
          Pressed(now.pad_buttons, prev_.pad_buttons, kMaskA)) {
        out.confirm = true;
      }

      // Row navigation: Up/Down arrows, D-pad Up/Down, or left stick Y edge.
      if ((now.key_up && !prev_.key_up) ||
          Pressed(now.pad_buttons, prev_.pad_buttons, kMaskDpadUp) ||
          (stick_y > 0 && prev_stick_y_ <= 0)) {
        out.move_row = -1;
      } else if ((now.key_down && !prev_.key_down) ||
                 Pressed(now.pad_buttons, prev_.pad_buttons, kMaskDpadDown) ||
                 (stick_y < 0 && prev_stick_y_ >= 0)) {
        out.move_row = +1;
      }

      // Value adjustment: Left/Right arrows, D-pad Left/Right, or left stick X edge.
      if ((now.key_left && !prev_.key_left) ||
          Pressed(now.pad_buttons, prev_.pad_buttons, kMaskDpadLeft) ||
          (stick_x < 0 && prev_stick_x_ >= 0)) {
        out.adjust = -1;
      } else if ((now.key_right && !prev_.key_right) ||
                 Pressed(now.pad_buttons, prev_.pad_buttons, kMaskDpadRight) ||
                 (stick_x > 0 && prev_stick_x_ <= 0)) {
        out.adjust = +1;
      }

      // Tab / page switch: LB / RB or Tab / Shift+Tab.
      if (Pressed(now.pad_buttons, prev_.pad_buttons, kMaskLeftShoulder) ||
          (now.key_tab && !prev_.key_tab && now.key_shift)) {
        out.switch_page = -1;
      } else if (Pressed(now.pad_buttons, prev_.pad_buttons, kMaskRightShoulder) ||
                 (now.key_tab && !prev_.key_tab && !now.key_shift)) {
        out.switch_page = +1;
      }
    }
  }

  prev_ = now;
  prev_stick_y_ = stick_y;
  prev_stick_x_ = stick_x;
  return out;
}

}  // namespace rex::glue::options
