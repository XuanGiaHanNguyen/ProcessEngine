#pragma once

#include "pf/factory/level.hpp"

#include <optional>
#include <string>

namespace pf::factory {

/// One change to a design, found by suggestChange().
struct DesignHint {
  Design design;            ///< the design with the change applied
  Cents profit = 0;         ///< simulate(level, design).profit
  Cents gain = 0;           ///< profit minus the current design's profit
  std::string description;  ///< what to do, in words ("Order 2 more Cotton per hour")
};

/// The single change that raises profit the most, or nullopt if no single change helps.
/// Tries every order step, belt weight step, belt removal, new belt, machine removal, recipe
/// switch and new machine (belted to every producer and consumer). Each candidate is checked
/// and scored with simulate(), so a hint is never invalid and its profit is exact. `design`
/// must be valid. Stops trying candidates after `time_limit_ms` and returns the best so far.
[[nodiscard]] std::optional<DesignHint> suggestChange(const Level& level, const Design& design,
                                                      double time_limit_ms = 2000);

}  // namespace pf::factory
