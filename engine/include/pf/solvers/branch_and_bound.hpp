#pragma once

#include "pf/bounds.hpp"
#include "pf/rules.hpp"
#include "pf/solver.hpp"

#include <cstdint>
#include <string>

namespace pf {

/// Outcome of a branch-and-bound search from some state.
struct SearchResult {
  Schedule runs;           ///< best continuation found (runs started at or after the root tick)
  Quantity earnings = 0;   ///< final earnings of that continuation
  bool completed = false;  ///< the whole tree was searched: `earnings` is optimal
  std::int64_t nodes = 0;
  std::int64_t pruned = 0;
  std::int64_t transposition_hits = 0;
  Quantity root_bound = 0;
};

/// Depth-first branch and bound over the event-based decision tree used by DPSolver
/// (decisions at tick 0 and completion ticks; start multisets in non-decreasing process
/// order). Children are ordered best-first by their DualBound upper bound and pruned when
/// that bound cannot beat the incumbent, which starts as the better of the two greedy
/// rollouts. A transposition set skips (state, next-process) pairs whose subtree was
/// already searched to completion: anything better in it would already be the incumbent.
class BranchAndBoundSearch {
 public:
  /// `progress` may be null. `max_transpositions` caps the transposition set's size.
  BranchAndBoundSearch(const Instance& instance, const DualBound& bound, const Deadline& deadline,
                       ProgressReporter* progress, std::int64_t max_transpositions);

  /// Searches every continuation of `root` (which must be at an event tick, or tick 0).
  [[nodiscard]] SearchResult run(const SimState& root);

 private:
  class Impl;
  const Instance& instance_;
  const DualBound& bound_;
  const Deadline& deadline_;
  ProgressReporter* progress_;
  std::int64_t max_transpositions_;
};

/// Exact solver wrapping BranchAndBoundSearch. Anytime: on timeout it returns the
/// incumbent with proven_optimal = false. Stats: nodes, pruned, transposition_hits,
/// root_bound.
class BranchAndBoundSolver final : public Solver {
 public:
  static constexpr std::int64_t kDefaultMaxTranspositions = 4'000'000;

  [[nodiscard]] std::string name() const override { return "branch_and_bound"; }

 protected:
  RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                       const Deadline& deadline, ProgressReporter& progress) override;
};

}  // namespace pf
