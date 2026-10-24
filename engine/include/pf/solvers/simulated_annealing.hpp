#pragma once

#include "pf/solver.hpp"

#include <string>

namespace pf {

/// Simulated annealing over schedules, starting from the greedy_potential schedule.
///
/// Moves: insert a run (random process, at an event tick of the current schedule or a
/// uniformly random tick), remove a run, shift a run's start by a few ticks, or change a
/// run's process. Repair: the candidate is replayed in start order through the rule core
/// (SimState) and every run that is illegal at that point is dropped, so every candidate is
/// valid. Metropolis acceptance with geometric cooling from `initial_temperature`
/// (0 = automatic: 5% of the starting earnings, at least 1) down to
/// `initial_temperature * final_temperature_ratio` over `iterations` steps.
///
/// Seeded by SolveOptions::seed: deterministic when the iteration budget, not the time
/// limit, ends the run. Stats: iterations, accepted, improvements.
class SimulatedAnnealingSolver final : public Solver {
 public:
  static constexpr double kDefaultIterations = 20'000;
  static constexpr double kDefaultFinalRatio = 1e-3;

  [[nodiscard]] std::string name() const override { return "simulated_annealing"; }

 protected:
  RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                       const Deadline& deadline, ProgressReporter& progress) override;
};

}  // namespace pf
