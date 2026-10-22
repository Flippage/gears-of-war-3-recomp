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
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
