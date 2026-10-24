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

RawSolution GreedySolver::solveRaw(const Instance& instance, const SolveOptions& /*options*/,
                                   const Deadline& deadline, ProgressReporter& progress) {
  std::optional<PotentialValues> potential;
  if (score_ == GreedyScore::PotentialValuePerTick) potential.emplace(instance);

  // Score of starting `p` at `tick`; only positive scores are worth a worker slot.
  const auto score = [&](const Process& p, Tick tick) {
    double out_value = 0;
    for (const ResourceAmount& o : p.outputs) {
      const double unit = potential
                              ? potential->value(o.resource, instance.horizon - tick - p.duration)
                              : static_cast<double>(instance.resources[o.resource].sell_price);
      out_value += static_cast<double>(o.qty) * unit;
    }
    double in_value = 0;
    for (const ResourceAmount& i : p.inputs) {
      in_value += static_cast<double>(i.qty * instance.resources[i.resource].sell_price);
    }
    return (out_value - in_value) / static_cast<double>(p.duration);
  };

  RawSolution result;
  SimState sim(instance);
  while (!deadline.expired()) {
    while (true) {
      std::optional<ProcessIndex> best;
      double best_score = 0;
      for (ProcessIndex p = 0; p < instance.processes.size(); ++p) {
        if (sim.checkStart(p)) continue;
        const double s = score(instance.processes[p], sim.tick());
        if (s > best_score) {  // strict: ties go to the lower process index
          best_score = s;
          best = p;
        }
      }
      if (!best) break;
      sim.start(*best);
      result.runs.push_back({*best, sim.tick()});
    }
    const auto next = sim.nextEventTick();
    if (!next) break;
    sim.advanceTo(*next);
  }
  sim.advanceTo(instance.horizon);
  const auto runs = static_cast<std::int64_t>(result.runs.size());
  result.stats["runs_started"] = runs;
  progress.improved(sim.inventoryValue(), runs);
  return result;
}

}  // namespace pf
