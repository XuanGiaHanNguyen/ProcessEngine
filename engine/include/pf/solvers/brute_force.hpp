#pragma once

#include "pf/solver.hpp"

#include <cstdint>
#include <string>

namespace pf {

/// Exhaustive search, used as ground truth in tests. At every tick it branches on every
/// multiset of legal starts (processes taken in non-decreasing index order so each multiset
/// is enumerated once), then advances one tick -- or straight to the next completion when no
/// start is legal, since nothing can change before then. No pruning, no memoization.
///
/// Guarded by a size limit: refuses instances with more than kMaxProcesses processes or a
/// horizon above kMaxHorizon, and gives up cleanly after `max_nodes` nodes
/// (param "max_nodes", default kDefaultMaxNodes). proven_optimal is set only when the
/// search ran to completion.
class BruteForceSolver final : public Solver {
 public:
  static constexpr std::size_t kMaxProcesses = 8;
  static constexpr Tick kMaxHorizon = 24;
  static constexpr std::int64_t kDefaultMaxNodes = 20'000'000;

  [[nodiscard]] std::string name() const override { return "brute_force"; }

 protected:
  RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                       const Deadline& deadline, ProgressReporter& progress) override;
};

}  // namespace pf
