#include "pf/solvers/brute_force.hpp"

#include "pf/rules.hpp"

#include <optional>

namespace pf {

namespace {

class Search {
 public:
  Search(const Instance& instance, std::int64_t max_nodes, const Deadline& deadline,
         ProgressReporter& progress)
      : instance_(instance), max_nodes_(max_nodes), deadline_(deadline), progress_(progress) {}

  /// Explores every schedule reachable from `state`; `from` is the lowest process index that
  /// may still be started at the current tick.
  void dfs(const SimState& state, ProcessIndex from) {
    if (stopped_) return;
    if (++nodes_ > max_nodes_) {
      stopped_ = true;
      node_limit_hit_ = true;
      return;
    }
    if (nodes_ % 4096 == 0 && deadline_.expired()) {
      stopped_ = true;
      return;
    }

    bool any_legal = false;
    for (ProcessIndex p = 0; p < instance_.processes.size(); ++p) {
      if (state.checkStart(p)) continue;
      any_legal = true;
      if (p < from) continue;
      SimState child = state;
      child.start(p);
      current_.push_back({p, state.tick()});
      dfs(child, p);
      current_.pop_back();
    }

    // Start nothing more at this tick.
    std::optional<Tick> next;
    if (any_legal && state.tick() < instance_.horizon) {
      next = state.tick() + 1;
    } else {
      next = state.nextEventTick();
    }
    SimState child = state;
    if (next) {
      child.advanceTo(*next);
      dfs(child, 0);
    } else {
      child.advanceTo(instance_.horizon);
      leaf(child.inventoryValue());
    }
  }

  [[nodiscard]] std::int64_t nodes() const { return nodes_; }
  [[nodiscard]] bool completed() const { return !stopped_; }
  [[nodiscard]] bool nodeLimitHit() const { return node_limit_hit_; }
  [[nodiscard]] const Schedule& best() const { return best_; }

 private:
  void leaf(Quantity value) {
    if (best_value_ && value <= *best_value_) return;
    best_value_ = value;
    best_ = current_;
    progress_.improved(value, nodes_);
  }

  const Instance& instance_;
  std::int64_t max_nodes_;
  const Deadline& deadline_;
  ProgressReporter& progress_;
  std::int64_t nodes_ = 0;
  bool stopped_ = false;
  bool node_limit_hit_ = false;
  Schedule current_;
  Schedule best_;
  std::optional<Quantity> best_value_;
};

}  // namespace

RawSolution BruteForceSolver::solveRaw(const Instance& instance, const SolveOptions& options,
                                       const Deadline& deadline, ProgressReporter& progress) {
  RawSolution result;
  if (instance.processes.size() > kMaxProcesses || instance.horizon > kMaxHorizon) {
    result.error = "instance too large for brute force (limit: " + std::to_string(kMaxProcesses) +
                   " processes, horizon " + std::to_string(kMaxHorizon) + ")";
    return result;
  }
  const auto max_nodes =
      static_cast<std::int64_t>(options.param("max_nodes", static_cast<double>(kDefaultMaxNodes)));

  Search search(instance, max_nodes, deadline, progress);
  search.dfs(SimState(instance), 0);

  result.stats["nodes"] = search.nodes();
  if (search.nodeLimitHit()) {
    result.error = "brute force exceeded its node limit (" + std::to_string(max_nodes) + ")";
    return result;
  }
  result.runs = search.best();
  result.proven_optimal = search.completed();
  return result;
}

}  // namespace pf
