#pragma once

#include "pf/solver.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pf {

/// The time-indexed LP relaxation of an instance and the upper bound derived from it.
///
/// Variables x[p][t] >= 0: (fractional) number of runs of p started at tick t, for
/// t in [0, T - d_p]. Maximize start value + sum (net market value of p) * x[p][t] subject to
///   inventory:  for every consumed resource r and tick tau < T,
///               sum_{t <= tau} in[p][r] x[p][t] - sum_{t + d_p <= tau} out[p][r] x[p][t]
///                 <= start[r]
///   workers:    for every tick tau < T, sum_{t <= tau < t + d_p} x[p][t] <= W
///   max_runs:   sum_t x[p][t] <= max_runs[p]
/// Every valid schedule is a feasible integer point, so the LP optimum bounds the optimum.
struct LpRelaxation {
  /// Rigorous integer upper bound: computed from the dual solution and corrected for any
  /// dual infeasibility (each x[p][t] <= W), then capped by DualBound; valid even if the
  /// simplex stopped early.
  Quantity bound = 0;
  double lp_objective = 0;  ///< primal LP value (if the LP was solved)
  bool solved = false;      ///< simplex reached optimality
  bool too_large = false;   ///< LP skipped: bound is the DualBound alone
  std::int64_t iterations = 0;
  std::int64_t variables = 0;
  std::int64_t constraints = 0;
  /// x[p][t], indexed [p][t] (t up to T - d_p); empty if too_large.
  std::vector<std::vector<double>> x;
};

/// Largest LP (rows * (rows + cols) dense tableau entries) solveLp will attempt.
inline constexpr std::size_t kMaxLpTableau = 6'000'000;

/// Builds and solves the relaxation, stopping early if `deadline` expires.
[[nodiscard]] LpRelaxation solveLp(const Instance& instance, const Deadline& deadline);

/// Heuristic guided by the LP: walks the ticks, accumulating each process's planned LP
/// mass and starting a run whenever a process has accumulated >= 0.5 and the start is
/// legal; a second variant also fills idle workers greedily. Returns the best of the two
/// variants and the greedy_potential schedule. Stats include lp_bound (the relaxation's
/// upper bound, always >= the optimum), lp_objective, lp_iterations, lp_solved.
class LPRelaxationSolver final : public Solver {
 public:
  [[nodiscard]] std::string name() const override { return "lp_relaxation"; }

 protected:
  RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                       const Deadline& deadline, ProgressReporter& progress) override;
};

}  // namespace pf
