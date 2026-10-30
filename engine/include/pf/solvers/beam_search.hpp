#pragma once

#include "pf/solver.hpp"

#include <string>

namespace pf {

/// Beam search over the event-based decision tree used by the exact solvers, one action
/// (start a process, or advance to the next completion) per level. Every child is scored
/// by a greedy_potential rollout from it, which is also a complete schedule, so the best
/// rollout seen is kept as the answer. The `beam_width` (param, default 16) best children
/// survive each level; duplicates (same state and next-process index) are merged.
/// Deterministic; never proves optimality. Stats: levels, nodes, rollouts.
class BeamSearchSolver final : public Solver {
 public:
  static constexpr double kDefaultWidth = 16;

  [[nodiscard]] std::string name() const override { return "beam_search"; }

 protected:
  RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                       const Deadline& deadline, ProgressReporter& progress) override;
};

}  // namespace pf
