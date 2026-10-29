// gow3 - ReXGlue Recompiled Project
//
// The game languages found in this copy of the game (see gow3_language.h). Kept apart from
// gow3_language.cpp so the options catalog builds without the rex runtime.

#include "gow3_language.h"

#include <cctype>
#include <system_error>

namespace rex::glue {
namespace {
std::set<std::string> g_available;
}  // namespace

bool GameHasLanguage(std::string_view code) {
  return g_available.count(std::string(code)) != 0;
}

void NoteGameLanguage(const std::string& code) {
  g_available.insert(code);
}

const std::set<std::string>& GameLanguages() {
  return g_available;
}

std::vector<std::string> ScanGameLanguages(const std::filesystem::path& game_root) {
  std::vector<std::string> codes;
  std::error_code ec;
  for (const auto& item :
       std::filesystem::directory_iterator(game_root / "GearGame" / "CookedXbox360", ec)) {
    const std::string name = item.path().filename().string();
    // Coalesced_<code>.bin: the language's text bundle.
    if (name.size() == 17 && name.rfind("Coalesced_", 0) == 0 && name.substr(13) == ".bin") {
      std::string code = name.substr(10, 3);
      for (char& c : code) c = char(std::toupper(static_cast<unsigned char>(c)));
      codes.push_back(std::move(code));
    }
  }
  return codes;
}

}  // namespace rex::glue
