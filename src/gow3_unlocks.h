// gow3 - ReXGlue Recompiled Project
//
// Campaign unlocks. Insane difficulty is locked until the campaign is finished once; the game's
// script reads its unlocks through UOnlinePlayerStorage::GetProfileSettingValueInt, which this
// hooks. gow3_profile_log logs every profile setting the script reads (id, value), to find the
// setting that holds the unlocks.
#pragma once

namespace rex::glue {

// After the XEX is loaded (OnPostSetup).
void InstallUnlockHooks();

}  // namespace rex::glue
