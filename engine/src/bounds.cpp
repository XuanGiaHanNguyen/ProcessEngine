#include "pf/bounds.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace pf {

DualBound::DualBound(const Instance& instance)
    : instance_(&instance), resources_(instance.resources.size()) {
  const auto entries = (static_cast<std::size_t>(instance.horizon) + 1) * resources_;
  if (entries > kMaxTableEntries) {
    throw std::length_error("instance too large for the dual bound tables (horizon " +
                            std::to_string(instance.horizon) + " x " + std::to_string(resources_) +
                            " resources)");
  }

  // Smallest lambda that pays for the inputless processes, by bisection: demand(lambda) is
  // non-increasing in lambda, and lambda = demand(0) is always feasible.
  double lambda_min = 0;
  const double demand0 = generatorDemand(buildTable(0));
  if (demand0 > 0) {
    double lo = 0;
    double hi = demand0;
    for (int i = 0; i < 40; ++i) {
      const double mid = (lo + hi) / 2;
      if (generatorDemand(buildTable(mid)) <= mid) {
        hi = mid;
      } else {
        lo = mid;
      }
    }
    lambda_min = hi;
  }

  // Best market gain per worker-tick, used to spread a few more candidates above lambda_min.
  double gain_rate = 0;
  for (const Process& p : instance.processes) {
    double gain = 0;
    for (const ResourceAmount& o : p.outputs) {
      gain += static_cast<double>(o.qty * instance.resources[o.resource].sell_price);
    }
    for (const ResourceAmount& i : p.inputs) {
      gain -= static_cast<double>(i.qty * instance.resources[i.resource].sell_price);
    }
    gain_rate = std::max(gain_rate, gain / static_cast<double>(p.duration));
  }

  lambdas_ = {lambda_min};
  if (entries * 4 <= kMaxTableEntries && gain_rate > 0) {
    for (const double k : {0.5, 1.0, 2.0}) lambdas_.push_back(lambda_min + k * gain_rate);
  }
  for (const double lambda : lambdas_) tables_.push_back(buildTable(lambda));
}

DualBound::Table DualBound::buildTable(double lambda) const {
  const Instance& inst = *instance_;
  const auto horizon = static_cast<std::size_t>(inst.horizon);
  Table table;
  table.lambda = lambda;
  table.mu.assign((horizon + 1) * resources_, 0);
  for (std::size_t r = 0; r < resources_; ++r) {
    table.mu[r] = static_cast<double>(inst.resources[r].sell_price);
  }
  for (std::size_t tau = 1; tau <= horizon; ++tau) {
    double* row = &table.mu[tau * resources_];
    std::copy_n(row - resources_, resources_, row);  // non-decreasing in tau
    for (const Process& p : inst.processes) {
      const auto d = static_cast<std::size_t>(p.duration);
      if (p.inputs.empty() || d > tau) continue;
      const double* later = &table.mu[(tau - d) * resources_];
      double out_value = 0;
      for (const ResourceAmount& o : p.outputs) {
        out_value += static_cast<double>(o.qty) * later[o.resource];
      }
      double in_value = 0;
      double in_qty = 0;
      for (const ResourceAmount& i : p.inputs) {
        in_value += static_cast<double>(i.qty) * row[i.resource];
        in_qty += static_cast<double>(i.qty);
      }
      const double deficit = out_value - in_value - lambda * static_cast<double>(d);
      if (deficit > 0) {
        // Raising mu at this tau only helps the other constraints at this tau (it is their
        // input side), so one pass per tau is enough.
        const double raise = deficit / in_qty;
        for (const ResourceAmount& i : p.inputs) row[i.resource] += raise;
      }
    }
  }
  return table;
}

double DualBound::generatorDemand(const Table& table) const {
  const Instance& inst = *instance_;
  double demand = 0;
  for (const Process& p : inst.processes) {
    if (!p.inputs.empty() || p.duration > inst.horizon) continue;
    // mu is non-decreasing in tau, so the tightest tau is the largest: tau = T.
    const double* later =
        &table.mu[static_cast<std::size_t>(inst.horizon - p.duration) * resources_];
    double out_value = 0;
    for (const ResourceAmount& o : p.outputs) {
      out_value += static_cast<double>(o.qty) * later[o.resource];
    }
    demand = std::max(demand, out_value / static_cast<double>(p.duration));
  }
  return demand;
}

double DualBound::bound(const Table& table, const SimState& state) const {
  const Instance& inst = *instance_;
  const Tick tau = inst.horizon - state.tick();
  const double* row = &table.mu[static_cast<std::size_t>(tau) * resources_];
  auto total = static_cast<double>(inst.start_money);
  for (std::size_t r = 0; r < resources_; ++r) {
    total += static_cast<double>(state.inventory()[r]) * row[r];
  }
  double busy = 0;
  for (const ActiveRun& run : state.activeRuns()) {
    const double* at_end = &table.mu[static_cast<std::size_t>(inst.horizon - run.end) * resources_];
    for (const ResourceAmount& o : inst.processes[run.process].outputs) {
      total += static_cast<double>(o.qty) * at_end[o.resource];
    }
    busy += static_cast<double>(run.end - state.tick());
  }
  const double free_worker_ticks =
      static_cast<double>(inst.workers) * static_cast<double>(tau) - busy;
  return total + table.lambda * free_worker_ticks;
}

Quantity DualBound::upperBound(const SimState& state) const {
  double best = bound(tables_.front(), state);
  for (std::size_t i = 1; i < tables_.size(); ++i) best = std::min(best, bound(tables_[i], state));
  // Round down, but never below the true value because of floating-point error.
  const double tolerance = 1e-7 * std::max(1.0, std::abs(best));
  return static_cast<Quantity>(std::floor(best + tolerance));
}

}  // namespace pf
