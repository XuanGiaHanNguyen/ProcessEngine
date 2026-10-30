#pragma once

#include "pf/solver.hpp"

#include <cstdint>
#include <string>

namespace pf {

/// Exact dynamic programming over event states, with memoization.
///
/// State: (tick, inventory, runs in progress as (end, process), run counts of capped
/// processes), taken at tick 0 and at completion ticks. Decisions are made only at those
/// event ticks: an exchange argument (docs/solvers.md) shows some optimal schedule starts
/// every run at tick 0 or at a completion tick. At each event state the solver enumerates
/// every multiset of legal starts, then advances to the next completion;
///   V(s) = max over start multisets M of V(advance(start(s, M)))
/// and V = inventory value when nothing is running and nothing more is started.
///
/// Fails cleanly when more than `max_states` (param, default kDefaultMaxStates) distinct
/// event states are stored or the time limit runs out; it has no anytime answer.
class DPSolver final : public Solver {
 public:
  static constexpr std::int64_t kDefaultMaxStates = 2'000'000;

  [[nodiscard]] std::string name() const override { return "dp"; }

 protected:
  RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                       const Deadline& deadline, ProgressReporter& progress) override;
};

}  // namespace pf
