// gow3 - ReXGlue Recompiled Project
//
// Dynamic button prompt glyph replacement for Gears of War 3.
#pragma once

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

}  // namespace rex::glue
