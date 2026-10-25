// gow3 - ReXGlue Recompiled Project
//
// Dynamic button prompt glyph replacement for Gears of War 3.
#pragma once

#include <filesystem>
#include <string>

#include <rex/cvar.h>
#include <rex/ui/window.h>

REXCVAR_DECLARE(std::string, gow3_button_prompts);

namespace rex::glue {

// Initializes texture replacement settings before the GPU plugin loads.
void PrepareButtonGlyphs();

// Installs the window listener for keyboard/mouse detection and starts the worker thread.
void InstallButtonGlyphs(rex::ui::Window* window);

// Shuts down the background worker thread.
void ShutdownButtonGlyphs();

// Notified when keyboard or mouse input is detected.
void NotifyKeyboardMouseInput();

// Periodically evaluates input device and cvar to update texture_replace_variant.
void PollButtonPrompts();

// Returns the active controller glyph style: "playstation" or "xbox".
std::string GetActiveControllerGlyphStyle();

// Finds the glyph PNG path for a given style ("xbox", "playstation", "keyboard") and button/key name.
std::filesystem::path FindGlyphFile(const std::string& style, const std::string& button_or_key);

// Wakes the background worker thread to immediately recompose keyboard prompt atlases on keybind change.
void NotifyBindingsChanged();

}  // namespace rex::glue
