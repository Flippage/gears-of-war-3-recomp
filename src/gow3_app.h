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
#include <rex/system/xmemory.h>
#include <rex/ui/flags.h>

#include "gow3_content_fix.h"
#include "gow3_options_actions.h"
#include "gow3_options_overlay.h"
#include "gow3_utf8_guard.h"
#include "gow3_xam_input_guard.h"
#include "gow3_xam_trace.h"

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
    config.gpu_plugin = "xenos";
    // Guest content names are raw bytes; the runtime's UTF-8 decoder throws on a bad lead byte.
    rex::glue::InstallUtf8Guard();
    // The guest polls XamInput* from several threads; the runtime's device list has no lock.
    rex::glue::InstallXamInputGuard();
    rex::glue::InstallDiscErrorSilencer();
    // Save devices: keep the async XamContentCreateEx/Close root names alive (before the trace,
    // so the trace wraps the fixed call).
    rex::glue::InstallContentFix();
    rex::glue::InstallXamTrace();  // dev tool, GOW3_XAM_TRACE=1
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
    rex::glue::AllocateContentFixRing();
    // Dev tool: GOW3_DUMP_IMAGE=<dir> writes the loaded (decrypted, decompressed) image to
    // <dir>/image.bin for tools/make_image_blocks.py and the hint harvester.
    const char* dir = std::getenv("GOW3_DUMP_IMAGE");
    if (!dir || !*dir) return;
    constexpr uint32_t kBase = 0x82000000, kEnd = 0x835B0000;
    std::filesystem::create_directories(dir);
    std::ofstream out(std::filesystem::path(dir) / "image.bin", std::ios::binary);
    const auto* bytes = runtime()->memory()->TranslateVirtual<const char*>(kBase);
    out.write(bytes, kEnd - kBase);
  }
  // void OnPostSetup() override {}
  void OnConfigureFonts(ImFontAtlas* atlas) override {
    rex::glue::AddOptionsFonts(atlas);
  }
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    ApplyConfigUserRoot(options_paths_);
    options_overlay_ = rex::glue::CreateOptionsOverlay(drawer, options_paths_);
  }
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  void OnShutdown() override {
    rex::glue::ClosePcOptionsForShutdown();
    options_overlay_.reset();
  }

  // A game folder holds default.xex and GearGame\.
  static bool IsGameFolder(const std::filesystem::path& dir) {
    std::error_code ec;
    return !dir.empty() && std::filesystem::exists(dir / "default.xex", ec) &&
           std::filesystem::is_directory(dir / "GearGame", ec);
  }

  // The folder itself or one of its subfolders.
  static std::filesystem::path FindGameFolderIn(const std::filesystem::path& dir) {
    if (IsGameFolder(dir)) return dir;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(
             dir, std::filesystem::directory_options::skip_permission_denied, ec);
         !ec && it != std::filesystem::directory_iterator(); it.increment(ec)) {
      if (IsGameFolder(it->path())) return it->path();
    }
    return {};
  }

  // Lets a double-click boot without --game_data_root, in this order: the folder given on the
  // command line, the first line of gow3_game_path.txt next to the exe, then the exe's folder and
  // up to four parent folders (each with its direct subfolders). Same idea as the Gears of War 2
  // port's ResolveContentRoot.
  static std::filesystem::path ResolveGameFolder(const std::filesystem::path& given) {
    if (IsGameFolder(given)) return given;
    const std::filesystem::path home = rex::filesystem::GetExecutablePath().parent_path();
    if (std::ifstream in(home / "gow3_game_path.txt"); in) {
      std::string line;
      std::getline(in, line);
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (auto hit = FindGameFolderIn(std::filesystem::u8path(line)); !hit.empty()) return hit;
    }
    std::filesystem::path dir = home;
    for (int depth = 0; depth <= 4 && !dir.empty(); ++depth, dir = dir.parent_path()) {
      if (auto hit = FindGameFolderIn(dir); !hit.empty()) return hit;
      if (dir == dir.parent_path()) break;
    }
    return given;
  }
  // Saves, profile and shader cache in %LOCALAPPDATA%\GearsOfWar3PC (as the Gears of War 2 port
  // does with GearsOfWar2PC); without a user data root the save devices are never mounted.
  void OnConfigurePaths(rex::PathConfig& paths) override {
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
  std::unique_ptr<rex::ui::ImGuiDialog> options_overlay_;
};
