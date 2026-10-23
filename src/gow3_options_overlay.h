// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.
//
// In-game PC Options overlay for Gears of War 3.
// Opens on F10 (keyboard) or Back + Start held together (controller), blocks
// guest input while visible via xam_input_block, applies live cvars immediately,
// and saves modified settings to gow3.toml when closed.
#pragma once

#include <memory>

#include <rex/rex_app.h>
#include <rex/ui/imgui_dialog.h>
#include <rex/ui/imgui_drawer.h>

struct ImGuiIO;

namespace rex::glue {

// Adds crisp TrueType body and header fonts to `io` before the ImGui font
// atlas is built (called from Gow3App::OnConfigureFonts).
void AddOptionsFonts(ImFontAtlas* atlas);

// Creates the persistent PC Options ImGui dialog attached to `drawer`.
std::unique_ptr<rex::ui::ImGuiDialog> CreateOptionsOverlay(rex::ui::ImGuiDrawer* drawer,
                                                           const rex::PathConfig& paths);

// Flushes any open PC Options edits to gow3.toml and restores xam_input_block
// before process shutdown.
void ClosePcOptionsForShutdown();

}  // namespace rex::glue
