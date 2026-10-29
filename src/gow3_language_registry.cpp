// gow3 - ReXGlue Recompiled Project
//
// The game languages found in this copy of the game (see gow3_language.h). Kept apart from
// gow3_language.cpp so the options catalog builds without the rex runtime.

#include "gow3_language.h"

#include <cctype>
#include <filesystem>
#include <string>
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
  // increment(ec) and the native (wide) name: nothing here throws, whatever the folder holds.
  for (std::filesystem::directory_iterator it(game_root / "GearGame" / "CookedXbox360", ec), end;
       !ec && it != end; it.increment(ec)) {
    const std::wstring name = it->path().filename().native();
    // Coalesced_<code>.bin: the language's text bundle.
    if (name.size() == 17 && name.starts_with(L"Coalesced_") && name.ends_with(L".bin")) {
      std::string code;
      for (const wchar_t c : name.substr(10, 3)) {
        if (c > 0x7F) {
          code.clear();
          break;
        }
        code += char(std::toupper(static_cast<unsigned char>(c)));
      }
      if (code.size() == 3) codes.push_back(std::move(code));
    }
  }
  return codes;
}

}  // namespace rex::glue
