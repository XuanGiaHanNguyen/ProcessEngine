#include "pf/solvers/branch_and_bound.hpp"

#include "pf/solvers/greedy.hpp"

#include <algorithm>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "search_state.hpp"

namespace pf {

class BranchAndBoundSearch::Impl {
 public:
  Impl(const BranchAndBoundSearch& owner, const SimState& root)
      : o_(owner), capped_(detail::cappedProcesses(owner.instance_)) {
    // Incumbent: the better greedy rollout from the root.
    const PotentialValues potential(o_.instance_);
    for (const GreedyScore score :
         {GreedyScore::PotentialValuePerTick, GreedyScore::MarketValuePerTick}) {
      Rollout r = greedyRollout(root, score, &potential);
      if (!has_incumbent_ || r.earnings > result_.earnings) {
        has_incumbent_ = true;
        result_.earnings = r.earnings;
        result_.runs = std::move(r.runs);
      }
    }
    report();
  }

  SearchResult run(const SimState& root) {
    result_.root_bound = o_.bound_.upperBound(root);
    dfs(root, 0, result_.root_bound);
    result_.completed = !aborted_;
    return std::move(result_);
  }

 private:
  struct Child {
    SimState state;
    std::optional<ProcessIndex> started;  ///< nullopt = the "advance to next event" child
    Quantity bound;
  };

  void report() {
    if (o_.progress_ != nullptr) o_.progress_->improved(result_.earnings, result_.nodes);
  }

  void leaf(const SimState& state) {
    SimState done = state;
    done.advanceTo(o_.instance_.horizon);
    const Quantity value = done.inventoryValue();
    if (value > result_.earnings) {
      result_.earnings = value;
      result_.runs = path_;
      report();
    }
  }

  /// Searches continuations of `state` in which further starts at this tick use process
  /// indices >= `from`. `ub` is an upper bound for this subtree.
  void dfs(const SimState& state, ProcessIndex from, Quantity ub) {
    if (aborted_) return;
    ++result_.nodes;
    if (result_.nodes % 1024 == 0) {
      if (o_.deadline_.expired()) {
        aborted_ = true;
        return;
      }
      if (o_.progress_ != nullptr && result_.nodes % 65536 == 0) {
        o_.progress_->heartbeat(result_.nodes);
      }
    }
    if (ub <= result_.earnings) {
      ++result_.pruned;
      return;
    }
    detail::StateKey key = detail::makeKey(state, capped_);
    key.push_back(static_cast<std::int64_t>(from));
    if (done_.contains(key)) {
      ++result_.transposition_hits;
      return;
    }

    std::vector<Child> children;
    if (const auto next = state.nextEventTick()) {
      SimState advanced = state;
      advanced.advanceTo(*next);
      const Quantity b = o_.bound_.upperBound(advanced);
      children.push_back({std::move(advanced), std::nullopt, b});
    } else {
      leaf(state);  // nothing running: starting nothing more ends the game
    }
    for (ProcessIndex p = from; p < o_.instance_.processes.size(); ++p) {
      if (state.checkStart(p)) continue;
      SimState started = state;
      started.start(p);
      const Quantity b = o_.bound_.upperBound(started);
      children.push_back({std::move(started), p, b});
    }
    // Best-first: most promising bound first; stable so ties keep a deterministic order.
    std::stable_sort(children.begin(), children.end(),
                     [](const Child& a, const Child& b) { return a.bound > b.bound; });

    for (Child& child : children) {
      if (aborted_) return;
      if (child.bound <= result_.earnings) {
        ++result_.pruned;
        continue;
      }
      if (child.started) {
        path_.push_back({*child.started, state.tick()});
        dfs(child.state, *child.started, child.bound);
        path_.pop_back();
      } else {
        dfs(child.state, 0, child.bound);
      }
    }
    if (!aborted_ && std::cmp_less(done_.size(), o_.max_transpositions_)) {
      done_.insert(std::move(key));
    }
  }

  const BranchAndBoundSearch& o_;
  std::vector<ProcessIndex> capped_;
  SearchResult result_;
  bool has_incumbent_ = false;
  bool aborted_ = false;
  Schedule path_;
  std::unordered_set<detail::StateKey, detail::StateKeyHash> done_;
};

BranchAndBoundSearch::BranchAndBoundSearch(const Instance& instance, const DualBound& bound,
                                           const Deadline& deadline, ProgressReporter* progress,
                                           std::int64_t max_transpositions)
    : instance_(instance),
      bound_(bound),
      deadline_(deadline),
      progress_(progress),
      max_transpositions_(max_transpositions) {}

SearchResult BranchAndBoundSearch::run(const SimState& root) { return Impl(*this, root).run(root); }

RawSolution BranchAndBoundSolver::solveRaw(const Instance& instance, const SolveOptions& options,
                                           const Deadline& deadline, ProgressReporter& progress) {
  RawSolution result;
  const auto max_transpositions = static_cast<std::int64_t>(
      options.param("max_transpositions", static_cast<double>(kDefaultMaxTranspositions)));
  std::optional<DualBound> bound;
  try {
    bound.emplace(instance);
  } catch (const std::length_error&) {
    // Too large for the bound tables: fall back to the greedy incumbent, unproven.
    const PotentialValues potential(instance);
    result.runs =
        greedyRollout(SimState(instance), GreedyScore::PotentialValuePerTick, &potential).runs;
    result.stats["bound_unavailable"] = 1;
    return result;
  }
  BranchAndBoundSearch search(instance, *bound, deadline, &progress, max_transpositions);
  SearchResult r = search.run(SimState(instance));
  result.runs = std::move(r.runs);
  result.proven_optimal = r.completed;
  result.stats["nodes"] = r.nodes;
  result.stats["pruned"] = r.pruned;
  result.stats["transposition_hits"] = r.transposition_hits;
  result.stats["root_bound"] = r.root_bound;
  return result;
}

}  // namespace pf
