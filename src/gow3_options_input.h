// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// Pure, testable input translator for the PC Options overlay.
// Turns a snapshot of host gamepad + keyboard state into discrete menu edges
// (open/close toggle via F10 or Back+Start chord, D-pad / stick / arrow
// navigation, LB/RB or Tab page switching, A/Enter confirm, B/Esc cancel).
#pragma once

#include <cstdint>

namespace rex::glue::options {

// Matches X_INPUT_GAMEPAD_* bit masks without pulling in runtime headers.
enum PadMask : uint16_t {
  kMaskDpadUp = 0x0001,
  kMaskDpadDown = 0x0002,
  kMaskDpadLeft = 0x0004,
  kMaskDpadRight = 0x0008,
  kMaskStart = 0x0010,
  kMaskBack = 0x0020,
  kMaskLeftThumb = 0x0040,
  kMaskRightThumb = 0x0080,
  kMaskLeftShoulder = 0x0100,
  kMaskRightShoulder = 0x0200,
  kMaskA = 0x1000,
  kMaskB = 0x2000,
  kMaskX = 0x4000,
  kMaskY = 0x8000,
};

struct RawInput {
  // Controller (XInput / SDL merged state for user 0).
  uint16_t pad_buttons = 0;
  int16_t thumb_lx = 0;
  int16_t thumb_ly = 0;

  // Keyboard state for this frame.
  bool key_f10 = false;
  bool key_escape = false;
  bool key_up = false;
  bool key_down = false;
  bool key_left = false;
  bool key_right = false;
  bool key_enter = false;
  bool key_tab = false;
  bool key_shift = false;
  bool key_delete = false;
  bool key_backspace = false;

  // True when an ImGui text widget currently owns keyboard input.
  bool text_input_active = false;
};

struct MenuEdges {
  bool toggle_open = false;  // F10 or Back+Start chord pressed this frame.
  bool cancel = false;       // Esc or B pressed while open.
  bool confirm = false;      // Enter / Space or A pressed while open.
  bool clear_slot = false;    // Delete / Backspace or X pressed on a binding slot.
  int move_row = 0;          // -1 up, +1 down.
  int adjust = 0;            // -1 left, +1 right.
  int switch_page = 0;       // -1 previous tab (LB / Shift+Tab), +1 next tab (RB / Tab).
};

class InputTracker {
 public:
  MenuEdges Step(const RawInput& now, bool menu_is_open);

 private:
  RawInput prev_{};
  bool chord_latched_ = false;
  int prev_stick_y_ = 0;
  int prev_stick_x_ = 0;
};

}  // namespace rex::glue::options
