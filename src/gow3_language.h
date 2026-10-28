// gow3 - ReXGlue Recompiled Project
//
// Game language (gow3_language, PC Options -> Display): the languages this copy of the game has
// (GearGame\CookedXbox360\Coalesced_<code>.bin), picked by the console language and region the game
// reads (user_language, user_country). auto follows Windows' display language.
#pragma once

#include <filesystem>
#include <string_view>

namespace rex::glue {

// Finds the game's languages and sets user_language / user_country for the chosen one. Runs at the
// start of OnPreSetup, before the kernel reads them.
void PrepareLanguage(const std::filesystem::path& game_root);
// Whether this copy of the game has the language (Unreal Engine's code: INT, FRA, RUS, ...).
bool GameHasLanguage(std::string_view code);

}  // namespace rex::glue
