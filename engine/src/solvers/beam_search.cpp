#include "pf/solvers/beam_search.hpp"

#include "pf/rules.hpp"
#include "pf/solvers/greedy.hpp"

#include <algorithm>
#include <unordered_set>

#include "search_state.hpp"

namespace pf {

namespace {

struct Node {
  SimState state;
  ProcessIndex from = 0;
  Schedule path;
  Quantity score = 0;
};

}  // namespace

RawSolution BeamSearchSolver::solveRaw(const Instance& instance, const SolveOptions& options,
                                       const Deadline& deadline, ProgressReporter& progress) {
  const auto width =
      static_cast<std::size_t>(std::max(1.0, options.param("beam_width", kDefaultWidth)));
  const PotentialValues potential(instance);
  const auto capped = detail::cappedProcesses(instance);

  Rollout best = greedyRollout(SimState(instance), GreedyScore::PotentialValuePerTick, &potential);
  progress.improved(best.earnings);
  std::int64_t levels = 0;
  std::int64_t nodes = 0;
  std::int64_t rollouts = 1;

  // Scores a child with a rollout, keeping the rollout if it is the best schedule so far.
  auto evaluate = [&](Node& child) {
    Rollout r = greedyRollout(child.state, GreedyScore::PotentialValuePerTick, &potential);
    ++rollouts;
    child.score = r.earnings;
    if (r.earnings > best.earnings) {
      best.earnings = r.earnings;
      best.runs = child.path;
      best.runs.insert(best.runs.end(), r.runs.begin(), r.runs.end());
      progress.improved(best.earnings, nodes);
    }
  };

  std::vector<Node> beam;
  beam.push_back({SimState(instance), 0, {}, best.earnings});
  while (!beam.empty() && !deadline.expired()) {
    ++levels;
    std::vector<Node> children;
    std::unordered_set<detail::StateKey, detail::StateKeyHash> seen;
    auto add = [&](Node child) {
      detail::StateKey key = detail::makeKey(child.state, capped);
      key.push_back(static_cast<std::int64_t>(child.from));
      if (!seen.insert(std::move(key)).second) return;
      ++nodes;
      evaluate(child);
      children.push_back(std::move(child));
    };
    for (const Node& node : beam) {
      if (deadline.expired()) break;
      if (const auto next = node.state.nextEventTick()) {
        Node child{node.state, 0, node.path, 0};
        child.state.advanceTo(*next);
        add(std::move(child));
      }
      // (With nothing running, "advance" would end the game; that leaf is covered by the
      // rollout that scored this node.)
      for (ProcessIndex p = node.from; p < instance.processes.size(); ++p) {
        if (node.state.checkStart(p)) continue;
        Node child{node.state, p, node.path, 0};
        child.state.start(p);
        child.path.push_back({p, node.state.tick()});
        add(std::move(child));
      }
    }
    std::stable_sort(children.begin(), children.end(),
                     [](const Node& a, const Node& b) { return a.score > b.score; });
    if (children.size() > width)
      children.erase(children.begin() + static_cast<std::ptrdiff_t>(width), children.end());
    beam = std::move(children);
  }

  RawSolution result;
  result.runs = std::move(best.runs);
  result.stats["levels"] = levels;
  result.stats["nodes"] = nodes;
  result.stats["rollouts"] = rollouts;
  return result;
}

}  // namespace pf
