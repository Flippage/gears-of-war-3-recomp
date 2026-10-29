#include <filesystem>

#include "gow3_options_catalog.h"
#include "setup/settings_store.h"
#include "test_util.h"

namespace {
std::filesystem::path RepoFile(const char* rel) {
  return std::filesystem::path(GOW3_SOURCE_DIR) / rel;
}
}  // namespace

TEST(settings_keep_unknown_lines) {
  auto f = TempDir("set1") / "gow3.toml";
  WriteFile(f, "# my note\nreadback_memexport = false\nsome_future_key = 3\nfullscreen = true\n");
  auto s = gow3::setup::LoadSettings(f);
  auto m = gow3::setup::MakeSettingsModel(s);
  CHECK(m.Get("window_mode") != "windowed");
  m.Set("window_mode", "windowed");
  CHECK(gow3::setup::SaveSettings(f, m));
  auto text = FileText(f);
  CHECK(text.contains("# my note") && text.contains("some_future_key = 3") &&
        text.contains("fullscreen = false"));
}

TEST(settings_missing_key_uses_default) {
  auto m = gow3::setup::MakeSettingsModel(gow3::setup::LoadSettings(TempDir("set2") / "gow3.toml"));
  CHECK(m.Get("resolution_scale") == rex::glue::options::FindRow("resolution_scale")->default_value);
}

TEST(settings_literals) {
  CHECK(gow3::setup::TomlLiteralFor("gow2_upscaler", "fsr3") == "\"fsr3\"");
  CHECK(gow3::setup::TomlLiteralFor("resolution_scale", "2") == "2");
  CHECK(gow3::setup::TomlLiteralFor("fullscreen", "true") == "true");
  CHECK(gow3::setup::TomlLiteralFor("window_width", "1920") == "1920");
  CHECK(gow3::setup::TomlLiteralFor("gow2_temporal", "false") == "false");
}

TEST(settings_unreadable_never_written) {
  auto f = TempDir("set3") / "gow3.toml";
  WriteFile(f, "fullscreen = tru e\n[[[\n");
  auto s = gow3::setup::LoadSettings(f);
  CHECK(!s.readable && !s.error.empty());
  auto m = gow3::setup::MakeSettingsModel(s);
  m.Set("window_mode", "windowed");
  CHECK(!gow3::setup::SaveSettings(f, m) && FileText(f) == "fullscreen = tru e\n[[[\n");
}

TEST(settings_v022_file_round_trip) {  // the shipped config/gow3.toml, untouched rows stay byte-identical
  auto f = TempDir("set4") / "gow3.toml";
  std::filesystem::copy_file(RepoFile("config/gow3.toml"), f);
  auto before = FileText(f);
  const auto mtime = std::filesystem::last_write_time(f);
  auto m = gow3::setup::MakeSettingsModel(gow3::setup::LoadSettings(f));
  CHECK(gow3::setup::SaveSettings(f, m) && FileText(f) == before);
  CHECK(std::filesystem::last_write_time(f) == mtime);
}

TEST(settings_composites_expand_to_raw_keys) {
  auto f = TempDir("set5") / "gow3.toml";
  WriteFile(f, "gow2_temporal = true\ngow2_upscaler = \"fsr3\"\n");
  auto m = gow3::setup::MakeSettingsModel(gow3::setup::LoadSettings(f));
  CHECK(m.Get("upscaler") == "fsr3");
  m.Set("upscaler", "off");
  m.Set("window_size", "1920x1080");
  CHECK(gow3::setup::SaveSettings(f, m));
  auto text = FileText(f);
  CHECK(text.contains("gow2_temporal = false") && text.contains("gow2_upscaler = \"off\""));
  CHECK(text.contains("window_width = 1920") && text.contains("window_height = 1080"));
}

TEST(settings_missing_file_shows_cvar_defaults) {  // what F10 shows on a stock install
  auto m = gow3::setup::MakeSettingsModel(gow3::setup::LoadSettings(TempDir("set6") / "gow3.toml"));
  CHECK(m.Get("window_size") == "0x0");
  CHECK(m.Get("window_mode") == "borderless");
  CHECK(m.Get("upscaler") == "off");
}

TEST(settings_literals_by_control) {
  using gow3::setup::TomlLiteralFor;
  CHECK(TomlLiteralFor("monitor", "1") == "1");                       // kChoice, integer choices
  CHECK(TomlLiteralFor("gow3_widescreen", "21:9") == "\"21:9\"");     // kChoice, string choices
  CHECK(TomlLiteralFor("gow3_mouse_scale", "1") == "1.0");            // kSliderFloat
  CHECK(TomlLiteralFor("user_name", "a\\b\"c") == "\"a\\\\b\\\"c\"");  // kText escapes
  CHECK(TomlLiteralFor("user_name", "a\nb\tc") == "\"a\\nb\\tc\"");
}

TEST(settings_copy_default) {
  auto d = TempDir("set_default");
  WriteFile(d / "config" / "gow3.default.toml", "# release default\nreadback_memexport = false\n");
  const std::filesystem::path dflt = d / "config" / "gow3.default.toml";
  std::filesystem::permissions(dflt, std::filesystem::perms::owner_write, std::filesystem::perm_options::remove);
  CHECK(gow3::setup::CopyDefaultSettings(dflt, d / "gow3.toml"));
  CHECK(FileText(d / "gow3.toml") == "# release default\nreadback_memexport = false\n");
  // The copy is writable, so the touched keys go in after the default's lines.
  auto m = gow3::setup::MakeSettingsModel(gow3::setup::LoadSettings(d / "gow3.toml"));
  m.Set("window_mode", "windowed");
  CHECK(gow3::setup::SaveSettings(d / "gow3.toml", m));
  CHECK(FileText(d / "gow3.toml").contains("# release default") && FileText(d / "gow3.toml").contains("fullscreen = false"));
  // An existing file is never replaced; no default, nothing copied.
  CHECK(!gow3::setup::CopyDefaultSettings(dflt, d / "gow3.toml"));
  CHECK(FileText(d / "gow3.toml").contains("fullscreen = false"));
  CHECK(!gow3::setup::CopyDefaultSettings(d / "none.toml", d / "other.toml"));
  CHECK(!std::filesystem::exists(d / "other.toml"));
  std::filesystem::permissions(dflt, std::filesystem::perms::owner_write, std::filesystem::perm_options::add);
}