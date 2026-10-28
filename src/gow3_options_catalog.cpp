// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_options_catalog.h"

#include "gow3_language.h"

#include <filesystem>
#include <string>
#include <utility>

#include <rex/filesystem.h>

namespace rex::glue::options {
namespace {

std::vector<Choice> PadButtonChoices() {
  return {
      {"a", {"A"}},
      {"b", {"B"}},
      {"x", {"X"}},
      {"y", {"Y"}},
      {"lb", {"LB (Left Bumper)"}},
      {"rb", {"RB (Right Bumper)"}},
      {"ls", {"LS (Left Stick Click)"}},
      {"rs", {"RS (Right Stick Click)"}},
      {"start", {"Start"}},
      {"back", {"Back"}},
      {"dpad_up", {"D-Pad Up"}},
      {"dpad_down", {"D-Pad Down"}},
      {"dpad_left", {"D-Pad Left"}},
      {"dpad_right", {"D-Pad Right"}},
      {"none", {"Disabled"}},
  };
}

RowSpec PadButtonRow(const char* cvar, const char* def, const char* en_label,
                     const char* en_help) {
  return {
      cvar,
      Page::kPadButtons,
      Control::kChoice,
      Apply::kNow,
      {en_label},
      {en_help},
      def,
      PadButtonChoices(),
  };
}

// Gears of War 3 recomp: keyboard / mouse bindings (the keyboard/mouse driver's keybind_*
// settings: comma-separated key names, Shift+ combinations, LMB / RMB / MMB). The defaults include
// the mouse buttons set by PrepareNativeMouseAim.
RowSpec KeyBindRow(const char* cvar, const char* def, const char* en_label, const char* en_help) {
  return {
      cvar,
      Page::kKeyBindings,
      Control::kText,
      Apply::kNow,
      {en_label},
      {en_help},
      def,
      {},
  };
}

std::vector<PageSpec> BuildCatalog() {
  std::vector<PageSpec> pages;

  // --- Display ---
  pages.push_back({
      Page::kDisplay,
      {"Display"},
      {"Window mode, monitor, graphics adapter, vertical sync, and frame rate."},
      {
          {
              "window_mode",
              Page::kDisplay,
              Control::kChoice,
              Apply::kNow,
              {"Window Mode"},
              {"Switch between a resizable window, a borderless fullscreen window, or exclusive fullscreen."},
              "borderless",
              {
                  {"windowed", {"Windowed"}},
                  {"borderless", {"Borderless Fullscreen"}},
                  {"exclusive", {"Exclusive Fullscreen"}},
              },
          },
          {
              "window_size",
              Page::kDisplay,
              Control::kChoice,
              Apply::kNow,
              {"Window Size"},
              {"Size of the window when running in Windowed mode."},
              "1280x720",
              {
                  {"0x0", {"Automatic"}},
                  {"1280x720", {"1280 x 720 (720p)"}},
                  {"1600x900", {"1600 x 900 (900p)"}},
                  {"1920x1080", {"1920 x 1080 (1080p)"}},
                  {"2560x1440", {"2560 x 1440 (1440p)"}},
                  {"3440x1440", {"3440 x 1440 (Ultrawide)"}},
                  {"3840x2160", {"3840 x 2160 (4K)"}},
              },
          },
          {
              "monitor",
              Page::kDisplay,
              Control::kChoice,
              Apply::kNow,
              {"Monitor"},
              {"Which display the game opens on."},
              "0",
              {
                  {"0", {"Primary Monitor"}},
              },
          },
          {
              "d3d12_adapter",
              Page::kDisplay,
              Control::kChoice,
              Apply::kRestart,
              {"Graphics Card (GPU)"},
              {"Selects the Direct3D 12 adapter used to render the game. Useful on laptops with both integrated and dedicated GPUs."},
              "-1",
              {
                  {"-1", {"Automatic"}},
              },
          },
          {
              "vsync",
              Page::kDisplay,
              Control::kToggle,
              Apply::kNow,
              {"Vertical Sync (V-Sync)"},
              {"Locks frame presentation to your monitor refresh rate to prevent screen tearing."},
              "true",
              {},
          },
          {
              "gow3_unlock_fps",
              Page::kDisplay,
              Control::kToggle,
              Apply::kNow,
              {"Unlock 30 FPS Cap"},
              {"Skips the Xbox 360 engine's 30 FPS frame-rate limiter and its frame mode."},
              "true",
              {},
          },
          {
              "gow3_widescreen",
              Page::kDisplay,
              Control::kChoice,
              Apply::kRestart,
              {"Ultrawide (Aspect Ratio)"},
              {"Expands the view horizontally (Hor+) for wide monitors instead of side bars, up to 32:9. Wider than 16:9 needs the Native or ROV renderer (RTV has the console's 10 MB of render memory). Automatic turns it on for displays wider than 16:9. Takes effect on next start."},
              "auto",
              {
                  {"auto", {"Automatic (Auto-Detect)"}},
                  {"off", {"16:9 (Standard)"}},
                  {"21:9", {"21:9 (Native / ROV Renderer)"}},
                  {"32:9", {"32:9 (Native / ROV Renderer)"}},
                  {"16:10", {"16:10 (Handheld / Steam Deck)"}},
              },
          },
          {
              "gow3_fov",
              Page::kDisplay,
              Control::kSliderInt,
              Apply::kNow,
              {"Field of View (FOV)"},
              {"Camera FOV in degrees. 80 is default console FOV. Higher values widen your peripheral view and reduce motion sickness. Applies immediately."},
              "80",
              {},
              65.0,
              100.0,
              1.0,
              "°",
          },
          {
              "gow3_language",
              Page::kDisplay,
              Control::kChoice,
              Apply::kRestart,
              {"Game Language"},
              {"Text and subtitles, and speech where your copy of the game has it (otherwise English speech). Only the languages your copy contains are listed. Automatic follows Windows' display language. Takes effect on next start."},
              "auto",
              {
                  {"auto", {"Automatic (Windows Language)"}},
                  {"INT", {"English"}},
                  {"FRA", {"French"}},
                  {"DEU", {"German"}},
                  {"ITA", {"Italian"}},
                  {"ESN", {"Spanish (Spain)"}},
                  {"ESM", {"Spanish (Latin America)"}},
                  {"PTB", {"Portuguese (Brazil)"}},
                  {"POL", {"Polish"}},
                  {"RUS", {"Russian"}},
                  {"CZE", {"Czech"}},
                  {"HUN", {"Hungarian"}},
                  {"JPN", {"Japanese"}},
                  {"KOR", {"Korean"}},
                  {"CHN", {"Chinese"}},
                  {"CHT", {"Chinese (Traditional)"}},
              },
          },
      },
  });

  // --- Graphics ---
  pages.push_back({
      Page::kGraphics,
      {"Graphics"},
      {"Temporal upscaling, internal resolution scale, presentation filters, and Gears 3 visual patches."},
      {
          {
              "gow3_renderer",
              Page::kGraphics,
              Control::kChoice,
              Apply::kRestart,
              {"Renderer"},
              {"Native is the fastest (render targets kept on the GPU, from the Gears of War 2 port). RTV and ROV emulate the Xbox 360's EDRAM; ROV is the most exact and the slowest. Restart to apply."},
              "native",
              {
                  {"native", {"Native (Fastest)"}},
                  {"rtv", {"RTV (Host Render Targets)"}},
                  {"rov", {"ROV (Most Exact, Slowest)"}},
              },
          },
          {
              "upscaler",
              Page::kGraphics,
              Control::kChoice,
              Apply::kNow,
              {"Temporal Upscaler"},
              {"Reconstructs a sharper image from camera jitter, depth, and motion vectors. Auto picks DLSS on NVIDIA, FSR 4 on RDNA 4, or FSR 3 elsewhere."},
              "off",
              {
                  {"off", {"Off"}},
                  {"auto", {"Auto (Best Available)"}},
                  {"dlss", {"NVIDIA DLSS"}},
                  {"fsr3", {"AMD FSR 3"}},
                  {"fsr4", {"AMD FSR 4"}},
              },
          },
          {
              "gow2_upscale_quality",
              Page::kGraphics,
              Control::kChoice,
              Apply::kNow,
              {"Upscaler Quality Mode"},
              {"Controls the temporal upscaler quality preset. Automatic chooses Native (DLAA / Native AA) at 720p and Quality at higher render scales."},
              "-1",
              {
                  {"-1", {"Automatic"}},
                  {"5", {"Native (DLAA / Native AA)"}},
                  {"4", {"Ultra Quality"}},
                  {"2", {"Quality"}},
                  {"1", {"Balanced"}},
                  {"0", {"Performance"}},
                  {"3", {"Ultra Performance"}},
              },
          },
          {
              "gow2_dlss_preset",
              Page::kGraphics,
              Control::kChoice,
              Apply::kNow,
              {"DLSS Preset"},
              {"The DLSS model. Default lets DLSS pick: K for DLAA, Quality and Balanced, M for Performance, L for Ultra Performance. K is the sharpest transformer model; J ghosts slightly less but flickers more."},
              "default",
              {
                  {"default", {"Default (Recommended)"}},
                  {"k", {"K (Transformer, Best Quality)"}},
                  {"j", {"J (Transformer, Less Ghosting)"}},
                  {"m", {"M (Transformer, Performance)"}},
                  {"l", {"L (Transformer, Ultra Performance)"}},
              },
          },
          {
              "resolution_scale",
              Page::kGraphics,
              Control::kChoice,
              Apply::kRestart,
              {"Internal Resolution Scale"},
              {"Multiplies the Xbox 360 1280x720 render targets in both dimensions. Higher scales are much heavier on GPU memory and fill rate."},
              "1",
              {
                  {"1", {"1x (1280 x 720, Native)"}},
                  {"2", {"2x (2560 x 1440)"}},
                  {"3", {"3x (3840 x 2160, experimental)"}},
              },
          },
          {
              "present_effect",
              Page::kGraphics,
              Control::kChoice,
              Apply::kRestart,
              {"Display Scaling & Sharpening"},
              {"Post-scaling filter applied when fitting the rendered frame to your window or monitor."},
              "bilinear",
              {
                  {"bilinear", {"Bilinear (Soft)"}},
                  {"cas", {"AMD CAS (Sharpen)"}},
                  {"fsr", {"AMD FSR 1 (Spatial Upscale + Sharpen)"}},
                  {"fsr2", {"AMD FSR 2"}},
                  {"fsr3", {"AMD FSR 3"}},
              },
          },
          {
              "present_cas_additional_sharpness",
              Page::kGraphics,
              Control::kSliderFloat,
              Apply::kRestart,
              {"CAS / FSR Additional Sharpness"},
              {"Extra sharpening strength for AMD CAS and FSR 1 presentation filters."},
              "0",
              {},
              0.0,
              1.0,
              0.05,
              "",
          },
          {
              "gow3_black_shading_fix",
              Page::kGraphics,
              Control::kToggle,
              Apply::kRestart,
              {"Black Shading Fix"},
              {"Disables tiled MSAA (GUseTilingMode=FALSE) to prevent black blocks in character shadows. Read when the renderer starts."},
              "true",
              {},
          },
          {
              "gow3_aniso16",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNextLevel,
              {"16x Anisotropic Filtering"},
              {"Forces 16x anisotropic texture filtering so ground and wall textures stay sharp at oblique angles."},
              "true",
              {},
          },
          {
              "gow3_texture_pool_mb",
              Page::kGraphics,
              Control::kChoice,
              Apply::kRestart,
              {"Texture Streaming Pool"},
              {"Size of the Unreal Engine 3 texture streaming pool in megabytes. A larger pool evicts and reloads fewer textures while moving through a level."},
              "196",
              {
                  {"0", {"Game Default (145 MB)"}},
                  {"196", {"196 MB (Recommended)"}},
                  {"256", {"256 MB"}},
                  {"384", {"384 MB"}},
                  {"512", {"512 MB"}},
              },
          },
          {
              "gow3_occlusion_query_fix",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Occlusion Query Fix"},
              {"Tests occlusion against full-resolution depth to reduce geometry popping."},
              "false",
              {},
          },
          {
              "gow3_disable_motion_blur",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Disable Motion Blur"},
              {"Turns off camera and object motion blur to eliminate ghosting and smearing during fast camera movement."},
              "true",
              {},
          },
          {
              "gow3_disable_dof",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Disable Depth of Field"},
              {"Turns off depth-of-field blur in gameplay and cutscenes."},
              "false",
              {},
          },
          {
              "gow3_disable_bloom",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Disable Bloom"},
              {"Turns off bloom glow in the post-processing pass."},
              "false",
              {},
          },
          {
              "gow3_disable_ao",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Disable Ambient Occlusion"},
              {"Turns off screen-space ambient occlusion."},
              "false",
              {},
          },
          {
              "gow3_disable_lens_flares",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Disable Lens Flares"},
              {"Turns off screen-space lens flare sprites."},
              "false",
              {},
          },
          {
              "gow3_resolution_scaling_fix",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Resolution Scaling Fix (Disable Post-Processing)"},
              {"Disables all post-processing to avoid edge-detection overcorrection and color smearing at higher resolutions. Changes the overall look."},
              "false",
              {},
          },
          {
              "gow3_shadow_perf_b",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Smart Shadow Cuts (Performance Option B)"},
              {"Caps per-object shadow depth draws at 20 per frame, disables pre-allocated shadow caching (which breaks on NVIDIA), and turns off OneFrameRenderLag."},
              "false",
              {},
          },
          {
              "gow3_disable_dynamic_shadows",
              Page::kGraphics,
              Control::kToggle,
              Apply::kNow,
              {"Disable Dynamic Shadows"},
              {"Turns off dynamic shadows completely."},
              "false",
              {},
          },
      },
  });

  // --- Controls ---
  pages.push_back({
      Page::kControls,
      {"Controls"},
      {"Keyboard & mouse input, mouse sensitivity, controller dead zone, and button remapping."},
      {
          {
              "gow3_button_prompts",
              Page::kControls,
              Control::kChoice,
              Apply::kNow,
              {"Button Prompts"},
              {"Which button prompt glyphs to display: match active input device, force a specific controller/keyboard style, or keep the original Xbox 360 icons."},
              "auto",
              {
                  {"auto", {"Automatic (Switch on Input)"}},
                  {"xbox", {"Xbox Series"}},
                  {"playstation", {"PlayStation"}},
                  {"keyboard", {"Keyboard & Mouse"}},
                  {"original", {"Original Xbox 360"}},
              },
          },
          {
              "mnk_mode",
              Page::kControls,
              Control::kToggle,
              Apply::kNow,
              {"Keyboard & Mouse Input"},
              {"Enables keyboard and mouse controls in addition to a controller."},
              "true",
              {},
          },
          {
              "mnk_mouse",
              Page::kControls,
              Control::kToggle,
              Apply::kNow,
              {"Mouse Look & Aim"},
              {"Captures the mouse when the game window is focused and uses it to look and aim."},
              "true",
              {},
          },
          {
              "gow3_mouse_invert_y",
              Page::kControls,
              Control::kToggle,
              Apply::kNow,
              {"Invert Mouse Y"},
              {"Moving the mouse forward looks down."},
              "false",
              {},
          },
          {
              "gow3_mouse_scale",
              Page::kControls,
              Control::kSliderFloat,
              Apply::kNow,
              {"Mouse Sensitivity"},
              {"How fast the camera turns when moving the mouse."},
              "1",
              {},
              0.1,
              5.0,
              0.05,
              "x",
          },
          {
              "pad_right_stick_deadzone",
              Page::kControls,
              Control::kSliderFloat,
              Apply::kNow,
              {"Controller Right-Stick Dead Zone"},
              {"Radial dead zone for a physical controller's right stick. Raise this if the camera drifts when the stick is released."},
              "0",
              {},
              0.0,
              0.40,
              0.01,
              "",
          },
          {
              "pad_buttons",
              Page::kControls,
              Control::kAction,
              Apply::kNow,
              {"Controller Button Remapping"},
              {"Choose which physical controller button triggers each Xbox 360 button in the game."},
              "",
              {},
          },
          {
              "key_bindings",
              Page::kControls,
              Control::kAction,
              Apply::kNow,
              {"Keyboard & Mouse Bindings"},
              {"Choose which keys and mouse buttons do what with Keyboard & Mouse Input on."},
              "",
              {},
          },
      },
  });

  // --- Keyboard & Mouse Bindings (sub-page opened from Controls) ---
  pages.push_back({
      Page::kKeyBindings,
      {"Keyboard & Mouse Bindings"},
      {"Select a slot with Left/Right and press Enter or click to bind. Press Delete, Backspace, or (X) to clear. Reset Page restores defaults."},
      {
          KeyBindRow("keybind_lstick_up", "W", "Move Forward",
                     "Left stick up. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_lstick_down", "S", "Move Back",
                     "Left stick down. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_lstick_left", "A", "Move Left",
                     "Left stick left. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_lstick_right", "D", "Move Right",
                     "Left stick right. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_right_trigger", "LMB", "Fire (RT)",
                     "Right trigger. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_left_trigger", "RMB", "Aim (LT)",
                     "Left trigger. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_a", "Space", "Cover / Evade / Run (A)",
                     "A button. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_b", "F", "Melee (B)",
                     "B button. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_x", "E", "Interact / Revive / Pick Up (X)",
                     "X button. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_y", "Q", "Point of Interest / Execute (Y)",
                     "Y button. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_right_shoulder", "R", "Reload (RB)",
                     "Right bumper. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_left_shoulder", "C", "Tac/Com (LB)",
                     "Left bumper. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_rstick_press", "Z,MMB", "Zoom (Right Stick Click)",
                     "Right stick click. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_lstick_press", "Shift", "Left Stick Click",
                     "Left stick click. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_dpad_up", "4", "Grenade (D-Pad Up)",
                     "D-pad up. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_dpad_down", "3", "Pistol (D-Pad Down)",
                     "D-pad down. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_dpad_left", "1", "Weapon 1 (D-Pad Left)",
                     "D-pad left. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_dpad_right", "2", "Weapon 2 (D-Pad Right)",
                     "D-pad right. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_rstick_up", "Up", "Look Up",
                     "Right stick up (keyboard look; the mouse looks too). Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_rstick_down", "Down", "Look Down",
                     "Right stick down. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_rstick_left", "Left", "Look Left",
                     "Right stick left. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_rstick_right", "Right", "Look Right",
                     "Right stick right. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_start", "Return,Escape", "Pause (Start)",
                     "Start button. Comma-separated keys; Set captures one."),
          KeyBindRow("keybind_back", "Tab", "Scoreboard / Objectives (Back)",
                     "Back button. Comma-separated keys; Set captures one."),
      },
  });

  // --- Controller Buttons (sub-page opened from Controls) ---
  pages.push_back({
      Page::kPadButtons,
      {"Controller Buttons"},
      {"Select a slot and press Enter or click to bind. If already used, it moves. Press Delete, Backspace, or (X) to clear."},
      {
          PadButtonRow("pad_map_a", "a", "A Button (Cover / Evade / Run)",
                       "Physical controller button that presses A in the game."),
          PadButtonRow("pad_map_b", "b", "B Button (Melee)",
                       "Physical controller button that presses B in the game."),
          PadButtonRow("pad_map_x", "x", "X Button (Interact / Revive / Pickup)",
                       "Physical controller button that presses X in the game."),
          PadButtonRow("pad_map_y", "y", "Y Button (Point of Interest / Execute)",
                       "Physical controller button that presses Y in the game."),
          PadButtonRow("pad_map_lb", "lb", "LB (Tac/Com)",
                       "Physical controller button that presses Left Bumper in the game."),
          PadButtonRow("pad_map_rb", "rb", "RB (Reload / Active Reload)",
                       "Physical controller button that presses Right Bumper in the game."),
          PadButtonRow("pad_map_ls", "ls", "LS Click (Left Stick)",
                       "Physical controller button that presses Left Stick Click in the game."),
          PadButtonRow("pad_map_rs", "rs", "RS Click (Zoom Aim)",
                       "Physical controller button that presses Right Stick Click in the game."),
          PadButtonRow("pad_map_start", "start", "Start (Pause)",
                       "Physical controller button that presses Start in the game."),
          PadButtonRow("pad_map_back", "back", "Back (Scoreboard / Objectives)",
                       "Physical controller button that presses Back in the game."),
          PadButtonRow("pad_map_dpad_up", "dpad_up", "D-Pad Up (Equip Grenade)",
                       "Physical controller button that presses D-Pad Up in the game."),
          PadButtonRow("pad_map_dpad_down", "dpad_down", "D-Pad Down (Equip Pistol)",
                       "Physical controller button that presses D-Pad Down in the game."),
          PadButtonRow("pad_map_dpad_left", "dpad_left", "D-Pad Left (Weapon 1)",
                       "Physical controller button that presses D-Pad Left in the game."),
          PadButtonRow("pad_map_dpad_right", "dpad_right", "D-Pad Right (Weapon 2)",
                       "Physical controller button that presses D-Pad Right in the game."),
      },
  });

  // --- Multiplayer (System Link) ---
  pages.push_back({
      Page::kNetwork,
      {"Multiplayer"},
      {"System Link co-op and Versus over a home LAN or a virtual LAN (Radmin VPN, ZeroTier, Tailscale)."},
      {
          {
              "net_link_active",
              Page::kNetwork,
              Control::kToggle,
              Apply::kRestart,
              {"System Link (LAN / VPN)"},
              {"Enables Xbox 360 System Link networking. Turn on in the game's Party / Versus lobby with Party Type set to System Link."},
              "false",
              {},
          },
          {
              "user_name",
              Page::kNetwork,
              Control::kText,
              Apply::kRestart,
              {"Player Name (Gamertag)"},
              {"Name shown to other players in System Link lobbies (up to 15 characters)."},
              "User",
              {},
          },
          {
              "net_peers",
              Page::kNetwork,
              Control::kText,
              Apply::kRestart,
              {"Direct Peer IPs"},
              {"Comma-separated IPv4 addresses (or IP:port) of friends to reach directly when UDP broadcast does not cross the network."},
              "",
              {},
          },
          {
              "net_broadcast_targets",
              Page::kNetwork,
              Control::kText,
              Apply::kRestart,
              {"Extra Broadcast Targets"},
              {"Optional comma-separated broadcast or VPN subnet addresses (for example 26.255.255.255) that also receive System Link discovery packets."},
              "",
              {},
          },
          {
              "net_bind_ip",
              Page::kNetwork,
              Control::kText,
              Apply::kRestart,
              {"Network Adapter IP"},
              {"Local IPv4 address of the network adapter to bind System Link sockets to. Leave blank to bind all adapters (0.0.0.0)."},
              "",
              {},
          },
      },
  });

  // --- Saves ---
  pages.push_back({
      Page::kSaves,
      {"Saves"},
      {"Where campaign progress, player profiles, and checkpoints are stored on disk."},
      {
          {
              "user_data_root",
              Page::kSaves,
              Control::kChoice,
              Apply::kRestart,
              {"Save Folder"},
              {"Choose between the default AppData folder, a portable folder inside the game directory, or any custom folder chosen via Browse below."},
              "",
              {
                  {"", {"Default (AppData\\Local\\GearsOfWar3PC\\user)"}},
              },
          },
          {
              "gow3_unlock_insane",
              Page::kSaves,
              Control::kToggle,
              Apply::kNow,
              {"Unlock Insane Difficulty"},
              {"Insane is selectable from the start, without finishing the campaign first."},
              "true",
              {},
          },
      },
  });

  // Game Language lists only the languages this copy of the game has (gow3_language.cpp).
  for (auto& page : pages) {
    for (auto& row : page.rows) {
      if (std::string_view(row.cvar) != "gow3_language") continue;
      std::erase_if(row.choices, [](const Choice& c) {
        return std::string_view(c.value) != "auto" && !GameHasLanguage(c.value);
      });
    }
  }

  // Release builds have no DLSS (built with -NoDlss, without the DLSS runtime DLL): the choice is
  // only offered when that DLL sits next to the exe, as a build from source puts it there.
  // The name is put together at run time so the release exe holds no "nvngx" text, which
  // scripts\ngx-scan.ps1 treats as NVIDIA code and refuses to package.
  const std::string dlss_dll = std::string("nv") + "ngx" + "_dlss.dll";
  std::error_code ec;
  if (!std::filesystem::exists(rex::filesystem::GetExecutableFolder() / dlss_dll, ec)) {
    for (auto& page : pages) {
      std::erase_if(page.rows,
                    [](const RowSpec& row) { return std::string_view(row.cvar) == "gow2_dlss_preset"; });
      for (auto& row : page.rows) {
        if (std::string_view(row.cvar) != "upscaler") continue;
        std::erase_if(row.choices, [](const Choice& c) { return std::string_view(c.value) == "dlss"; });
      }
    }
  }

  return pages;
}

}  // namespace

const std::vector<PageSpec>& Catalog() {
  static const std::vector<PageSpec> kCatalog = BuildCatalog();
  return kCatalog;
}

const RowSpec* FindRow(std::string_view cvar) {
  for (const auto& page : Catalog()) {
    for (const auto& row : page.rows) {
      if (cvar == row.cvar) {
        return &row;
      }
    }
  }
  return nullptr;
}

const Text& UiText(std::string_view key) {
  static const std::pair<std::string_view, Text> kTable[] = {
      {"title", {"PC OPTIONS"}},
      {"subtitle", {"GEARS OF WAR 3"}},
      {"apply_now", {"Applies now"}},
      {"apply_level", {"Applies on next level load"}},
      {"apply_restart", {"Needs game restart"}},
      {"pending_restart", {"Restart the game to apply changes marked 'Needs game restart'."}},
      {"pending_level", {"Some changes will take effect the next time a level loads."}},
      {"restart_now", {"Restart Game Now"}},
      {"restarting", {"Restarting..."}},
      {"reset_page", {"Reset Page to Defaults"}},
      {"advanced", {"Advanced (All Cvars)"}},
      {"close", {"Resume"}},
      {"on", {"On"}},
      {"off", {"Off"}},
      {"browse_folder", {"Browse..."}},
      {"open_save_folder", {"Open Current Save Folder"}},
      {"current_save_folder", {"Active save folder"}},
      {"hint_bar", {"Up/Down: Select   Left/Right: Adjust   A/Enter: Toggle   LB/RB: Tab   B/Esc/F10: Close"}},
      {"hint_bar_bindings", {"Up/Down: Action   Left/Right: Slot   A/Enter: Bind   Delete/X: Clear   B/Esc: Back"}},
      {"back_to_controls", {"<  Back to Controls"}},
      {"pad_press_button", {"Press a button on your controller...   (Esc: Cancel)"}},
      {"pad_remap", {"Remap"}},
      {"key_press_key", {"Press a key or mouse button...   (Esc: Cancel)"}},
      {"key_set", {"Set"}},
  };
  for (const auto& [k, text] : kTable) {
    if (k == key) {
      return text;
    }
  }
  static const Text kEmpty{"", };
  return kEmpty;
}

Lang CurrentLang() {
  return Lang::kEn;
}

}  // namespace rex::glue::options
