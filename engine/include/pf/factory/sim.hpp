#pragma once

#include "pf/factory/level.hpp"

#include <cstdint>
#include <vector>

namespace pf::factory {

/// A machine's state after the Start step of an hour.
enum class MachineStatus : std::uint8_t {
  Busy,     ///< a batch is running
  Blocked,  ///< inputs are ready but an output buffer has no room for a batch
  Starved,  ///< some input is missing
};

/// One machine during one hour.
struct MachineHour {
  MachineStatus status = MachineStatus::Starved;
  int progress = 0;            ///< hours of the running batch done, counting this one (Busy only)
  int total = 0;               ///< hours per batch
  std::vector<Index> missing;  ///< inputs short of a batch (Starved only)
  std::vector<std::int64_t> inputs;   ///< input buffer after Dispatch, per recipe input
  std::vector<std::int64_t> outputs;  ///< output buffer after Dispatch, per recipe output
  int batches_done = 0;               ///< batches completed so far, including this hour

  bool operator==(const MachineHour&) const = default;
};

/// Everything the UI needs to replay one hour.
struct HourReport {
  int hour = 0;
  Cents revenue = 0;
  Cents purchases = 0;
  Cents profit = 0;                   ///< cumulative, starting at minus the build cost
  std::vector<std::int64_t> sold;     ///< units sold this hour, per resource
  std::vector<MachineHour> machines;  ///< per design machine, in design order
  std::vector<int> belt_moved;        ///< units dispatched this hour, per design belt
  std::vector<std::int64_t> stock;    ///< supplier stock after Dispatch, per supplier

  bool operator==(const HourReport&) const = default;
};

/// The outcome of a test: profit = sales - purchases - build.
struct SimResult {
  Cents profit = 0;
  Cents sales = 0;
  Cents purchases = 0;
  Cents build = 0;
  std::vector<std::int64_t> sold;  ///< total units sold, per resource
  std::vector<HourReport> hours;   ///< empty unless a report was requested

  bool operator==(const SimResult&) const = default;
};

/// Runs a design for the level's hours with the rules of docs/factory-spec.md. This is the
/// only implementation of the rules: tests, playback and every factory solver call it.
/// Throws std::invalid_argument if checkDesign(level, design) is not empty.
[[nodiscard]] SimResult simulate(const Level& level, const Design& design, bool with_report = true);

}  // namespace pf::factory
