// gow3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <windows.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <string>

#include <rex/filesystem.h>
#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/logging.h>
#include <rex/system/kernel_state.h>
#include <rex/system/user_module.h>
#include <rex/system/xex_module.h>
#include <rex/system/xmemory.h>
#include <rex/ui/flags.h>

#include "setup/finder.h"
#include "setup/settings_store.h"
#include "setup/state.h"
#include "gow3_content_fix.h"
#include "gow3_crash_log.h"
#include "gow3_dlc.h"
#include "gow3_launch_check.h"
#include "gow3_title_update.h"
#include "gow3_options_actions.h"
#include "gow3_coalesced.h"
#include "gow3_mouse_aim.h"
#include "gow3_options_overlay.h"
#include "gow3_utf8_guard.h"
#include "gow3_xam_input_guard.h"
#include "gow3_xam_trace.h"
#include "gow3_button_glyphs.h"
#include "gow3_unlocks.h"
#include "gow3_renderer.h"
#include "gow3_main_menu_exit.h"
#include "gow3_widescreen.h"
#include "gow3_language.h"

class Gow3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Gow3App>(new Gow3App(ctx, "gow3",
        PPCImageConfig));
  }

  // Override virtual hooks for customization:
  // void OnPostInitLogging() override {}
  void OnPreSetup(rex::RuntimeConfig& config) override {
    rex::glue::options_actions::WaitForPreviousInstance();
    rex::glue::InstallCrashLog();
    config.gpu_plugin = "xenos";
    rex::glue::ApplyRendererChoice();
    // Game language: the console language / region the kernel reports, from gow3_language.
    rex::glue::PrepareLanguage(game_data_root());
    // Enable runtime texture replacement for button prompts.
    rex::glue::PrepareButtonGlyphs();
    // Guest content names are raw bytes; the runtime's UTF-8 decoder throws on a bad lead byte.
    rex::glue::InstallUtf8Guard();
    // The guest polls XamInput* from several threads; the runtime's device list has no lock.
    rex::glue::InstallXamInputGuard();
    rex::glue::InstallDiscErrorSilencer();
    // Save devices: keep the async XamContentCreateEx/Close root names alive (before the trace,
    // so the trace wraps the fixed call).
    rex::glue::InstallContentFix();
    rex::glue::InstallXamTrace();  // dev tool, GOW3_XAM_TRACE=1
    // Native mouse look replaces the mouse-to-right-stick emulation (gow3_mouse_aim).
    rex::glue::PrepareNativeMouseAim();
    // Ultrawide back buffer and video mode setup.
    rex::glue::PrepareWidescreen();
    // Edited copies of the Coalesced bundles (menu mouse, exit game), served in place of the originals.
    coalesced_hashes_ = rex::glue::PrepareCoalescedOverrides(game_data_root(), cache_root());
    SetUpscaleTarget();
  }

  // The patched runtime's temporal upscalers (DLSS/FSR, gow2_temporal + gow2_upscaler) scale the
  // finished frame to the screen and skip every frame while its size is unknown. Same as the
  // Gears of War 2 port's SetUpscaleTarget; GOW2_TARGET_W/H set by the player win.
  static void SetUpscaleTarget() {
    DEVMODEW dm{};
    dm.dmSize = sizeof(dm);
    const bool have = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmPelsHeight;
    if (!std::getenv("GOW2_TARGET_W"))
      _putenv_s("GOW2_TARGET_W", std::to_string(have ? dm.dmPelsWidth : 1920).c_str());
    if (!std::getenv("GOW2_TARGET_H"))
      _putenv_s("GOW2_TARGET_H", std::to_string(have ? dm.dmPelsHeight : 1080).c_str());
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  void OnPostLoadXexImage() override {
    // The edited Coalesced copies' hashes, before any game code checks them.
    rex::glue::PatchCoalescedHashes(coalesced_hashes_);
    rex::glue::AllocateContentFixRing();
    rex::glue::InstallPlayerDlc(game_data_root());
    // Dev tool: GOW3_DUMP_IMAGE=<dir> writes the loaded (decrypted, decompressed) image to
    // <dir>/image.bin for tools/make_image_blocks.py and the hint harvester.
    const char* dir = std::getenv("GOW3_DUMP_IMAGE");
    if (!dir || !*dir) return;
    // The loaded module's own range, so Title Update 6's image (a different size) dumps whole.
    auto module = rex::system::kernel_state()->GetExecutableModule();
    auto* xex = module ? module->xex_module() : nullptr;
    if (!xex) return;
    const uint32_t base = xex->base_address(), size = xex->image_size();
    std::filesystem::create_directories(dir);
    std::ofstream out(std::filesystem::path(dir) / "image.bin", std::ios::binary);
    const auto* bytes = runtime()->memory()->TranslateVirtual<const char*>(base);
    out.write(bytes, size);
    REXLOG_INFO("GOW3_DUMP_IMAGE: {:08X}..{:08X} -> {}", base, base + size, dir);
    if (std::getenv("GOW3_DUMP_IMAGE_EXIT")) {
      // Dump only (tools\map_tu6_addresses.py): stop before the game runs.
      rex::FlushLogging();
      std::exit(0);
    }
  }
  void OnPostSetup() override {
    rex::glue::InstallNativeMouseAim(window());
    rex::glue::InstallButtonGlyphs(window());
    rex::glue::InstallUnlockHooks();
  }
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    rex::glue::AddOptionsFonts(atlas);
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    ApplyConfigUserRoot(options_paths_);
    options_overlay_ = rex::glue::CreateOptionsOverlay(drawer, options_paths_, immediate_drawer());
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override {
    rex::glue::ShutdownButtonGlyphs();
    rex::glue::ClosePcOptionsForShutdown();
    options_overlay_.reset();
  }

  // A game folder holds default.xex and GearGame\.
  static bool IsGameFolder(const std::filesystem::path& dir) {
    return gow3::setup::IsGameFolder(dir);
  }

  // Lets a double-click boot without --game_data_root, in this order: the folder given on the
  // command line, the first line of gow3_game_path.txt next to the exe, then the exe's folder and
  // up to four parent folders (each with its direct subfolders). Same idea as the Gears of War 2
  // port's ResolveContentRoot. Shared with launcher.exe (gow3::setup::ResolveGameFolder), so both
  // act on the same folder.
  static std::filesystem::path ResolveGameFolder(const std::filesystem::path& given) {
    return gow3::setup::ResolveGameFolder(given, rex::filesystem::GetExecutablePath().parent_path());
  }
  // Saves, profile and shader cache in %LOCALAPPDATA%\GearsOfWar3PC (as the Gears of War 2 port
  // does with GearsOfWar2PC); without a user data root the save devices are never mounted.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    // A release ships config\gow3.default.toml, not gow3.toml, so extracting a new zip over an
    // install never replaces the player's settings: the first start makes gow3.toml from it (the
    // config is loaded right after this).
    gow3::setup::CopyDefaultSettings(paths.config_path.parent_path() / "config" / "gow3.default.toml",
                                     paths.config_path);
    paths.game_data_root = ResolveGameFolder(paths.game_data_root);
    paths.update_data_root = paths.game_data_root / "$SystemUpdate";
    // An explicit --user_data_root / --cache_root (test runs, several installs) wins.
    const bool own_user_root = IsSetByPlayer("user_data_root");
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
      const std::filesystem::path base = std::filesystem::path(local) / "GearsOfWar3PC";
      if (!own_user_root) paths.user_data_root = base / "user";
      if (!own_user_root && !IsSetByPlayer("cache_root")) paths.cache_root = base / "cache";
    }
    std::error_code ec;
    std::filesystem::create_directories(paths.user_data_root, ec);
    std::filesystem::create_directories(paths.cache_root, ec);
    options_paths_ = paths;
  }

  // gow3.toml is loaded after OnConfigurePaths; apply a user_data_root saved by the PC Options
  // menu (or written in gow3.toml) before the runtime mounts the save devices.
  std::optional<rex::PathConfig> OnFinalizePaths(
      const rex::PathConfig& defaults,
      std::function<void(rex::PathConfig)> /*resume*/) override {
    rex::PathConfig p = defaults;
    ApplyConfigUserRoot(p);
    // The TU6 executable: its title update as UPDATE: (or a clear stop without it).
    rex::glue::PrepareTitleUpdate(p.game_data_root, p.update_data_root);
    options_paths_ = p;
    return p;
  }

  static void ApplyConfigUserRoot(rex::PathConfig& paths) {
    if (IsSetByPlayer("user_data_root")) {
      const std::string custom = REXCVAR_GET(user_data_root);
      if (!custom.empty()) {
        paths.user_data_root = rex::to_path(custom);
        std::error_code ec;
        std::filesystem::create_directories(paths.user_data_root, ec);
      }
    }
  }

  static bool IsSetByPlayer(const char* name) {
    return rex::cvar::GetFlagSource(name) != rex::cvar::Source::kDefault;
  }

 private:
  rex::PathConfig options_paths_;
  std::vector<rex::glue::CoalescedHash> coalesced_hashes_;
  std::unique_ptr<rex::ui::ImGuiDialog> options_overlay_;
};
