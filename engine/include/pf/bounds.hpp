#pragma once

#include "pf/model.hpp"
#include "pf/rules.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pf {

/// An admissible upper bound on the final earnings reachable from any state.
///
/// It is a potential (LP-dual) argument. Pick a worker-tick price lambda >= 0 and unit
/// values mu_r(tau) for "one unit of r with tau ticks left" such that
///   (1) mu_r(0) >= sell_price(r) and mu_r(tau) is non-decreasing in tau,
///   (2) every process p with duration d <= tau satisfies
///         sum_out q_o * mu_o(tau - d) <= sum_in q_i * mu_i(tau) + lambda * d.
/// Then Phi(state) = start_money + sum_r inv_r * mu_r(T - t)
///                   + sum_{runs in progress} sum_out q_o * mu_o(T - end)
///                   + lambda * (free worker-ticks left before T)
/// never increases when a run starts (by 2) or time passes (by 1), and equals the
/// earnings at T. So Phi(state) bounds every schedule from that state. Worker limits only
/// enter through lambda and max_runs is ignored, so this is a relaxation.
///
/// For each candidate lambda, mu is built bottom-up in tau; when a process violates (2),
/// the deficit is spread over its inputs in proportion to their quantities. Processes
/// without inputs can only be paid for by lambda, so lambda is at least the smallest value
/// that covers them (found by bisection). A few lambda values are kept and the smallest
/// bound is used. See docs/solvers.md.
/// A run in progress, as seen by DualBound: when it ends and what it produces.
struct RunRef {
  Tick end = 0;
  ProcessIndex process = 0;
};

class DualBound {
 public:
  /// Builds the tables. Throws std::length_error if the tables would be too large
  /// (horizon * resources above kMaxTableEntries).
  explicit DualBound(const Instance& instance);

  static constexpr std::size_t kMaxTableEntries = 20'000'000;

  /// Upper bound on the final earnings reachable from `state` (an integer, rounded down
  /// with a small tolerance). Always >= the true optimum from that state.
  [[nodiscard]] Quantity upperBound(const SimState& state) const;
  /// Same bound from raw parts: the tick, an inventory array (one entry per resource), and
  /// the runs in progress (used by the solvers' compact states).
  [[nodiscard]] Quantity upperBound(Tick tick, const Quantity* inventory,
                                    const std::vector<RunRef>& running) const;

  /// The worker-tick prices in use (for docs and tests).
  [[nodiscard]] const std::vector<double>& lambdas() const { return lambdas_; }

 private:
  struct Table {
    double lambda = 0;
    std::vector<double> mu;  // (tau, resource) row-major
  };

  [[nodiscard]] Table buildTable(double lambda) const;
  /// Smallest lambda' such that inputless processes satisfy condition (2) under table(lambda').
  [[nodiscard]] double generatorDemand(const Table& table) const;
  [[nodiscard]] double bound(const Table& table, Tick tick, const Quantity* inventory,
                             const std::vector<RunRef>& running) const;

  const Instance* instance_;
  std::size_t resources_;
  std::vector<Table> tables_;
  std::vector<double> lambdas_;
};

}  // namespace pf
