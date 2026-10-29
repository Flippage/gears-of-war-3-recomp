// gow3 - ReXGlue Recompiled Project
//
// Game language (gow3_language, PC Options -> Display): the languages this copy of the game has
// (GearGame\CookedXbox360\Coalesced_<code>.bin), picked by the console language and region the game
// reads (user_language, user_country). auto follows Windows' display language.
#pragma once

#include <filesystem>
#include <set>
#include <string>
#include <string_view>

namespace rex::glue {

// Finds the game's languages and sets user_language / user_country for the chosen one. Runs at the
// start of OnPreSetup, before the kernel reads them.
void PrepareLanguage(const std::filesystem::path& game_root);
// Whether this copy of the game has the language (Unreal Engine's code: INT, FRA, RUS, ...).
bool GameHasLanguage(std::string_view code);
// The registry behind GameHasLanguage (gow3_language_registry.cpp, no rex runtime needed).
// Only PrepareLanguage and the launcher's game scan should call this.
void NoteGameLanguage(const std::string& code);
const std::set<std::string>& GameLanguages();

}  // namespace rex::glue
