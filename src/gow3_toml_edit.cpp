// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026 Santiago Quiroz Upegui and the gow2-recomp contributors
// Adapted for the Gears of War 3 recomp.

#include "gow3_toml_edit.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <system_error>

namespace rex::glue {
namespace {

std::string_view TrimLeft(std::string_view s) {
  size_t i = 0;
  while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) {
    ++i;
  }
  return s.substr(i);
}

std::string_view TrimRight(std::string_view s) {
  size_t n = s.size();
  while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
    --n;
  }
  return s.substr(0, n);
}

// If `line` is `<indent>key<ws>=<rhs>` (active) or `<indent>#<ws>key<ws>=<rhs>`
// (commented template), returns true and sets `is_commented` and `trailing_comment`.
bool MatchKeyLine(std::string_view line, std::string_view key, bool& is_commented,
                  std::string& indent_out, std::string& trailing_comment_out) {
  std::string_view raw = TrimRight(line);
  std::string_view trimmed = TrimLeft(raw);
  indent_out.assign(raw.substr(0, raw.size() - trimmed.size()));

  is_commented = false;
  if (!trimmed.empty() && trimmed.front() == '#') {
    is_commented = true;
    trimmed.remove_prefix(1);
    trimmed = TrimLeft(trimmed);
  }

  if (trimmed.size() <= key.size() || trimmed.substr(0, key.size()) != key) {
    return false;
  }
  std::string_view after_key = trimmed.substr(key.size());
  if (after_key.empty() ||
      (after_key.front() != ' ' && after_key.front() != '\t' && after_key.front() != '=')) {
    return false;
  }
  after_key = TrimLeft(after_key);
  if (after_key.empty() || after_key.front() != '=') {
    return false;
  }
  std::string_view rhs = after_key.substr(1);

  // Preserve an inline `# comment` on active lines (outside quotes).
  trailing_comment_out.clear();
  if (!is_commented) {
    bool in_quotes = false;
    bool escaped = false;
    for (size_t i = 0; i < rhs.size(); ++i) {
      char c = rhs[i];
      if (escaped) {
        escaped = false;
        continue;
      }
      if (c == '\\' && in_quotes) {
        escaped = true;
        continue;
      }
      if (c == '"') {
        in_quotes = !in_quotes;
        continue;
      }
      if (c == '#' && !in_quotes) {
        trailing_comment_out = "  ";
        trailing_comment_out.append(rhs.substr(i));
        break;
      }
    }
  }
  return true;
}

}  // namespace

std::string FormatTomlLiteral(std::string_view s) {
  if (s == "true" || s == "false") {
    return std::string(s);
  }
  if (!s.empty() && s.front() == '"' && s.back() == '"') {
    return std::string(s);
  }
  // Numeric check (integer or decimal).
  bool numeric = !s.empty();
  bool seen_digit = false;
  bool seen_dot = false;
  for (size_t i = 0; i < s.size(); ++i) {
    char c = s[i];
    if (i == 0 && (c == '-' || c == '+')) {
      continue;
    }
    if (std::isdigit(static_cast<unsigned char>(c))) {
      seen_digit = true;
      continue;
    }
    if (c == '.' && !seen_dot) {
      seen_dot = true;
      continue;
    }
    numeric = false;
    break;
  }
  if (numeric && seen_digit) {
    return std::string(s);
  }
  // Quote as a TOML basic string with escaped backslashes and quotes.
  std::string out = "\"";
  for (char c : s) {
    if (c == '\\' || c == '"') {
      out.push_back('\\');
    }
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string UpdateTomlText(std::string_view existing_content,
                           const std::vector<TomlEntry>& entries) {
  std::vector<std::string> lines;
  {
    std::string cur;
    for (char c : existing_content) {
      if (c == '\n') {
        if (!cur.empty() && cur.back() == '\r') {
          cur.pop_back();
        }
        lines.push_back(std::move(cur));
        cur.clear();
      } else {
        cur.push_back(c);
      }
    }
    if (!cur.empty()) {
      if (cur.back() == '\r') {
        cur.pop_back();
      }
      lines.push_back(std::move(cur));
    }
  }

  for (const auto& [key, literal] : entries) {
    if (key.empty()) {
      continue;
    }
    int active_idx = -1;
    int commented_idx = -1;
    std::string active_indent;
    std::string active_comment;
    std::string commented_indent;

    for (size_t i = 0; i < lines.size(); ++i) {
      bool is_commented = false;
      std::string indent;
      std::string trailing_comment;
      if (!MatchKeyLine(lines[i], key, is_commented, indent, trailing_comment)) {
        continue;
      }
      if (!is_commented) {
        active_idx = static_cast<int>(i);
        active_indent = std::move(indent);
        active_comment = std::move(trailing_comment);
      } else if (commented_idx < 0) {
        commented_idx = static_cast<int>(i);
        commented_indent = std::move(indent);
      }
    }

    if (active_idx >= 0) {
      lines[active_idx] = active_indent + key + " = " + literal + active_comment;
    } else if (commented_idx >= 0) {
      lines[commented_idx] = commented_indent + key + " = " + literal;
    } else {
      lines.push_back(key + " = " + literal);
    }
  }

  std::ostringstream out;
  for (size_t i = 0; i < lines.size(); ++i) {
    out << lines[i] << "\n";
  }
  return out.str();
}

bool UpdateTomlFile(const std::filesystem::path& path, const std::vector<TomlEntry>& entries) {
  if (path.empty()) {
    return false;
  }
  std::string existing;
  if (std::ifstream in(path, std::ios::binary); in) {
    std::ostringstream ss;
    ss << in.rdbuf();
    existing = ss.str();
  }

  std::string updated = UpdateTomlText(existing, entries);

  std::error_code ec;
  if (path.has_parent_path()) {
    std::filesystem::create_directories(path.parent_path(), ec);
  }
  auto tmp_path = path;
  tmp_path += ".tmp";
  {
    std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
    if (!out) {
      return false;
    }
    out.write(updated.data(), static_cast<std::streamsize>(updated.size()));
    if (!out.good()) {
      return false;
    }
  }
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    std::filesystem::copy_file(tmp_path, path, std::filesystem::copy_options::overwrite_existing,
                               ec);
    std::error_code rm_ec;
    std::filesystem::remove(tmp_path, rm_ec);
    if (ec) {
      return false;
    }
  }
  return true;
}

}  // namespace rex::glue
