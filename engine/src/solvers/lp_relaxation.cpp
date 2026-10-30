#include "pf/solvers/lp_relaxation.hpp"

#include "pf/bounds.hpp"
#include "pf/rules.hpp"
#include "pf/solvers/greedy.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "simplex.hpp"

namespace pf {

namespace {

double netValue(const Instance& inst, const Process& p) {
  double v = 0;
  for (const ResourceAmount& o : p.outputs) {
    v += static_cast<double>(o.qty * inst.resources[o.resource].sell_price);
  }
  for (const ResourceAmount& i : p.inputs) {
    v -= static_cast<double>(i.qty * inst.resources[i.resource].sell_price);
  }
  return v;
}

}  // namespace

LpRelaxation solveLp(const Instance& inst, const Deadline& deadline) {
  LpRelaxation out;
  const std::size_t P = inst.processes.size();
  const auto T = static_cast<std::size_t>(inst.horizon);

  // Variable numbering.
  std::vector<std::size_t> first_var(P + 1, 0);
  for (std::size_t p = 0; p < P; ++p) {
    const auto d = static_cast<std::size_t>(inst.processes[p].duration);
    first_var[p + 1] = first_var[p] + (d <= T ? T - d + 1 : 0);
  }
  const std::size_t n = first_var[P];

  // Resources that some process consumes need inventory rows.
  std::vector<ResourceIndex> consumed;
  for (ResourceIndex r = 0; r < inst.resources.size(); ++r) {
    for (const Process& p : inst.processes) {
      if (std::any_of(p.inputs.begin(), p.inputs.end(),
                      [&](const ResourceAmount& a) { return a.resource == r; })) {
        consumed.push_back(r);
        break;
      }
    }
  }
  std::vector<ProcessIndex> capped;
  for (ProcessIndex p = 0; p < P; ++p) {
    if (inst.processes[p].max_runs) capped.push_back(p);
  }
  const std::size_t m = consumed.size() * T + T + capped.size();
  out.variables = static_cast<std::int64_t>(n);
  out.constraints = static_cast<std::int64_t>(m);

  const DualBound dual_bound(inst);  // may throw std::length_error on huge instances
  const Quantity dual = dual_bound.upperBound(SimState(inst));
  if (m * (m + n) > kMaxLpTableau) {
    out.too_large = true;
    out.bound = dual;
    return out;
  }

  detail::LinearProgram lp;
  lp.rows = m;
  lp.cols = n;
  lp.a.assign(m * n, 0.0);
  lp.b.assign(m, 0.0);
  lp.c.assign(n, 0.0);
  for (std::size_t p = 0; p < P; ++p) {
    const double v = netValue(inst, inst.processes[p]);
    for (std::size_t j = first_var[p]; j < first_var[p + 1]; ++j) lp.c[j] = v;
  }
  std::size_t row = 0;
  for (const ResourceIndex r : consumed) {
    for (std::size_t tau = 0; tau < T; ++tau, ++row) {
      lp.b[row] = static_cast<double>(inst.start_inventory[r]);
      for (std::size_t p = 0; p < P; ++p) {
        const Process& proc = inst.processes[p];
        const auto d = static_cast<std::size_t>(proc.duration);
        double in = 0;
        double out_qty = 0;
        for (const ResourceAmount& a : proc.inputs) {
          if (a.resource == r) in = static_cast<double>(a.qty);
        }
        for (const ResourceAmount& a : proc.outputs) {
          if (a.resource == r) out_qty = static_cast<double>(a.qty);
        }
        for (std::size_t j = first_var[p]; j < first_var[p + 1]; ++j) {
          const std::size_t t = j - first_var[p];
          double coef = 0;
          if (t <= tau) coef += in;
          if (t + d <= tau) coef -= out_qty;
          lp.at(row, j) = coef;
        }
      }
    }
  }
  for (std::size_t tau = 0; tau < T; ++tau, ++row) {
    lp.b[row] = static_cast<double>(inst.workers);
    for (std::size_t p = 0; p < P; ++p) {
      const auto d = static_cast<std::size_t>(inst.processes[p].duration);
      for (std::size_t j = first_var[p]; j < first_var[p + 1]; ++j) {
        const std::size_t t = j - first_var[p];
        if (t <= tau && tau < t + d) lp.at(row, j) = 1;
      }
    }
  }
  for (const ProcessIndex p : capped) {
    lp.b[row] = static_cast<double>(inst.processes[p].max_runs.value_or(0));  // capped => set
    for (std::size_t j = first_var[p]; j < first_var[p + 1]; ++j) lp.at(row, j) = 1;
    ++row;
  }

  const detail::SimplexResult sr =
      detail::solveSimplex(lp, 200'000, [&] { return deadline.expired(); });
  out.iterations = sr.iterations;
  out.solved = sr.status == detail::SimplexResult::Status::Optimal;
  out.lp_objective = sr.objective;

  // Weak duality, made rigorous: for y >= 0,
  //   c.x = y.Ax + (c - A^T y).x <= y.b + sum_j max(0, c_j - (A^T y)_j) * W,
  // because the worker rows force every x_j <= W. So any y gives a valid bound.
  const double constant = static_cast<double>(inst.start_money) + [&] {
    double s = 0;
    for (std::size_t r = 0; r < inst.resources.size(); ++r) {
      s += static_cast<double>(inst.start_inventory[r] * inst.resources[r].sell_price);
    }
    return s;
  }();
  double bound = constant;
  for (std::size_t i = 0; i < m; ++i) bound += sr.y[i] * lp.b[i];
  for (std::size_t j = 0; j < n; ++j) {
    double aty = 0;
    for (std::size_t i = 0; i < m; ++i) aty += lp.at(i, j) * sr.y[i];
    bound += std::max(0.0, lp.c[j] - aty) * static_cast<double>(inst.workers);
  }
  const double tolerance = 1e-6 * std::max(1.0, std::abs(bound));
  const auto lp_bound = static_cast<Quantity>(std::floor(bound + tolerance));
  out.bound = std::min(lp_bound, dual);
  out.lp_objective += constant;

  out.x.resize(P);
  for (std::size_t p = 0; p < P; ++p) {
    out.x[p].assign(sr.x.begin() + static_cast<std::ptrdiff_t>(first_var[p]),
                    sr.x.begin() + static_cast<std::ptrdiff_t>(first_var[p + 1]));
  }
  return out;
}

namespace {

/// Follows the LP plan tick by tick; optionally fills idle workers with greedy choices.
Rollout followLp(const Instance& inst, const LpRelaxation& lp, bool greedy_fill,
                 const PotentialValues& potential) {
  Rollout out;
  SimState sim(inst);
  std::vector<double> pending(inst.processes.size(), 0.0);
  for (Tick t = 0; t < inst.horizon; ++t) {
    sim.advanceTo(t);
    for (ProcessIndex p = 0; p < inst.processes.size(); ++p) {
      const auto ti = static_cast<std::size_t>(t);
      if (ti < lp.x[p].size()) pending[p] += lp.x[p][ti];
    }
    while (true) {
      std::optional<ProcessIndex> best;
      for (ProcessIndex p = 0; p < inst.processes.size(); ++p) {
        if (pending[p] < 0.5 || !sim.canStart(p)) continue;
        if (!best || pending[p] > pending[*best]) best = p;
      }
      if (!best) break;
      pending[*best] -= 1;
      out.runs.push_back({*best, t});
      sim.start(*best);
    }
    if (greedy_fill) {
      while (const auto choice =
                 greedyChoice(sim, GreedyScore::PotentialValuePerTick, &potential)) {
        out.runs.push_back({*choice, t});
        sim.start(*choice);
      }
    }
  }
  sim.advanceTo(inst.horizon);
  out.earnings = sim.inventoryValue();
  return out;
}

}  // namespace

RawSolution LPRelaxationSolver::solveRaw(const Instance& instance, const SolveOptions& /*options*/,
                                         const Deadline& deadline, ProgressReporter& progress) {
  RawSolution result;
  const PotentialValues potential(instance);
  Rollout best = greedyRollout(SimState(instance), GreedyScore::PotentialValuePerTick, &potential);
  std::int64_t choice = 0;  // 0 = greedy, 1 = LP rounding, 2 = LP rounding + greedy fill
  progress.improved(best.earnings);

  LpRelaxation lp;
  try {
    lp = solveLp(instance, deadline);
  } catch (const std::length_error&) {
    result.runs = std::move(best.runs);
    result.stats["lp_unavailable"] = 1;
    return result;
  }
  if (!lp.too_large) {
    for (const bool fill : {false, true}) {
      Rollout r = followLp(instance, lp, fill, potential);
      if (r.earnings > best.earnings) {
        best = std::move(r);
        choice = fill ? 2 : 1;
        progress.improved(best.earnings);
      }
    }
  }
  result.runs = std::move(best.runs);
  // The LP bound proves optimality when the rounded schedule reaches it.
  result.proven_optimal = best.earnings >= lp.bound;
  result.stats["lp_bound"] = lp.bound;
  result.stats["lp_objective_floor"] = static_cast<std::int64_t>(std::floor(lp.lp_objective));
  result.stats["lp_iterations"] = lp.iterations;
  result.stats["lp_solved"] = lp.solved ? 1 : 0;
  result.stats["lp_too_large"] = lp.too_large ? 1 : 0;
  result.stats["lp_variables"] = lp.variables;
  result.stats["lp_constraints"] = lp.constraints;
  result.stats["heuristic_choice"] = choice;
  return result;
}

}  // namespace pf
