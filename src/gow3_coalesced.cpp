// gow3 - ReXGlue Recompiled Project
//
// Coalesced edits, see gow3_coalesced.h. Bundle layout (big-endian): int32 file count, then per
// file an FString name, int32 section count, per section an FString name, int32 entry count, per
// entry an FString key and an FString value. An FString is an int32 length (negative: that many
// UTF-16LE code units including the terminator; positive: that many Latin-1 bytes including the
// terminator) followed by the characters (tools/coalesced.py reads the same layout).
//
// The game reads a bundle at its Xbox360TOC.txt size and checks the SHA-1 of exactly those bytes
// against HashSec; the bundles are exactly their TOC size, so an edited copy must not be longer,
// and is padded with zeros to that size (the parser is count driven and never reads the padding).
// HashSec (base disc 0.0.0.11): 0x835A0000 - 0x835A07AF, "<lower-case name>\0" + 20 SHA-1 bytes,
// 51 entries, coalesced_int.bin first.

#include "gow3_coalesced.h"

#include <windows.h>
#include <bcrypt.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string_view>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/memory/utils.h>
#include <rex/runtime.h>

REXCVAR_DEFINE_BOOL(gow3_menu_mouse, false, "input",
                    "Experimental: [GFxUI.GamepadMouse] reads the mouse axes, in a cached copy of the "
                    "Coalesced bundles (the console menus show no pointer, so it does nothing yet); "
                    "restart")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

REXCVAR_DEFINE_BOOL(gow3_main_menu_exit, true, "ui",
                    "Add Exit Game to the main menu (replaces Xbox LIVE Marketplace); restart")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::glue {

namespace {

constexpr uint32_t kHashSecBegin = 0x835A0000;
constexpr uint32_t kHashSecEnd = 0x835A07AF;

struct FStr {
  bool wide = false;
  std::vector<uint8_t> raw;  // characters including the terminator, as stored
};

struct Entry {
  FStr key, value;
};

struct Section {
  FStr name;
  std::vector<Entry> entries;
};

struct IniFile {
  FStr name;
  std::vector<Section> sections;
};

class Reader {
 public:
  explicit Reader(const std::vector<uint8_t>& d) : d_(d) {}
  bool ok = true;
  int32_t I32() {
    if (p_ + 4 > d_.size()) {
      ok = false;
      return 0;
    }
    int32_t v = int32_t((uint32_t(d_[p_]) << 24) | (uint32_t(d_[p_ + 1]) << 16) |
                        (uint32_t(d_[p_ + 2]) << 8) | d_[p_ + 3]);
    p_ += 4;
    return v;
  }
  FStr Str() {
    FStr s;
    int32_t n = I32();
    if (!ok || n == 0) return s;
    s.wide = n < 0;
    size_t bytes = s.wide ? size_t(-int64_t(n)) * 2 : size_t(n);
    if (p_ + bytes > d_.size()) {
      ok = false;
      return s;
    }
    s.raw.assign(d_.begin() + p_, d_.begin() + p_ + bytes);
    p_ += bytes;
    return s;
  }

 private:
  const std::vector<uint8_t>& d_;
  size_t p_ = 0;
};

bool Parse(const std::vector<uint8_t>& bytes, std::vector<IniFile>* out) {
  Reader r(bytes);
  int32_t file_count = r.I32();
  for (int32_t f = 0; f < file_count && r.ok; ++f) {
    IniFile file;
    file.name = r.Str();
    int32_t section_count = r.I32();
    for (int32_t s = 0; s < section_count && r.ok; ++s) {
      Section section;
      section.name = r.Str();
      int32_t entry_count = r.I32();
      for (int32_t e = 0; e < entry_count && r.ok; ++e) {
        Entry entry;
        entry.key = r.Str();
        entry.value = r.Str();
        section.entries.push_back(std::move(entry));
      }
      file.sections.push_back(std::move(section));
    }
    out->push_back(std::move(file));
  }
  return r.ok && file_count >= 0;
}

void PutI32(std::vector<uint8_t>* o, int32_t v) {
  uint32_t u = uint32_t(v);
  o->push_back(uint8_t(u >> 24));
  o->push_back(uint8_t(u >> 16));
  o->push_back(uint8_t(u >> 8));
  o->push_back(uint8_t(u));
}

void PutStr(std::vector<uint8_t>* o, const FStr& s) {
  if (s.raw.empty()) {
    PutI32(o, 0);
    return;
  }
  PutI32(o, s.wide ? -int32_t(s.raw.size() / 2) : int32_t(s.raw.size()));
  o->insert(o->end(), s.raw.begin(), s.raw.end());
}

std::vector<uint8_t> Serialize(const std::vector<IniFile>& files) {
  std::vector<uint8_t> o;
  PutI32(&o, int32_t(files.size()));
  for (const IniFile& f : files) {
    PutStr(&o, f.name);
    PutI32(&o, int32_t(f.sections.size()));
    for (const Section& s : f.sections) {
      PutStr(&o, s.name);
      PutI32(&o, int32_t(s.entries.size()));
      for (const Entry& e : s.entries) {
        PutStr(&o, e.key);
        PutStr(&o, e.value);
      }
    }
  }
  return o;
}

// ASCII view of an FString (non-ASCII characters become '?'), for matching.
std::string Text(const FStr& s) {
  std::string t;
  if (s.wide) {
    for (size_t i = 0; i + 1 < s.raw.size(); i += 2) {
      uint16_t c = uint16_t(s.raw[i] | (s.raw[i + 1] << 8));
      if (!c) break;
      t.push_back(c < 0x80 ? char(c) : '?');
    }
  } else {
    for (uint8_t c : s.raw) {
      if (!c) break;
      t.push_back(char(c));
    }
  }
  return t;
}

FStr MakeLike(const FStr& like, std::string_view text) {
  FStr s;
  s.wide = like.wide;
  for (char c : text) {
    s.raw.push_back(uint8_t(c));
    if (s.wide) s.raw.push_back(0);
  }
  s.raw.push_back(0);
  if (s.wide) s.raw.push_back(0);
  return s;
}

FStr MakeLikeWide(const FStr& like, std::wstring_view text) {
  FStr s;
  s.wide = like.wide;
  if (s.wide) {
    for (wchar_t c : text) {
      uint16_t u = static_cast<uint16_t>(c);
      s.raw.push_back(static_cast<uint8_t>(u & 0xFF));
      s.raw.push_back(static_cast<uint8_t>((u >> 8) & 0xFF));
    }
    s.raw.push_back(0);
    s.raw.push_back(0);
  } else {
    for (wchar_t c : text) {
      s.raw.push_back(static_cast<uint8_t>(c < 0x80 ? c : '?'));
    }
    s.raw.push_back(0);
  }
  return s;
}

struct ExitStrings {
  const wchar_t* label;
  const wchar_t* tooltip;
};

ExitStrings GetExitStringsForLang(std::string_view lang) {
  if (lang == "FRA") return {L"Quitter le jeu", L"Quitter Gears of War 3 et revenir au bureau."};
  if (lang == "PTB") return {L"Sair do Jogo", L"Sair de Gears of War 3 para a \u00e1rea de trabalho."};
  if (lang == "POL") return {L"Wyjd\u017a z gry", L"Wyjd\u017a z Gears of War 3 do pulpitu."};
  if (lang == "RUS") return {L"\u0412\u044b\u0445\u043e\u0434 \u0438\u0437 \u0438\u0433\u0440\u044b", L"\u0412\u044b\u0439\u0442\u0438 \u0438\u0437 Gears of War 3 \u043d\u0430 \u0440\u0430\u0431\u043e\u0447\u0438\u0439 \u0441\u0442\u043e\u043b."};
  if (lang == "CZE") return {L"Ukon\u010dit hru", L"Ukon\u010dit Gears of War 3 a p\u0159ej\u00edt na plochu."};
  if (lang == "HUN") return {L"Kil\u00e9p\u00e9s a j\u00e1t\u00e9kb\u00f3l", L"Kil\u00e9p\u00e9s a Gears of War 3 j\u00e1t\u00e9kb\u00f3l az asztalra."};
  if (lang == "KOR") return {L"\uac8c\uc784 \uc885\ub8cc", L"Gears of War 3\ub97c \uc885\ub8cc\ud558\uace0 \ubc14\ud0d5 \ud654\uba74\uc73c\ub85c \ub3cc\uc544\uac11\ub2c8\ub2e4."};
  if (lang == "CHN") return {L"\u96e2\u958b\u904a\u6232", L"\u96e2\u958b Gears of War 3 \u4e26\u8fd4\u56de\u684c\u9762\u3002"};
  return {L"Exit Game", L"Exit Gears of War 3 to desktop."};
}

int ApplyMainMenuExit(std::string_view lang, std::vector<IniFile>* files) {
  const auto strings = GetExitStringsForLang(lang);
  int changed = 0;
  for (IniFile& file : *files) {
    for (Section& section : file.sections) {
      const std::string name = Text(section.name);
      if (name == "MainMenu") {
        for (Entry& e : section.entries) {
          const std::string key = Text(e.key);
          if (key == "DLC") {
            e.value = MakeLikeWide(e.value, strings.label);
            ++changed;
          } else if (key == "TOOLTIP_DLC") {
            e.value = MakeLikeWide(e.value, strings.tooltip);
            ++changed;
          }
        }
      }
    }
  }
  return changed;
}

// Applies the edits; returns the number of values changed (0: nothing to do in this bundle).
int ApplyMenuMouse(std::vector<IniFile>* files) {
  int changed = 0;
  for (IniFile& file : *files) {
    for (Section& section : file.sections) {
      const std::string name = Text(section.name);
      if (name == "GFxUI.GamepadMouse") {
        for (Entry& e : section.entries) {
          const std::string key = Text(e.key);
          const char* want = key == "X" ? "MouseX" : key == "Y" ? "MouseY" : nullptr;
          if (want && Text(e.value) != want) {
            e.value = MakeLike(e.value, want);
            ++changed;
          }
        }
      } else if (name == "Engine.PlayerInput") {
        // Room for the longer values: the mobile / debug bindings do nothing on this build.
        const size_t before = section.entries.size();
        section.entries.erase(
            std::remove_if(section.entries.begin(), section.entries.end(),
                           [](const Entry& e) {
                             return Text(e.value).find("MOBILE_") != std::string::npos;
                           }),
            section.entries.end());
        changed += int(before - section.entries.size());
      }
    }
  }
  return changed;
}

bool Sha1(const std::vector<uint8_t>& bytes, std::array<uint8_t, 20>* out) {
  BCRYPT_ALG_HANDLE alg = nullptr;
  if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA1_ALGORITHM, nullptr, 0) != 0) return false;
  const bool ok = BCryptHash(alg, nullptr, 0, const_cast<PUCHAR>(bytes.data()), ULONG(bytes.size()),
                             out->data(), ULONG(out->size())) == 0;
  BCryptCloseAlgorithmProvider(alg, 0);
  return ok;
}

bool ReadAll(const std::filesystem::path& path, std::vector<uint8_t>* out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  out->assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

}  // namespace

std::vector<CoalescedHash> PrepareCoalescedOverrides(const std::filesystem::path& game_root,
                                                     const std::filesystem::path& cache_root) {
  std::vector<CoalescedHash> hashes;
  const bool menu_mouse = REXCVAR_GET(gow3_menu_mouse);
  const bool menu_exit = REXCVAR_GET(gow3_main_menu_exit);
  if (!menu_mouse && !menu_exit) return hashes;

  const std::filesystem::path cooked = game_root / "GearGame" / "CookedXbox360";
  const std::filesystem::path out_dir = cache_root / "coalesced";
  std::error_code ec;
  if (!std::filesystem::is_directory(cooked, ec)) return hashes;
  std::filesystem::create_directories(out_dir, ec);

  std::string overrides;
  int done = 0, skipped = 0;
  for (const auto& item : std::filesystem::directory_iterator(cooked, ec)) {
    const std::string name = item.path().filename().string();
    if (name.size() < 15 || name.rfind("Coalesced_", 0) != 0 || item.path().extension() != ".bin")
      continue;
    std::string lang = "INT";
    if (name.size() >= 14) {
      lang = name.substr(10, name.size() - 14);
      std::transform(lang.begin(), lang.end(), lang.begin(),
                     [](unsigned char c) { return char(std::toupper(c)); });
    }
    std::vector<uint8_t> bytes;
    std::vector<IniFile> files;
    if (!ReadAll(item.path(), &bytes) || !Parse(bytes, &files) || Serialize(files) != bytes) {
      ++skipped;  // unknown layout or trailing data
      continue;
    }
    int changed = 0;
    if (menu_exit) {
      changed += ApplyMainMenuExit(lang, &files);
    }
    if (menu_mouse) {
      changed += ApplyMenuMouse(&files);
    }
    if (changed == 0) {
      ++skipped;
      continue;
    }
    std::vector<uint8_t> edited = Serialize(files);
    if (edited.size() > bytes.size()) {
      REXLOG_WARN("COALESCED: {} edited size ({} bytes) exceeds original ({} bytes); skipping",
                  name, edited.size(), bytes.size());
      ++skipped;  // must fit the TOC size (= the original size)
      continue;
    }
    edited.resize(bytes.size(), 0);
    const std::filesystem::path dst = out_dir / name;
    {
      std::ofstream out(dst, std::ios::binary | std::ios::trunc);
      out.write(reinterpret_cast<const char*>(edited.data()), std::streamsize(edited.size()));
      if (!out) {
        ++skipped;
        continue;
      }
    }
    CoalescedHash hash;
    hash.name = name;
    std::transform(hash.name.begin(), hash.name.end(), hash.name.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (!Sha1(edited, &hash.sha1)) {
      ++skipped;
      continue;
    }
    hashes.push_back(std::move(hash));
    if (!overrides.empty()) overrides += ";";
    overrides += "GearGame/CookedXbox360/" + name + "=" + dst.string();
    ++done;
  }
  if (!overrides.empty()) {
    // Keep entries another component may have set.
    if (const char* existing = std::getenv("GOW2_VFS_OVERRIDES"); existing && *existing)
      overrides = std::string(existing) + ";" + overrides;
    _putenv_s("GOW2_VFS_OVERRIDES", overrides.c_str());
  }
  REXLOG_INFO("COALESCED: main menu exit / mouse edits in {} bundle(s), {} left unchanged; the game folder is "
              "not modified",
              done, skipped);
  return hashes;
}

void PatchCoalescedHashes(const std::vector<CoalescedHash>& hashes) {
  auto* runtime = rex::Runtime::instance();
  if (hashes.empty() || !runtime || !runtime->memory()) return;
  uint8_t* table = runtime->memory()->virtual_membase() + kHashSecBegin;
  const uint32_t size = kHashSecEnd - kHashSecBegin;
  // As in the Gears of War 2 port: change host page protection only when the table is not
  // writable already, and restore it afterwards.
  const uintptr_t page = rex::memory::page_size();
  uint8_t* page_base = reinterpret_cast<uint8_t*>(reinterpret_cast<uintptr_t>(table) & ~(page - 1));
  const size_t page_length = ((table + size - page_base) + page - 1) & ~(page - 1);
  size_t length = page_length;
  rex::memory::PageAccess access = rex::memory::PageAccess::kNoAccess;
  rex::memory::QueryProtect(page_base, length, access);
  const bool writable = access == rex::memory::PageAccess::kReadWrite ||
                        access == rex::memory::PageAccess::kExecuteReadWrite;
  rex::memory::PageAccess old_access = access;
  if (!writable && !rex::memory::Protect(page_base, page_length,
                                         rex::memory::PageAccess::kReadWrite, &old_access)) {
    REXLOG_WARN("COALESCED: HashSec not writable; the edited bundles will fail the check");
    return;
  }
  int patched = 0;
  for (const CoalescedHash& hash : hashes) {
    const size_t need = hash.name.size() + 1 + hash.sha1.size();
    for (size_t i = 0; i + need <= size; ++i) {
      if (std::memcmp(table + i, hash.name.c_str(), hash.name.size() + 1) == 0) {
        std::memcpy(table + i + hash.name.size() + 1, hash.sha1.data(), hash.sha1.size());
        ++patched;
        break;
      }
    }
  }
  if (!writable) rex::memory::Protect(page_base, page_length, old_access, nullptr);
  REXLOG_INFO("COALESCED: {} of {} HashSec entries replaced with the edited bundles'", patched,
              hashes.size());
}

}  // namespace rex::glue
