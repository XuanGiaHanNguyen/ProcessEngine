#pragma once

#include "pf/model.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pf {

/// The kinds of rule violation the simulation can detect (section 2 of PROJECT_PLAN.md).
enum class RuleViolation : std::uint8_t {
  UnknownProcess,         ///< process index out of range
  StartInPast,            ///< start tick before the current tick (or negative)
  PastHorizon,            ///< run would finish after T (rule 4)
  InsufficientInventory,  ///< inputs not available (rule 5)
  NoFreeWorker,           ///< all W worker slots busy (rule 3)
  MaxRunsExceeded,        ///< process already ran max_runs times
  Overflow,               ///< a quantity or the earnings left the int64 range
};

/// Stable machine-readable name, e.g. "insufficient_inventory".
[[nodiscard]] const char* toString(RuleViolation kind);

/// A rule violation with a human-readable explanation such as
/// "not enough iron (need 3, have 1)".
struct RuleError {
  RuleViolation kind;
  std::string message;
};

/// A run that has started but not yet produced its outputs.
struct ActiveRun {
  ProcessIndex process = 0;
  Tick start = 0;
  Tick end = 0;  ///< start + duration; outputs arrive at this tick

  bool operator==(const ActiveRun&) const = default;
};

/// The single implementation of the game rules. Both the one-shot validator and the
/// interactive GameSession are built on this class so they cannot disagree.
///
/// Semantics: at tick t, runs with end <= t have completed (their outputs are in the
/// inventory and their worker slots are free). Starting a run at t consumes its inputs
/// immediately and occupies a worker until its end tick.
///
/// The instance must outlive the SimState and must be well formed (see checkInstance).
class SimState {
 public:
  explicit SimState(const Instance& instance);

  [[nodiscard]] const Instance& instance() const { return *instance_; }
  [[nodiscard]] Tick tick() const { return tick_; }
  [[nodiscard]] const std::vector<Quantity>& inventory() const { return inventory_; }
  /// Runs in progress, ordered by (end, start, process).
  [[nodiscard]] const std::vector<ActiveRun>& activeRuns() const { return active_; }
  /// Number of times each process has been started so far.
  [[nodiscard]] const std::vector<std::int32_t>& runCounts() const { return run_counts_; }
  [[nodiscard]] std::int32_t freeWorkers() const;

  /// Returns why `process` cannot start at the current tick, or nullopt if it can.
  [[nodiscard]] std::optional<RuleError> checkStart(ProcessIndex process) const;

  /// Starts `process` at the current tick. Precondition: checkStart(process) is nullopt.
  void start(ProcessIndex process);

  /// Advances the clock to `t`, completing every run with end <= t.
  /// Precondition: tick() <= t <= horizon.
  void advanceTo(Tick t);

  /// The earliest tick > tick() at which a run completes, if any run is in progress.
  [[nodiscard]] std::optional<Tick> nextEventTick() const;

  /// start_money + value of the current inventory (runs in progress are not counted).
  /// Throws std::overflow_error if the value does not fit in Quantity.
  [[nodiscard]] Quantity inventoryValue() const;

  bool operator==(const SimState& other) const;

 private:
  const Instance* instance_;
  Tick tick_ = 0;
  std::vector<Quantity> inventory_;
  std::vector<ActiveRun> active_;
  std::vector<std::int32_t> run_counts_;
};

}  // namespace pf
