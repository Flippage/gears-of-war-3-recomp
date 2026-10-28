// gow3 - ReXGlue Recompiled Project
//
// Game language, see gow3_language.h.

#include "gow3_language.h"

#include <cctype>
#include <set>
#include <string>

#include <windows.h>

#include <rex/cvar.h>
#include <rex/logging.h>

REXCVAR_DEFINE_STRING(gow3_language, "auto", "UI",
                      "Game language (text, and speech where the game has it): auto (Windows' display "
                      "language) or a language of this copy of the game: INT (English), FRA, DEU, ITA, "
                      "ESN, ESM, JPN, KOR, CHN, CHT, PTB, POL, RUS, CZE, HUN; restart")
    .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace rex::glue {
namespace {

// Unreal Engine 3's language codes and the Xbox 360 settings that select them: the console language
// (XLanguage) and, where the language alone is not enough, the region (the SDK's country index).
// Czech and Hungarian have no console language: English in that region.
struct GameLanguage {
  const char* code;
  uint32_t language;
  uint32_t country;  // 0 = leave the region alone
};
constexpr GameLanguage kLanguages[] = {
    {"INT", 1, 0},  {"FRA", 4, 0},   {"DEU", 3, 0}, {"ITA", 6, 0},  {"ESN", 5, 31},
    {"ESM", 5, 71}, {"JPN", 2, 0},   {"KOR", 7, 0}, {"CHT", 8, 0},  {"CHN", 8, 0},
    {"PTB", 9, 13}, {"POL", 11, 0},  {"RUS", 12, 0}, {"CZE", 1, 23}, {"HUN", 1, 42},
};

std::set<std::string> g_available;

const GameLanguage* Find(std::string_view code) {
  for (const GameLanguage& l : kLanguages) {
    if (code == l.code) return &l;
  }
  return nullptr;
}

// The game language for Windows' display language, among the ones the game has.
std::string FromSystem() {
  const LANGID id = GetUserDefaultUILanguage();
  const WORD primary = PRIMARYLANGID(id), sub = SUBLANGID(id);
  auto first = [](std::initializer_list<const char*> codes) -> std::string {
    for (const char* c : codes) {
      if (g_available.count(c)) return c;
    }
    return "INT";
  };
  switch (primary) {
    case LANG_FRENCH: return first({"FRA"});
    case LANG_GERMAN: return first({"DEU"});
    case LANG_ITALIAN: return first({"ITA"});
    case LANG_SPANISH:
      return sub == SUBLANG_SPANISH_MODERN || sub == SUBLANG_SPANISH ? first({"ESN", "ESM"})
                                                                       : first({"ESM", "ESN"});
    case LANG_JAPANESE: return first({"JPN"});
    case LANG_KOREAN: return first({"KOR"});
    case LANG_CHINESE:
      return sub == SUBLANG_CHINESE_SIMPLIFIED || sub == SUBLANG_CHINESE_SINGAPORE
                 ? first({"CHN", "CHT"})
                 : first({"CHT", "CHN"});
    case LANG_PORTUGUESE: return first({"PTB"});
    case LANG_POLISH: return first({"POL"});
    case LANG_RUSSIAN: return first({"RUS"});
    case LANG_CZECH: return first({"CZE"});
    case LANG_HUNGARIAN: return first({"HUN"});
    default: return "INT";
  }
}

}  // namespace

bool GameHasLanguage(std::string_view code) {
  return g_available.count(std::string(code)) != 0;
}

void PrepareLanguage(const std::filesystem::path& game_root) {
  std::error_code ec;
  for (const auto& item :
       std::filesystem::directory_iterator(game_root / "GearGame" / "CookedXbox360", ec)) {
    const std::string name = item.path().filename().string();
    // Coalesced_<code>.bin: the language's text bundle.
    if (name.size() == 17 && name.rfind("Coalesced_", 0) == 0 && name.substr(13) == ".bin") {
      std::string code = name.substr(10, 3);
      for (char& c : code) c = char(std::toupper(static_cast<unsigned char>(c)));
      if (Find(code)) g_available.insert(code);
    }
  }
  std::string list;
  for (const std::string& code : g_available) list += (list.empty() ? "" : " ") + code;
  REXLOG_INFO("LANGUAGE game languages: {}", list.empty() ? "none found" : list);

  const std::string setting = REXCVAR_GET(gow3_language);
  const bool automatic = setting.empty() || setting == "auto";
  // A user_language of the player's own (gow3.toml or the command line) wins over auto.
  if (automatic && rex::cvar::GetFlagSource("user_language") != rex::cvar::Source::kDefault) {
    REXLOG_INFO("LANGUAGE auto: user_language set by the player, left as it is");
    return;
  }
  std::string code = automatic ? FromSystem() : setting;
  for (char& c : code) c = char(std::toupper(static_cast<unsigned char>(c)));
  if (!g_available.count(code)) {
    REXLOG_INFO("LANGUAGE {} not in this copy of the game, English instead", code);
    code = "INT";
  }
  const GameLanguage* lang = Find(code);
  if (!lang) return;
  rex::cvar::SetFlagByName("user_language", std::to_string(lang->language));
  if (lang->country) rex::cvar::SetFlagByName("user_country", std::to_string(lang->country));
  REXLOG_INFO("LANGUAGE {} ({}): user_language {}, user_country {}", code,
              automatic ? "auto" : "chosen", lang->language,
              lang->country ? std::to_string(lang->country) : std::string("unchanged"));
}

}  // namespace rex::glue
