#include "pf/solvers/branch_and_bound.hpp"

#include "pf/solvers/greedy.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

#include "compact_state.hpp"

namespace pf {

class BranchAndBoundSearch::Impl {
 public:
  Impl(const BranchAndBoundSearch& owner, const SimState& root)
      : o_(owner), rules_(owner.instance_), done_(rules_.width()) {
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
    std::vector<std::int64_t> state(rules_.width());
    rules_.fromSim(root, state.data());
    result_.root_bound = rules_.upperBound(state.data(), o_.bound_);
    dfs(0, state.data(), result_.root_bound);
    result_.completed = !aborted_;
    return std::move(result_);
  }

 private:
  /// A child of the node being expanded; its state lives in the depth's pool buffer.
  struct Child {
    std::size_t offset;    ///< into Level::states
    std::int64_t started;  ///< process started, or -1 for "advance to next event"
    Quantity bound;
  };
  /// Per-depth scratch, reused across the whole search (pool allocation: after warm-up the
  /// search allocates only when the transposition table grows).
  struct Level {
    std::vector<std::int64_t> states;
    std::vector<Child> children;
  };

  void report() {
    if (o_.progress_ != nullptr) o_.progress_->improved(result_.earnings, result_.nodes);
  }

  std::int64_t* push(Level& level, const std::int64_t* from_state) {
    const std::size_t offset = level.states.size();
    level.states.insert(level.states.end(), from_state, from_state + rules_.width());
    return level.states.data() + offset;
  }

  /// Searches continuations of `state` (whose `from` field limits same-tick starts).
  /// `ub` is an upper bound for this subtree.
  void dfs(std::size_t depth, const std::int64_t* state, Quantity ub) {
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
    const std::uint64_t hash = rules_.hash(state);
    if (done_.find(state, hash) != nullptr) {
      ++result_.transposition_hits;
      return;
    }

    if (levels_.size() <= depth) levels_.resize(depth + 1);
    Level& level = levels_[depth];
    level.states.clear();
    level.children.clear();
    const Tick tick = detail::CompactRules::tick(state);
    if (const auto next = rules_.nextEvent(state)) {
      const std::size_t offset = level.states.size();
      std::int64_t* child = push(level, state);
      rules_.advanceTo(child, *next);
      rules_.setFrom(child, 0);
      level.children.push_back({offset, -1, rules_.upperBound(child, o_.bound_)});
    } else {
      leaf(state);  // nothing running: starting nothing more ends the game
    }
    for (ProcessIndex p = rules_.from(state); p < o_.instance_.processes.size(); ++p) {
      if (!rules_.canStart(state, p)) continue;
      const std::size_t offset = level.states.size();
      std::int64_t* child = push(level, state);
      rules_.start(child, p);
      rules_.setFrom(child, p);
      level.children.push_back(
          {offset, static_cast<std::int64_t>(p), rules_.upperBound(child, o_.bound_)});
    }
    // Best-first: most promising bound first; stable so ties keep a deterministic order.
    std::stable_sort(level.children.begin(), level.children.end(),
                     [](const Child& a, const Child& b) { return a.bound > b.bound; });

    // Index-based loop: deeper levels may grow `levels_`, but this level's buffers stay put.
    for (std::size_t i = 0; i < levels_[depth].children.size(); ++i) {
      if (aborted_) return;
      const Child child = levels_[depth].children[i];
      if (child.bound <= result_.earnings) {
        ++result_.pruned;
        continue;
      }
      const std::int64_t* child_state = levels_[depth].states.data() + child.offset;
      if (child.started >= 0) path_.push_back({static_cast<ProcessIndex>(child.started), tick});
      dfs(depth + 1, child_state, child.bound);
      if (child.started >= 0) path_.pop_back();
    }
    if (!aborted_ && std::cmp_less(done_.size(), o_.max_transpositions_)) {
      done_.insert(state, hash, {});
    }
  }

  void leaf(const std::int64_t* state) {
    const Quantity value = rules_.finalValue(state);
    if (value > result_.earnings) {
      result_.earnings = value;
      result_.runs = path_;
      report();
    }
  }

  struct Empty {};

  const BranchAndBoundSearch& o_;
  detail::CompactRules rules_;
  SearchResult result_;
  bool has_incumbent_ = false;
  bool aborted_ = false;
  Schedule path_;
  std::vector<Level> levels_;
  detail::FlatStateMap<Empty> done_;
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
