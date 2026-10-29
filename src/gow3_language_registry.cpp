// gow3 - ReXGlue Recompiled Project
//
// The game languages found in this copy of the game (see gow3_language.h). Kept apart from
// gow3_language.cpp so the options catalog builds without the rex runtime.

#include "gow3_language.h"

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

}  // namespace rex::glue
