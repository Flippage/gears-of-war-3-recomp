#include "setup/settings_store.h"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <vector>

#include <toml++/toml.hpp>

#include "gow3_options_catalog.h"
#include "gow3_options_values_text.h"
#include "gow3_toml_edit.h"
#include "setup/path_util.h"

namespace gow3::setup {
namespace opt = rex::glue::options;
namespace {

// Text of a scalar node: strings unquoted, numbers and booleans as TOML writes them.
bool ScalarText(const toml::node& n, std::string& out) {
  if (auto* s = n.as_string()) {
    out = s->get();
  } else if (auto* b = n.as_boolean()) {
    out = b->get() ? "true" : "false";
  } else if (auto* i = n.as_integer()) {
    out = std::to_string(i->get());
  } else if (auto* d = n.as_floating_point()) {
    char buf[64];
    const auto r = std::to_chars(buf, buf + sizeof(buf), d->get());
    out.assign(buf, r.ptr);
  } else {
    return false;
  }
  return true;
}

void Collect(const toml::table& t, SettingsFile& f) {
  for (auto&& [key, node] : t) {
    std::string text;
    if (ScalarText(node, text)) f.values[std::string(key.str())] = std::move(text);
  }
}

bool IsInteger(std::string_view s) {
  if (s.empty()) return false;
  long long v = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  return r.ec == std::errc() && r.ptr == s.data() + s.size();
}

std::string Quote(std::string_view v) {
  std::string out = "\"";
  for (char c : v) {
    if (c == '\n') {
      out += "\\n";
    } else if (c == '\t') {
      out += "\\t";
    } else if (c == '\r') {
      out += "\\r";
    } else {
      if (c == '\\' || c == '"') out += '\\';
      out += c;
    }
  }
  return out + "\"";
}

std::string BoolLiteral(std::string_view v) {
  return (v == "true" || v == "1") ? "true" : "false";
}

std::string IntLiteral(std::string_view v) {
  const std::string s(v);
  return std::to_string(std::llround(std::strtod(s.c_str(), nullptr)));
}

std::string FloatLiteral(std::string_view v) {
  const std::string s(v);
  char buf[64];
  const auto r = std::to_chars(buf, buf + sizeof(buf), std::strtod(s.c_str(), nullptr));
  std::string out(buf, r.ptr);
  if (out.find_first_of(".en") == std::string::npos) out += ".0";
  return out;
}

}  // namespace

SettingsFile LoadSettings(const std::filesystem::path& path) {
  SettingsFile f;
  std::error_code ec;
  const bool there = std::filesystem::exists(path, ec);
  if (ec) {
    f.readable = false;
    f.error = "cannot check " + Utf8(path) + ": " + ec.message();
    return f;
  }
  if (!there) return f;
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    f.readable = false;
    f.error = "cannot open " + Utf8(path);
    return f;
  }
  std::ostringstream ss;
  ss << in.rdbuf();
#if TOML_EXCEPTIONS
  try {
    Collect(toml::parse(ss.str()), f);
  } catch (const toml::parse_error& e) {
    f.readable = false;
    f.error = std::string(e.description());
  }
#else
  auto r = toml::parse(ss.str());
  if (r) {
    Collect(r.table(), f);
  } else {
    f.readable = false;
    f.error = std::string(r.error().description());
  }
#endif
  return f;
}

opt::Model MakeSettingsModel(const SettingsFile& f) {
  // A raw key missing from the file reads as its cvar default, as the game sees it
  // (fullscreen, fullscreen_exclusive, window_width, window_height: SDK ui/window.cpp;
  // gow2_temporal, gow2_upscaler: SDK graphics/d3d12/gow2_temporal.cpp, gow2_upscale.cpp).
  auto raw = [&f](const char* key, const char* cvar_default) -> std::string {
    auto it = f.values.find(key);
    return it != f.values.end() ? it->second : std::string(cvar_default);
  };
  auto reader = [&f, raw](std::string_view cvar) -> std::string {
    const opt::RowSpec* row = opt::FindRow(cvar);
    const std::string def = row && row->default_value ? row->default_value : "";
    if (cvar == "window_mode") {
      return opt::JoinWindowMode(raw("fullscreen", "true"), raw("fullscreen_exclusive", "false"));
    }
    if (cvar == "window_size") {
      return opt::JoinWindowSize(raw("window_width", "0"), raw("window_height", "0"));
    }
    if (cvar == "upscaler") {
      return opt::JoinUpscaler(raw("gow2_temporal", "false"), raw("gow2_upscaler", "off"));
    }
    auto it = f.values.find(std::string(cvar));
    return it != f.values.end() ? it->second : def;
  };
  return opt::Model(reader);
}

std::string TomlLiteralFor(std::string_view key, std::string_view value) {
  if (key == "fullscreen" || key == "fullscreen_exclusive" || key == "gow2_temporal") {
    return BoolLiteral(value);
  }
  if (key == "window_width" || key == "window_height") return IntLiteral(value);
  if (key == "gow2_upscaler") return Quote(value);
  const opt::RowSpec* row = opt::FindRow(key);
  if (!row) return rex::glue::FormatTomlLiteral(value);
  switch (row->control) {
    case opt::Control::kToggle: return BoolLiteral(value);
    case opt::Control::kSliderInt: return IntLiteral(value);
    case opt::Control::kSliderFloat: return FloatLiteral(value);
    case opt::Control::kText: return Quote(value);
    case opt::Control::kChoice: {
      bool all_int = true;
      for (const auto& c : row->choices) all_int = all_int && IsInteger(c.value);
      // No static choices (monitors, GPUs, save folders are found at run time): go by the value.
      if (row->choices.empty()) all_int = IsInteger(value);
      return all_int ? IntLiteral(value) : Quote(value);
    }
    default: return rex::glue::FormatTomlLiteral(value);
  }
}

bool SaveSettings(const std::filesystem::path& path, const opt::Model& m) {
  if (path.empty()) return false;
  if (!LoadSettings(path).readable) return false;
  std::vector<rex::glue::TomlEntry> entries;
  auto add = [&](const std::string& key, const std::string& value) {
    for (const auto& e : entries) {
      if (e.first == key) return;
    }
    entries.emplace_back(key, TomlLiteralFor(key, value));
  };
  for (const auto& row_key : m.TouchedRows()) {
    const std::string v = m.Get(row_key);
    if (row_key == "window_mode") {
      auto [fs, ex] = opt::SplitWindowMode(v);
      add("fullscreen", fs);
      add("fullscreen_exclusive", ex);
    } else if (row_key == "window_size") {
      auto [w, h] = opt::SplitWindowSize(v);
      add("window_width", w);
      add("window_height", h);
    } else if (row_key == "upscaler") {
      auto [t, u] = opt::SplitUpscaler(v);
      add("gow2_temporal", t);
      add("gow2_upscaler", u);
    } else {
      add(row_key, v);
    }
  }
  if (entries.empty()) return true;
  return rex::glue::UpdateTomlFile(path, entries);
}

}  // namespace gow3::setup
