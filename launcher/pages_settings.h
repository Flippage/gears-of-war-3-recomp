#pragma once
// The settings pages: the in-game F10 options catalog over the gow3.toml next to the launcher.
#include <windows.h>

#include "gow3_options_catalog.h"

struct App;

// Display, Graphics, Controls (followed by the controller buttons and the key bindings), Network
// or Saves. Reads gow3.toml the first time; every change is saved straight away.
void DrawSettingsPage(App& app, rex::glue::options::Page page);
// While a key-binding slot waits for a key, WndProc hands it the key and mouse button messages
// first. True when the message was taken (ImGui doesn't see it then).
bool SettingsCaptureMessage(UINT msg, WPARAM wparam, LPARAM lparam);
