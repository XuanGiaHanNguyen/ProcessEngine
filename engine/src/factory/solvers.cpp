#include "pf/factory/solvers.hpp"

#include "pf/factory/sim.hpp"
#include "pf/random.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <numeric>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "../solvers/simplex.hpp"

namespace pf::factory {

namespace {

constexpr Cents kInvalid = std::numeric_limits<Cents>::min();

/// Every (machine type, recipe) a design may build.
std::vector<MachineChoice> allChoices(const Level& level) {
  std::vector<MachineChoice> out;
  for (Index t = 0; t < level.machine_types.size(); ++t) {
    for (Index r = 0; r < level.machine_types[t].recipes.size(); ++r) out.emplace_back(t, r);
  }
  return out;
}

const Recipe& recipe(const Level& level, const MachineChoice& c) {
  return level.machine_types[c.first].recipes[c.second];
}

bool takes(const std::vector<Amount>& amounts, Index r) {
  return std::any_of(amounts.begin(), amounts.end(),
                     [&](const Amount& a) { return a.resource == r; });
}

/// Distance of each resource from the suppliers (raw = 0); unreachable = large.
std::vector<int> resourceDepths(const Level& level) {
  constexpr int kFar = 1000;
  std::vector<int> depth(level.resources.size(), kFar);
  for (const Supplier& s : level.suppliers) depth[s.resource] = 0;
  for (std::size_t pass = 0; pass < level.resources.size(); ++pass) {
    for (const MachineChoice& c : allChoices(level)) {
      const Recipe& rec = recipe(level, c);
      int d = 0;
      for (const Amount& a : rec.inputs) d = std::max(d, depth[a.resource]);
      for (const Amount& a : rec.outputs) depth[a.resource] = std::min(depth[a.resource], d + 1);
    }
  }
  return depth;
}

int choiceDepth(const Level& level, const std::vector<int>& depths, const MachineChoice& c) {
  int d = 0;
  for (const Amount& a : recipe(level, c).inputs) d = std::max(d, depths[a.resource]);
  return d;
}

/// Shared state of one solve: the level, deadline, and an evaluation counter.
class Search {
 public:
  Search(const Level& level, const SolveOptions& options, ProgressSink* sink)
      : level_(level),
        deadline_(options),
        sink_(sink),
        depths_(resourceDepths(level)),
        choices_(allChoices(level)) {}

  [[nodiscard]] const Level& level() const { return level_; }
  [[nodiscard]] bool expired() const { return deadline_.expired(); }
  [[nodiscard]] const std::vector<MachineChoice>& choices() const { return choices_; }
  [[nodiscard]] std::int64_t evaluations() const { return evaluations_; }

  /// Profit of building `machines` with `orders`, or kInvalid if it breaks slots or budget.
  Cents evaluate(const std::vector<MachineChoice>& machines, const std::vector<int>& orders) {
    if (machines.size() > level_.slots.size()) return kInvalid;
    Cents build = 0;
    for (const MachineChoice& c : machines) build += level_.machine_types[c.first].cost;
    if (level_.budget && build > *level_.budget) return kInvalid;
    ++evaluations_;
    const Cents profit = simulate(level_, autoWiredDesign(level_, machines, orders), false).profit;
    if (profit > best_) {
      best_ = profit;
      if (sink_) {
        ProgressEvent e;
        e.kind = ProgressEvent::Kind::Improved;
        e.elapsed_ms = deadline_.elapsedMs();
        e.best_earnings = profit;
        e.nodes = evaluations_;
        sink_->onProgress(e);
      }
    }
    return profit;
  }

  /// Each supplier's order: what the machines consume per hour, rounded up.
  [[nodiscard]] std::vector<int> neededOrders(const std::vector<MachineChoice>& machines) const {
    std::vector<double> need(level_.resources.size(), 0.0);
    for (const MachineChoice& c : machines) {
      const Recipe& rec = recipe(level_, c);
      for (const Amount& a : rec.inputs) need[a.resource] += static_cast<double>(a.qty) / rec.hours;
    }
    std::vector<int> orders(level_.suppliers.size(), 0);
    for (Index s = 0; s < level_.suppliers.size(); ++s) {
      const Index r = level_.suppliers[s].resource;
      orders[s] =
          std::min(level_.suppliers[s].max_rate, static_cast<int>(std::ceil(need[r] - 1e-9)));
      need[r] = 0;  // a second supplier of the same resource starts at 0
    }
    return orders;
  }

  /// Coordinate descent on the orders (steps of 1 and 4). Returns the tuned profit.
  Cents tuneOrders(const std::vector<MachineChoice>& machines, std::vector<int>& orders) {
    Cents best = evaluate(machines, orders);
    if (best == kInvalid) return best;
    bool improved = true;
    while (improved && !expired()) {
      improved = false;
      for (Index s = 0; s < orders.size(); ++s) {
        for (const int step : {-4, -1, 1, 4}) {
          const int v = orders[s] + step;
          if (v < 0 || v > level_.suppliers[s].max_rate) continue;
          const int old = orders[s];
          orders[s] = v;
          const Cents p = evaluate(machines, orders);
          if (p > best) {
            best = p;
            improved = true;
          } else {
            orders[s] = old;
          }
        }
      }
    }
    return best;
  }

  /// `machines` plus one `choice` and, recursively, a producer for every input that neither
  /// a supplier nor an existing machine provides.
  [[nodiscard]] std::vector<MachineChoice> withChain(std::vector<MachineChoice> machines,
                                                     const MachineChoice& choice) const {
    std::vector<MachineChoice> todo{choice};
    while (!todo.empty() && machines.size() <= level_.slots.size()) {
      const MachineChoice c = todo.back();
      todo.pop_back();
      machines.push_back(c);
      for (const Amount& a : recipe(level_, c).inputs) {
        if (depths_[a.resource] == 0 || produced(machines, a.resource) ||
            std::any_of(todo.begin(), todo.end(), [&](const MachineChoice& t) {
              return takes(recipe(level_, t).outputs, a.resource);
            })) {
          continue;
        }
        std::optional<MachineChoice> producer;
        for (const MachineChoice& p : choices_) {
          if (!takes(recipe(level_, p).outputs, a.resource)) continue;
          if (!producer ||
              choiceDepth(level_, depths_, p) < choiceDepth(level_, depths_, *producer)) {
            producer = p;
          }
        }
        if (producer) todo.push_back(*producer);
      }
    }
    return machines;
  }

  /// Removes machines one at a time while that raises profit (e.g. a machine left idle).
  Cents prune(std::vector<MachineChoice>& machines, std::vector<int>& orders) {
    Cents current = tuneOrders(machines, orders);
    bool improved = true;
    while (improved && !machines.empty()) {
      improved = false;
      for (Index i = 0; i < machines.size(); ++i) {
        std::vector<MachineChoice> m = machines;
        m.erase(m.begin() + static_cast<std::ptrdiff_t>(i));
        std::vector<int> o = orders;
        const Cents p = tuneOrders(m, o);
        if (p != kInvalid && p > current) {
          machines = std::move(m);
          orders = std::move(o);
          current = p;
          improved = true;
          break;
        }
      }
    }
    return current;
  }

  /// Greedy construction: repeatedly add the chain that raises the tuned profit most.
  Cents greedy(std::vector<MachineChoice>& machines, std::vector<int>& orders) {
    Cents current = tuneOrders(machines, orders);
    if (current == kInvalid) current = 0;
    while (!expired()) {
      Cents best = current;
      std::vector<MachineChoice> best_m;
      std::vector<int> best_o;
      for (const MachineChoice& c : choices_) {
        if (expired()) break;
        std::vector<MachineChoice> m = withChain(machines, c);
        std::vector<int> o = neededOrders(m);
        const Cents p = tuneOrders(m, o);
        if (p != kInvalid && p > best) {
          best = p;
          best_m = std::move(m);
          best_o = std::move(o);
        }
      }
      if (best_m.empty()) break;
      machines = std::move(best_m);
      orders = std::move(best_o);
      current = best;
    }
    return prune(machines, orders);
  }

 private:
  [[nodiscard]] bool produced(const std::vector<MachineChoice>& machines, Index r) const {
    return std::any_of(machines.begin(), machines.end(),
                       [&](const MachineChoice& m) { return takes(recipe(level_, m).outputs, r); });
  }

  const Level& level_;
  Deadline deadline_;
  ProgressSink* sink_;
  std::vector<int> depths_;
  std::vector<MachineChoice> choices_;
  std::int64_t evaluations_ = 0;
  Cents best_ = kInvalid;
};

class GreedySolver final : public FactorySolver {
 public:
  [[nodiscard]] std::string name() const override { return "greedy"; }

 protected:
  RawFactorySolution solveRaw(const Level& level, const SolveOptions& options,
                              ProgressSink* sink) const override {
    Search search(level, options, sink);
    std::vector<MachineChoice> machines;
    std::vector<int> orders(level.suppliers.size(), 0);
    search.greedy(machines, orders);
    return {autoWiredDesign(level, machines, orders), {{"evaluations", search.evaluations()}}, {}};
  }
};

/// Simulated annealing over machine lists and orders, starting from the greedy design.
class LocalSearchSolver final : public FactorySolver {
 public:
  [[nodiscard]] std::string name() const override { return "local_search"; }

 protected:
  RawFactorySolution solveRaw(const Level& level, const SolveOptions& options,
                              ProgressSink* sink) const override {
    Search search(level, options, sink);
    Random rng(options.seed);
    const auto start = std::chrono::steady_clock::now();
    auto elapsedFraction = [&] {
      const double ms =
          std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
              .count();
      return std::min(1.0, ms / std::max(1.0, options.time_limit_ms));
    };

    std::vector<MachineChoice> cur;
    std::vector<int> cur_o(level.suppliers.size(), 0);
    Cents cur_p = search.greedy(cur, cur_o);
    std::vector<MachineChoice> best = cur;
    std::vector<int> best_o = cur_o;
    Cents best_p = cur_p;
    const auto& choices = search.choices();
    std::int64_t moves = 0;
    std::int64_t accepted = 0;

    while (!search.expired() && !choices.empty()) {
      ++moves;
      std::vector<MachineChoice> m = cur;
      std::vector<int> o = cur_o;
      const double roll = rng.unit();
      if (roll < 0.25) {
        m = search.withChain(m, choices[rng.uniform<std::size_t>(0, choices.size() - 1)]);
        const std::vector<int> need = search.neededOrders(m);
        for (Index s = 0; s < o.size(); ++s) o[s] = std::max(o[s], need[s]);
      } else if (roll < 0.45 && !m.empty()) {
        m.erase(m.begin() + static_cast<std::ptrdiff_t>(rng.uniform<std::size_t>(0, m.size() - 1)));
      } else if (roll < 0.6 && !m.empty()) {
        m[rng.uniform<std::size_t>(0, m.size() - 1)] =
            choices[rng.uniform<std::size_t>(0, choices.size() - 1)];
      } else if (!o.empty()) {
        const auto s = rng.uniform<std::size_t>(0, o.size() - 1);
        o[s] = std::clamp(o[s] + rng.uniform<int>(-3, 3), 0, level.suppliers[s].max_rate);
      }
      const Cents p = search.evaluate(m, o);
      if (p == kInvalid) continue;
      const double temperature = 3000.0 * (1.0 - elapsedFraction()) + 1.0;
      const auto delta = static_cast<double>(p - cur_p);
      if (delta >= 0 || rng.unit() < std::exp(delta / temperature)) {
        ++accepted;
        cur = std::move(m);
        cur_o = std::move(o);
        cur_p = p;
        if (cur_p > best_p) {
          best_p = search.tuneOrders(cur, cur_o);
          best = cur;
          best_o = cur_o;
          cur_p = best_p;
        }
      }
    }
    search.prune(best, best_o);
    return {autoWiredDesign(level, best, best_o),
            {{"evaluations", search.evaluations()}, {"moves", moves}, {"accepted", accepted}},
            {}};
  }
};

/// Linear program over totals; see flowUpperBound(). Column layout of the LP.
struct FlowLp {
  detail::LinearProgram lp;
  std::vector<MachineChoice> choices;
  std::size_t x0 = 0, n0 = 0, o0 = 0, f0 = 0, v0 = 0;
};

FlowLp buildFlowLp(const Level& level) {
  FlowLp f;
  f.choices = allChoices(level);
  const std::size_t j = f.choices.size();
  const std::size_t s = level.suppliers.size();
  const std::size_t l = level.listings.size();
  f.x0 = 0;
  f.n0 = j;
  f.o0 = 2 * j;
  f.f0 = 2 * j + s;
  f.v0 = 2 * j + s + l;
  auto& lp = f.lp;
  lp.cols = 2 * j + s + 2 * l;
  lp.rows = level.resources.size() + j + 1 + (level.budget ? 1 : 0) + s + l;
  lp.a.assign(lp.rows * lp.cols, 0.0);
  lp.b.assign(lp.rows, 0.0);
  lp.c.assign(lp.cols, 0.0);
  const double hours = level.hours;
  const double active = std::max(0, level.hours - 1);  // nothing arrives before hour 1

  std::size_t row = 0;
  // Conservation: consumed + sold <= bought + produced, per resource.
  for (Index r = 0; r < level.resources.size(); ++r, ++row) {
    for (std::size_t k = 0; k < j; ++k) {
      const Recipe& rec = recipe(level, f.choices[k]);
      for (const Amount& a : rec.inputs) {
        if (a.resource == r) lp.at(row, f.x0 + k) += a.qty;
      }
      for (const Amount& a : rec.outputs) {
        if (a.resource == r) lp.at(row, f.x0 + k) -= a.qty;
      }
    }
    for (std::size_t k = 0; k < s; ++k) {
      if (level.suppliers[k].resource == r) lp.at(row, f.o0 + k) = -hours;
    }
    for (std::size_t k = 0; k < l; ++k) {
      if (level.listings[k].resource == r) {
        lp.at(row, f.f0 + k) = 1;
        lp.at(row, f.v0 + k) = 1;
      }
    }
  }
  // Batches per recipe are limited by its machines.
  for (std::size_t k = 0; k < j; ++k, ++row) {
    lp.at(row, f.x0 + k) = 1;
    lp.at(row, f.n0 + k) = -std::floor(active / recipe(level, f.choices[k]).hours);
  }
  for (std::size_t k = 0; k < j; ++k) lp.at(row, f.n0 + k) = 1;
  lp.b[row++] = static_cast<double>(level.slots.size());
  if (level.budget) {
    for (std::size_t k = 0; k < j; ++k) {
      lp.at(row, f.n0 + k) = static_cast<double>(level.machine_types[f.choices[k].first].cost);
    }
    lp.b[row++] = static_cast<double>(*level.budget);
  }
  for (std::size_t k = 0; k < s; ++k, ++row) {
    lp.at(row, f.o0 + k) = 1;
    lp.b[row] = level.suppliers[k].max_rate;
  }
  for (std::size_t k = 0; k < l; ++k, ++row) {
    lp.at(row, f.f0 + k) = 1;
    lp.b[row] = level.listings[k].demand * active;
  }

  for (std::size_t k = 0; k < j; ++k) {
    lp.c[f.n0 + k] = -static_cast<double>(level.machine_types[f.choices[k].first].cost);
  }
  for (std::size_t k = 0; k < s; ++k)
    lp.c[f.o0 + k] = -hours * static_cast<double>(level.suppliers[k].unit_cost);
  for (std::size_t k = 0; k < l; ++k) {
    lp.c[f.f0 + k] = static_cast<double>(level.listings[k].price);
    lp.c[f.v0 + k] = static_cast<double>(level.listings[k].overflow_price);
  }
  return f;
}

/// Solves the flow LP, rounds its machine counts, then improves the result greedily.
class FlowLpSolver final : public FactorySolver {
 public:
  [[nodiscard]] std::string name() const override { return "flow_lp"; }

 protected:
  RawFactorySolution solveRaw(const Level& level, const SolveOptions& options,
                              ProgressSink* sink) const override {
    Search search(level, options, sink);
    const FlowLp f = buildFlowLp(level);
    const auto res = detail::solveSimplex(f.lp, 200'000, [&] { return search.expired(); });
    std::vector<MachineChoice> machines;
    std::vector<int> orders(level.suppliers.size(), 0);
    if (res.status == detail::SimplexResult::Status::Optimal) {
      // Whole machines first, then the largest fractional parts while slots allow.
      std::vector<std::pair<double, MachineChoice>> fractions;
      for (std::size_t k = 0; k < f.choices.size(); ++k) {
        const double n = res.x[f.n0 + k];
        for (int i = 0; i < static_cast<int>(std::floor(n + 1e-6)); ++i)
          machines.push_back(f.choices[k]);
        const double frac = n - std::floor(n + 1e-6);
        if (frac > 0.05) fractions.emplace_back(frac, f.choices[k]);
      }
      std::sort(fractions.begin(), fractions.end(),
                [](const auto& a, const auto& b) { return a.first > b.first; });
      for (const auto& fr : fractions) {
        if (machines.size() < level.slots.size()) machines.push_back(fr.second);
      }
      while (machines.size() > level.slots.size()) machines.pop_back();
      for (Index s = 0; s < level.suppliers.size(); ++s) {
        orders[s] = std::min(level.suppliers[s].max_rate,
                             static_cast<int>(std::ceil(res.x[f.o0 + s] - 1e-6)));
      }
    }
    if (search.tuneOrders(machines, orders) == kInvalid) {
      machines.clear();
      orders.assign(level.suppliers.size(), 0);
    }
    search.greedy(machines, orders);
    return {autoWiredDesign(level, machines, orders),
            {{"evaluations", search.evaluations()},
             {"lp_iterations", res.iterations},
             {"lp_bound", static_cast<std::int64_t>(std::floor(res.objective + 1e-6))}},
            {}};
  }
};

}  // namespace

FactorySolution FactorySolver::solve(const Level& level, const SolveOptions& options,
                                     ProgressSink* sink) const {
  const auto start = std::chrono::steady_clock::now();
  RawFactorySolution raw = solveRaw(level, options, sink);
  FactorySolution out;
  out.solver_name = name();
  out.runtime_ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  out.stats = std::move(raw.stats);
  out.error = std::move(raw.error);
  out.design = std::move(raw.design);
  const auto issues = checkDesign(level, out.design);
  if (!issues.empty()) {
    out.valid = false;
    if (!out.error) out.error = "invalid design: " + issues.front().message;
    out.design = emptyDesign(level);
    out.profit = 0;
    return out;
  }
  out.valid = true;
  out.profit = simulate(level, out.design, false).profit;
  return out;
}

std::vector<FactorySolverInfo> factorySolvers() {
  return {
      {"greedy", "adds the production chain that raises profit most, then tunes orders"},
      {"local_search", "simulated annealing over machines and orders, from the greedy design"},
      {"flow_lp", "rounds a linear flow model's machine counts, then improves greedily"},
  };
}

std::unique_ptr<FactorySolver> makeFactorySolver(std::string_view name) {
  if (name == "greedy") return std::make_unique<GreedySolver>();
  if (name == "local_search") return std::make_unique<LocalSearchSolver>();
  if (name == "flow_lp") return std::make_unique<FlowLpSolver>();
  return nullptr;
}

FlowBound flowUpperBound(const Level& level, const SolveOptions& options) {
  const FlowLp f = buildFlowLp(level);
  const Deadline deadline(options);
  const auto res = detail::solveSimplex(f.lp, 200'000, [&] { return deadline.expired(); });
  FlowBound b;
  b.solved = res.status == detail::SimplexResult::Status::Optimal;
  // The empty design earns 0, so the optimum is never negative.
  b.bound = std::max<Cents>(0, static_cast<Cents>(std::floor(res.objective + 1e-6)));
  return b;
}

Design autoWiredDesign(const Level& level, std::vector<MachineChoice> machines,
                       const std::vector<int>& orders) {
  const std::vector<int> depths = resourceDepths(level);
  std::stable_sort(machines.begin(), machines.end(),
                   [&](const MachineChoice& a, const MachineChoice& b) {
                     return choiceDepth(level, depths, a) < choiceDepth(level, depths, b);
                   });
  Design d = emptyDesign(level);
  d.orders = orders;
  d.orders.resize(level.suppliers.size(), 0);
  for (Index i = 0; i < machines.size() && i < level.slots.size(); ++i) {
    d.machines.push_back({i, machines[i].first, machines[i].second});
  }
  for (Index r = 0; r < level.resources.size(); ++r) {
    std::vector<NodeRef> producers;
    for (Index s = 0; s < level.suppliers.size(); ++s) {
      if (level.suppliers[s].resource == r) producers.push_back({NodeKind::Supplier, s});
    }
    std::vector<NodeRef> consumers;
    for (const PlacedMachine& m : d.machines) {
      const Recipe& rec = recipeOf(level, m);
      if (takes(rec.outputs, r)) producers.push_back({NodeKind::Slot, m.slot});
      if (takes(rec.inputs, r)) consumers.push_back({NodeKind::Slot, m.slot});
    }
    if (level.listingFor(r) != nullptr) consumers.push_back({NodeKind::Market, 0});
    for (const NodeRef& p : producers) {
      for (const NodeRef& c : consumers) {
        if (!(p == c)) d.belts.push_back({p, c, r, 1});
      }
    }
  }
  return d;
}

}  // namespace pf::factory
