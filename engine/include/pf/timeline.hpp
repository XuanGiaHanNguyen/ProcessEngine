#pragma once

#include "pf/model.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace pf {

/// One run placed on a worker slot, for Gantt charts.
struct TimelineRun {
  std::size_t run_index = 0;  ///< position in the input schedule
  ProcessIndex process = 0;
  Tick start = 0;
  Tick end = 0;
  std::int32_t slot = 0;  ///< worker slot 0..W-1; slots are reused lowest-first
};

/// Inventory right after everything that happens at `tick` (completions, then starts).
struct TimelinePoint {
  Tick tick = 0;
  std::vector<Quantity> inventory;
  std::int32_t busy_workers = 0;
  Quantity value = 0;  ///< start_money + inventory at sell prices (runs in progress excluded)
};

/// A valid schedule laid out over time: what the results view draws.
struct Timeline {
  std::vector<TimelineRun> runs;      ///< sorted by start, then schedule order
  std::vector<TimelinePoint> points;  ///< tick 0, every start/end tick, and T, ascending
  Quantity earnings = 0;
};

/// Replays `schedule` with the shared rule core (SimState) and records the layout.
/// Returns nullopt if the schedule is invalid (use validate() for the reason).
[[nodiscard]] std::optional<Timeline> buildTimeline(const Instance& instance,
                                                    const Schedule& schedule);

}  // namespace pf
