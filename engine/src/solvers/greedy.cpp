#include "pf/solvers/greedy.hpp"

#include "pf/rules.hpp"

#include <algorithm>
#include <optional>

namespace pf {

PotentialValues::PotentialValues(const Instance& instance)
    : resources_(instance.resources.size()),
      max_tau_(std::min(instance.horizon, kMaxTau)),
      table_((static_cast<std::size_t>(max_tau_) + 1) * resources_) {
  for (Tick tau = 0; tau <= max_tau_; ++tau) {
    double* row = &table_[static_cast<std::size_t>(tau) * resources_];
    for (std::size_t r = 0; r < resources_; ++r) {
      row[r] = static_cast<double>(instance.resources[r].sell_price);
    }
    for (const Process& p : instance.processes) {
      if (p.duration > tau) continue;
      const double* later = &table_[static_cast<std::size_t>(tau - p.duration) * resources_];
      double out_value = 0;
      for (const ResourceAmount& o : p.outputs) {
        out_value += static_cast<double>(o.qty) * later[o.resource];
      }
      double in_price = 0;
      for (const ResourceAmount& i : p.inputs) {
        in_price += static_cast<double>(i.qty * instance.resources[i.resource].sell_price);
      }
      for (const ResourceAmount& i : p.inputs) {
        const auto own = static_cast<double>(i.qty * instance.resources[i.resource].sell_price);
        const double per_unit = (out_value - (in_price - own)) / static_cast<double>(i.qty);
        row[i.resource] = std::max(row[i.resource], per_unit);
      }
    }
  }
}

double PotentialValues::value(ResourceIndex resource, Tick tau) const {
  const Tick t = std::clamp(tau, Tick{0}, max_tau_);
  return table_[(static_cast<std::size_t>(t) * resources_) + resource];
}

std::string GreedySolver::name() const {
  return score_ == GreedyScore::MarketValuePerTick ? "greedy" : "greedy_potential";
}

std::optional<ProcessIndex> greedyChoice(const SimState& state, GreedyScore score,
                                         const PotentialValues* potential) {
  const Instance& instance = state.instance();
  const bool use_potential = score == GreedyScore::PotentialValuePerTick;
  std::optional<ProcessIndex> best;
  double best_score = 0;
  for (ProcessIndex index = 0; index < instance.processes.size(); ++index) {
    if (!state.canStart(index)) continue;
    const Process& p = instance.processes[index];
    double out_value = 0;
    for (const ResourceAmount& o : p.outputs) {
      const double unit =
          use_potential ? potential->value(o.resource, instance.horizon - state.tick() - p.duration)
                        : static_cast<double>(instance.resources[o.resource].sell_price);
      out_value += static_cast<double>(o.qty) * unit;
    }
    double in_value = 0;
    for (const ResourceAmount& i : p.inputs) {
      in_value += static_cast<double>(i.qty * instance.resources[i.resource].sell_price);
    }
    const double s = (out_value - in_value) / static_cast<double>(p.duration);
    if (s > best_score) {  // strict: ties go to the lower process index
      best_score = s;
      best = index;
    }
  }
  return best;
}

Rollout greedyRollout(SimState state, GreedyScore score, const PotentialValues* potential) {
  Rollout out;
  while (true) {
    while (const auto choice = greedyChoice(state, score, potential)) {
      out.runs.push_back({*choice, state.tick()});
      state.start(*choice);
    }
    const auto next = state.nextEventTick();
    if (!next) break;
    state.advanceTo(*next);
  }
  state.advanceTo(state.instance().horizon);
  out.earnings = state.inventoryValue();
  return out;
}

RawSolution GreedySolver::solveRaw(const Instance& instance, const SolveOptions& /*options*/,
                                   const Deadline& /*deadline*/, ProgressReporter& progress) {
  // Greedy makes at most one pass over the event ticks, so it ignores the time limit.
  std::optional<PotentialValues> potential;
  if (score_ == GreedyScore::PotentialValuePerTick) potential.emplace(instance);
  Rollout rollout = greedyRollout(SimState(instance), score_, potential ? &*potential : nullptr);

  RawSolution result;
  result.runs = std::move(rollout.runs);
  const auto runs = static_cast<std::int64_t>(result.runs.size());
  result.stats["runs_started"] = runs;
  progress.improved(rollout.earnings, runs);
  return result;
}

}  // namespace pf
