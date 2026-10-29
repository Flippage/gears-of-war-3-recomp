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
  // builds\gow3-<state->build>.exe exists next to gow3.exe (a release with both builds).
  bool recorded_build_available = false;
};

// (1) game not valid: kSetup. (2) launcher.ini records the other build (state->existed, its build is
// not `build`) and recorded_build_available: kRepair (gow3.exe was replaced, e.g. by extracting a
// new release over the folder). (3) base build with new packages: kOffer. (4) TU6 build without
// tu6_present: kRepair. (5) TU6 build with new DLC: kOffer. (6) otherwise kStartGame.
LaunchAction DecideLaunch(const LaunchInputs& in);

// Not installed, not declined, and (for a TU) matches_disc. For Build::kTu6 a TU is never "new".
// Callers pass the build actually installed (the same one given to DecideLaunch).
std::vector<const FoundPackage*> NewPackages(const std::vector<FoundPackage>& found,
                                             const LauncherState& s, Build build);

}  // namespace gow3::setup
