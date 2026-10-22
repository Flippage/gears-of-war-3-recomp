// gow3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <rex/rex_app.h>
#include <rex/runtime.h>
#include <rex/system/xmemory.h>

#include "gow3_utf8_guard.h"
#include "gow3_xam_input_guard.h"

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
    config.gpu_plugin = "xenos";
    // Guest content names are raw bytes; the runtime's UTF-8 decoder throws on a bad lead byte.
    rex::glue::InstallUtf8Guard();
    // The guest polls XamInput* from several threads; the runtime's device list has no lock.
    rex::glue::InstallXamInputGuard();
    rex::glue::InstallDiscErrorSilencer();
  }
  // void OnLoadXexImage(std::string& xex_image) override {}
  // Dev tool: GOW3_DUMP_IMAGE=<dir> writes the loaded (decrypted, decompressed) image to
  // <dir>/image.bin for tools/make_image_blocks.py and the hint harvester.
  void OnPostLoadXexImage() override {
    const char* dir = std::getenv("GOW3_DUMP_IMAGE");
    if (!dir || !*dir) return;
    constexpr uint32_t kBase = 0x82000000, kEnd = 0x835B0000;
    std::filesystem::create_directories(dir);
    std::ofstream out(std::filesystem::path(dir) / "image.bin", std::ios::binary);
    const auto* bytes = runtime()->memory()->TranslateVirtual<const char*>(kBase);
    out.write(bytes, kEnd - kBase);
  }
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // Saves, profile and shader cache in %LOCALAPPDATA%\GearsOfWar3PC (as the Gears of War 2 port
  // does with GearsOfWar2PC); without a user data root the save devices are never mounted.
  void OnConfigurePaths(rex::PathConfig& paths) override {
    paths.update_data_root = paths.game_data_root / "$SystemUpdate";
    if (const char* local = std::getenv("LOCALAPPDATA"); local && *local) {
      const std::filesystem::path base = std::filesystem::path(local) / "GearsOfWar3PC";
      paths.user_data_root = base / "user";
      paths.cache_root = base / "cache";
      std::error_code ec;
      std::filesystem::create_directories(paths.user_data_root, ec);
      std::filesystem::create_directories(paths.cache_root, ec);
    }
  }
};
