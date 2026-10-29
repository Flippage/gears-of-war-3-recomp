#include "setup/launch_decision.h"

namespace gow3::setup {

std::vector<const FoundPackage*> NewPackages(const std::vector<FoundPackage>& found,
                                             const LauncherState& s, Build build) {
  std::vector<const FoundPackage*> out;
  for (const auto& p : found) {
    if (s.installed.count(p.id) || s.declined.count(p.id)) continue;
    if (p.is_tu && (build == Build::kTu6 || !p.matches_disc)) continue;
    out.push_back(&p);
  }
  return out;
}

LaunchAction DecideLaunch(const LaunchInputs& in) {
  if (!in.game_valid) return LaunchAction::kSetup;
  if (in.state->existed && in.state->build != in.build && in.recorded_build_available) return LaunchAction::kRepair;
  bool has_new = !NewPackages(*in.packages, *in.state, in.build).empty();
  // The base build also hands over once to ask about TU6 / DLC (an install from before they were
  // supported): the launcher records the answer (content_asked).
  if (in.build == Build::kBase) {
    return has_new || !in.state->content_asked ? LaunchAction::kOffer : LaunchAction::kStartGame;
  }
  if (!in.tu6_present) return LaunchAction::kRepair;
  return has_new ? LaunchAction::kOffer : LaunchAction::kStartGame;
}

}  // namespace gow3::setup
