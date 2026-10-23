// gow3 - ReXGlue Recompiled Project
//
// Native mouse look: captured mouse motion goes into the engine's own PC mouse axes (MouseX /
// MouseY, bound to aMouseX / aMouseY in the game's input config) instead of the emulated right
// stick, so there is no stick dead zone or stick acceleration. Adapted from the Gears of War 2
// port's gow2_mouse_aim (BSD-3-Clause, Santiago Quiroz Upegui and the gow2-recomp contributors).
#pragma once

namespace rex::ui {
class Window;
}

namespace rex::glue {

// Before the input system starts: turn the mouse-to-right-stick emulation off and give the mouse
// buttons their default bindings (fire, aim, zoom) unless the player bound them.
void PrepareNativeMouseAim();
// After setup: hook UPlayerInput::InputAxis and start banking mouse motion from the window.
// Logs and leaves the stick emulation on if the game code is not where expected.
void InstallNativeMouseAim(rex::ui::Window* window);
bool NativeMouseAimInstalled();

}  // namespace rex::glue
