#include "setup/launch_decision.h"

namespace gow3::setup {

namespace {
std::vector<const FoundPackage*> NewFor(const std::vector<FoundPackage>& found, const LauncherState& s,
                                        Build build) {
  std::vector<const FoundPackage*> out;
  for (const auto& p : found) {
    if (s.installed.count(p.id) || s.declined.count(p.id)) continue;
    if (p.is_tu && (build == Build::kTu6 || !p.matches_disc)) continue;
    out.push_back(&p);
  }
  return out;
}
}  // namespace

std::vector<const FoundPackage*> NewPackages(const std::vector<FoundPackage>& found,
                                             const LauncherState& s) {
  return NewFor(found, s, s.build);
}

LaunchAction DecideLaunch(const LaunchInputs& in) {
  if (!in.game_valid) return LaunchAction::kSetup;
  bool has_new = !NewFor(*in.packages, *in.state, in.build).empty();
  if (in.build == Build::kBase) return has_new ? LaunchAction::kOffer : LaunchAction::kStartGame;
  if (!in.tu6_present) return LaunchAction::kRepair;
  return has_new ? LaunchAction::kOffer : LaunchAction::kStartGame;
}

}  // namespace gow3::setup
