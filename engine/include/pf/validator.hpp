#pragma once

#include "pf/model.hpp"
#include "pf/rules.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace pf {

/// The first rule violation found while replaying a schedule.
struct Violation {
  RuleViolation kind;
  std::size_t run_index;  ///< index of the offending entry in the input schedule
  Tick tick;              ///< tick at which the violation happened
  std::string message;    ///< e.g. "run #3 at tick 4: not enough iron (need 3, have 1)"
};

/// Outcome of validate(). When valid, `earnings` and `final_inventory` describe tick T.
struct ValidationReport {
  bool valid = false;
  std::optional<Violation> violation;
  Quantity earnings = 0;
  std::vector<Quantity> final_inventory;
};

/// Replays `schedule` tick by tick against the rules and returns the first violation or
/// the final earnings. Independent of every solver: all solver output is checked here.
/// Runs starting at the same tick are applied in their schedule order; since outputs only
/// arrive at later ticks, that order never changes whether the schedule is valid.
/// Precondition: checkInstance(instance) is empty.
[[nodiscard]] ValidationReport validate(const Instance& instance, const Schedule& schedule);

}  // namespace pf
