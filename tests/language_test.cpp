#include <algorithm>
#include <filesystem>

#include "gow3_language.h"
#include "test_util.h"

namespace fs = std::filesystem;

TEST(scan_game_languages) {
  auto game = TempDir("languages");
  const fs::path cooked = game / "GearGame" / "CookedXbox360";
  WriteFile(cooked / "Coalesced_INT.bin", "x");
  WriteFile(cooked / "Coalesced_fra.bin", "x");
  WriteFile(cooked / L"Coalesced_\u0414\u0416\u0418.bin", "x");  // non-ASCII: skipped, no throw
  WriteFile(cooked / L"\u00DCbersicht.txt", "x");
  WriteFile(cooked / "Coalesced_INT.bin.bak", "x");
  auto codes = rex::glue::ScanGameLanguages(game);
  std::sort(codes.begin(), codes.end());
  CHECK(codes == std::vector<std::string>({"FRA", "INT"}));
  CHECK(rex::glue::ScanGameLanguages(game / "missing").empty());
}