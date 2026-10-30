#pragma once

#include "pf/rules.hpp"
#include "pf/solver.hpp"

#include <optional>
#include <string>
#include <vector>

namespace pf {

/// How GreedySolver ranks the processes it could start.
enum class GreedyScore : std::uint8_t {
  /// (market value of outputs - market value of inputs) / duration.
  MarketValuePerTick,
  /// Like MarketValuePerTick, but outputs are valued at their time-aware potential
  /// (see PotentialValues), so intermediates with sell_price 0 still look useful.
  PotentialValuePerTick,
};

/// Event-driven greedy: at each event tick, repeatedly start the legal process with the
/// highest positive score; when none is left, jump to the next completion. O(E * P) score
/// evaluations for E events and P processes. Never proves optimality.
class GreedySolver final : public Solver {
 public:
  explicit GreedySolver(GreedyScore score = GreedyScore::MarketValuePerTick) : score_(score) {}

  [[nodiscard]] std::string name() const override;

 protected:
  RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                       const Deadline& deadline, ProgressReporter& progress) override;

 private:
  GreedyScore score_;
};

/// Time-aware "what is one unit of this resource worth with tau ticks left" estimate:
///   v_r(tau) = max(sell_price(r),
///                  max over processes p consuming r with duration d <= tau of
///                  (sum_out q_o * v_o(tau - d) - sum_{other inputs j} q_j * price(j)) / q_r)
/// Values for tau > kMaxTau reuse kMaxTau to bound memory. A heuristic, not a bound: a
/// resource needed by several processes may be over-valued.
class PotentialValues {
 public:
  static constexpr Tick kMaxTau = 2048;

  explicit PotentialValues(const Instance& instance);

  /// Estimated value of one unit of `resource` with `tau` ticks remaining.
  [[nodiscard]] double value(ResourceIndex resource, Tick tau) const;

 private:
  std::size_t resources_;
  Tick max_tau_;
  std::vector<double> table_;  // (tau, resource) -> value, row-major by tau
};

/// The process greedy would start next from `state`: the legal process with the highest
/// strictly positive score (ties to the lower index), or nullopt if none is worth starting.
/// `potential` must be non-null for GreedyScore::PotentialValuePerTick.
[[nodiscard]] std::optional<ProcessIndex> greedyChoice(const SimState& state, GreedyScore score,
                                                       const PotentialValues* potential);

/// Runs greedy from `state` to the end: returns the runs it starts (with their ticks) and
/// the earnings it reaches. Used for B&B incumbents and Play-mode hints.
struct Rollout {
  Schedule runs;
  Quantity earnings = 0;
};
[[nodiscard]] Rollout greedyRollout(SimState state, GreedyScore score,
                                    const PotentialValues* potential);

}  // namespace pf
