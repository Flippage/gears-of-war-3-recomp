#include "setup/json_scan.h"

#include <cstdint>

namespace gow3::setup {
namespace {

constexpr int kMaxDepth = 64;

enum class Kind { kError, kString, kNumber, kBool, kNull, kObject, kArray };

class Parser {
 public:
  explicit Parser(std::string_view s) : s_(s) {}

  std::optional<ReleaseInfo> Release() {
    ReleaseInfo r;
    Ws();
    if (!Eat('{')) return std::nullopt;
    const bool ok = Members([&](const std::string& key) {
      if (key == "assets") return Assets(&r.asset_names);
      std::string str;
      bool b = false;
      const Kind k = Value(1, &str, &b);
      if (k == Kind::kError) return false;
      if (key == "tag_name" || key == "body") {
        if (k != Kind::kString) return false;
        (key == "tag_name" ? r.tag : r.body) = std::move(str);
      } else if (key == "prerelease") {
        if (k != Kind::kBool) return false;
        r.prerelease = b;
      }
      return true;
    });
    Ws();
    if (!ok || pos_ != s_.size()) return std::nullopt;
    return r;
  }

 private:
  void Ws() {
    while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
  }
  bool Eat(char c) {
    if (pos_ < s_.size() && s_[pos_] == c) {
      ++pos_;
      return true;
    }
    return false;
  }
  bool EatWord(std::string_view w) {
    if (s_.substr(pos_, w.size()) != w) return false;
    pos_ += w.size();
    return true;
  }

  // After '{': `"key": value` pairs up to '}'. `member` reads the value (it is at the value's first
  // character, white space skipped) and returns false on error.
  template <typename F>
  bool Members(F&& member) {
    Ws();
    if (Eat('}')) return true;
    for (;;) {
      Ws();
      std::string key;
      if (!String(&key)) return false;
      Ws();
      if (!Eat(':')) return false;
      Ws();
      if (!member(key)) return false;
      Ws();
      if (Eat('}')) return true;
      if (!Eat(',')) return false;
    }
  }

  // After '[': values up to ']'.
  template <typename F>
  bool Elements(F&& element) {
    Ws();
    if (Eat(']')) return true;
    for (;;) {
      Ws();
      if (!element()) return false;
      Ws();
      if (Eat(']')) return true;
      if (!Eat(',')) return false;
    }
  }

  // The top-level "assets": an array of objects; each one's own "name" (a string) is kept.
  bool Assets(std::vector<std::string>* names) {
    if (!Eat('[')) return false;
    names->clear();
    return Elements([&] {
      if (!Eat('{')) return false;
      std::optional<std::string> name;
      const bool ok = Members([&](const std::string& key) {
        std::string str;
        bool b = false;
        const Kind k = Value(3, &str, &b);
        if (k == Kind::kError) return false;
        if (key == "name") {
          if (k != Kind::kString) return false;
          name = std::move(str);
        }
        return true;
      });
      if (ok && name) names->push_back(std::move(*name));
      return ok;
    });
  }

  // Any value; a string goes to *str, a bool to *b. Containers are checked and skipped.
  Kind Value(int depth, std::string* str, bool* b) {
    if (depth > kMaxDepth || pos_ >= s_.size()) return Kind::kError;
    const char c = s_[pos_];
    if (c == '"') return String(str) ? Kind::kString : Kind::kError;
    if (c == '{') {
      ++pos_;
      return Members([&](const std::string&) {
               std::string ignored;
               bool ignored_b;
               return Value(depth + 1, &ignored, &ignored_b) != Kind::kError;
             })
                 ? Kind::kObject
                 : Kind::kError;
    }
    if (c == '[') {
      ++pos_;
      return Elements([&] {
               std::string ignored;
               bool ignored_b;
               return Value(depth + 1, &ignored, &ignored_b) != Kind::kError;
             })
                 ? Kind::kArray
                 : Kind::kError;
    }
    if (EatWord("true")) return *b = true, Kind::kBool;
    if (EatWord("false")) return *b = false, Kind::kBool;
    if (EatWord("null")) return Kind::kNull;
    return Number() ? Kind::kNumber : Kind::kError;
  }

  bool Digits() {
    const size_t start = pos_;
    while (pos_ < s_.size() && s_[pos_] >= '0' && s_[pos_] <= '9') ++pos_;
    return pos_ > start;
  }

  // -?(0|[1-9][0-9]*)(\.[0-9]+)?([eE][+-]?[0-9]+)?
  bool Number() {
    Eat('-');
    if (Eat('0')) {
      // no further integer digits after a leading zero
    } else if (pos_ < s_.size() && s_[pos_] >= '1' && s_[pos_] <= '9') {
      Digits();
    } else {
      return false;
    }
    if (Eat('.') && !Digits()) return false;
    if (Eat('e') || Eat('E')) {
      if (!Eat('+')) Eat('-');
      if (!Digits()) return false;
    }
    return true;
  }

  bool Hex4(uint32_t* v) {
    if (s_.size() - pos_ < 4) return false;
    *v = 0;
    for (int i = 0; i < 4; ++i) {
      const char c = s_[pos_++];
      uint32_t d;
      if (c >= '0' && c <= '9') d = c - '0';
      else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
      else return false;
      *v = *v << 4 | d;
    }
    return true;
  }

  static void AppendUtf8(std::string* out, uint32_t cp) {
    if (cp < 0x80) {
      *out += static_cast<char>(cp);
    } else if (cp < 0x800) {
      *out += static_cast<char>(0xC0 | cp >> 6);
      *out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
      *out += static_cast<char>(0xE0 | cp >> 12);
      *out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
      *out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
      *out += static_cast<char>(0xF0 | cp >> 18);
      *out += static_cast<char>(0x80 | (cp >> 12 & 0x3F));
      *out += static_cast<char>(0x80 | (cp >> 6 & 0x3F));
      *out += static_cast<char>(0x80 | (cp & 0x3F));
    }
  }

  // At '"': the decoded string (UTF-8). Raw control characters and bad escapes fail.
  bool String(std::string* out) {
    if (!Eat('"')) return false;
    out->clear();
    for (;;) {
      if (pos_ >= s_.size()) return false;
      const char c = s_[pos_++];
      if (c == '"') return true;
      if (static_cast<unsigned char>(c) < 0x20) return false;
      if (c != '\\') {
        *out += c;
        continue;
      }
      if (pos_ >= s_.size()) return false;
      switch (s_[pos_++]) {
        case '"': *out += '"'; break;
        case '\\': *out += '\\'; break;
        case '/': *out += '/'; break;
        case 'b': *out += '\b'; break;
        case 'f': *out += '\f'; break;
        case 'n': *out += '\n'; break;
        case 'r': *out += '\r'; break;
        case 't': *out += '\t'; break;
        case 'u': {
          uint32_t cp;
          if (!Hex4(&cp)) return false;
          if (cp >= 0xDC00 && cp <= 0xDFFF) return false;  // a low surrogate on its own
          if (cp >= 0xD800 && cp <= 0xDBFF) {
            uint32_t lo;
            if (!EatWord("\\u") || !Hex4(&lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
            cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
          }
          AppendUtf8(out, cp);
          break;
        }
        default: return false;
      }
    }
  }

  std::string_view s_;
  size_t pos_ = 0;
};

}  // namespace

std::optional<ReleaseInfo> ParseRelease(std::string_view json) { return Parser(json).Release(); }

}  // namespace gow3::setup
