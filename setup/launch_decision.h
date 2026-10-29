#pragma once
#include <vector>

#include "setup/finder.h"
#include "setup/state.h"

namespace gow3::setup {

enum class LaunchAction { kStartGame, kSetup, kOffer, kRepair };
struct LaunchInputs {
  Build build;
  bool game_valid;
  bool tu6_present;  // <game>\TU6\default.xexp exists
  const std::vector<FoundPackage>* packages;
  const LauncherState* state;
};

// (1) game not valid: kSetup. (2) base build with new packages: kOffer. (3) TU6 build without
// tu6_present: kRepair. (4) TU6 build with new DLC: kOffer. (5) otherwise kStartGame.
LaunchAction DecideLaunch(const LaunchInputs& in);

// Not installed, not declined, and (for a TU) matches_disc. For Build::kTu6 a TU is never "new".
std::vector<const FoundPackage*> NewPackages(const std::vector<FoundPackage>& found,
                                             const LauncherState& s);

}  // namespace gow3::setup
