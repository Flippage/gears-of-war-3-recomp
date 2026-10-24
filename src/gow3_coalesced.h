// gow3 - ReXGlue Recompiled Project
//
// Coalesced edits: the game keeps its .ini settings in GearGame\CookedXbox360\Coalesced_*.bin (one
// bundle per language). Edited copies of the player's own bundles are written to the cache folder,
// the runtime is told to serve them in place of the originals (GOW2_VFS_OVERRIDES), and their
// SHA-1 replaces the originals' in the game's HashSec table. The game folder is never modified.
// Adapted from the Gears of War 2 port's gow2_coalesced / gow2_sha_table (BSD-3-Clause, Santiago
// Quiroz Upegui and the gow2-recomp contributors; itself adapted from gears-judgement-recomp).
//
// Edits (gow3_menu_mouse): [GFxUI.GamepadMouse] X / Y = MouseX / MouseY, so the Scaleform menus
// move their mouse pointer with the mouse axes; to stay within each bundle's TOC size, the 15
// mobile / debug bindings of [Engine.PlayerInput] (MOBILE_*) are dropped.
#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace rex::glue {

struct CoalescedHash {
  std::string name;  // file name as the HashSec table spells it, lower case
  std::array<uint8_t, 20> sha1{};
};

// Before the runtime is created (OnPreSetup): writes the edited copies and sets
// GOW2_VFS_OVERRIDES. Returns the copies' hashes for PatchCoalescedHashes.
std::vector<CoalescedHash> PrepareCoalescedOverrides(const std::filesystem::path& game_root,
                                                     const std::filesystem::path& cache_root);

// After the XEX is in memory (OnPostLoadXexImage): writes the hashes into the HashSec table.
void PatchCoalescedHashes(const std::vector<CoalescedHash>& hashes);

}  // namespace rex::glue
