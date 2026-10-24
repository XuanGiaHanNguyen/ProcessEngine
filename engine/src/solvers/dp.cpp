#include "pf/solvers/dp.hpp"

#include "pf/rules.hpp"

#include <limits>
#include <optional>
#include <utility>

#include "compact_state.hpp"

namespace pf {

namespace {

class DynamicProgram {
 public:
  DynamicProgram(const Instance& instance, std::int64_t max_states, const Deadline& deadline)
      : instance_(instance),
        rules_(instance),
        memo_(rules_.width()),
        max_states_(max_states),
        deadline_(deadline) {}

  [[nodiscard]] const detail::CompactRules& rules() const { return rules_; }

  /// Optimal earnings from an event state (`from` = 0), or nullopt if aborted.
  std::optional<Quantity> value(std::size_t depth, const std::int64_t* state) {
    const std::uint64_t hash = rules_.hash(state);
    if (const Entry* e = memo_.find(state, hash)) {
      ++hits_;
      return e->value;
    }
    if (++expansions_ % 1024 == 0 && deadline_.expired()) {
      timed_out_ = true;
      return std::nullopt;
    }
    Best best{std::numeric_limits<Quantity>::min(), {}};
    std::vector<ProcessIndex> starts;
    if (!bestStarts(depth, state, starts, best)) return std::nullopt;
    if (std::cmp_greater_equal(memo_.size(), max_states_)) {
      state_limit_hit_ = true;
      return std::nullopt;
    }
    const auto offset = static_cast<std::uint32_t>(decisions_.size());
    decisions_.insert(decisions_.end(), best.starts.begin(), best.starts.end());
    memo_.insert(state, hash, {best.value, offset, static_cast<std::uint32_t>(best.starts.size())});
    return best.value;
  }

  /// The optimal schedule from `root`, read back from the memo table.
  Schedule reconstruct(const std::int64_t* root) {
    Schedule schedule;
    std::vector<std::int64_t> state(root, root + rules_.width());
    while (true) {
      const Entry* e = memo_.find(state.data(), rules_.hash(state.data()));
      if (e == nullptr) break;
      const Tick tick = detail::CompactRules::tick(state.data());
      for (std::uint32_t i = 0; i < e->count; ++i) {
        const ProcessIndex p = decisions_[e->offset + i];
        schedule.push_back({p, tick});
        rules_.start(state.data(), p);
      }
      const auto next = rules_.nextEvent(state.data());
      if (!next) break;
      rules_.advanceTo(state.data(), *next);
    }
    return schedule;
  }

  [[nodiscard]] std::int64_t states() const { return static_cast<std::int64_t>(memo_.size()); }
  [[nodiscard]] std::int64_t hits() const { return hits_; }
  [[nodiscard]] bool stateLimitHit() const { return state_limit_hit_; }
  [[nodiscard]] bool timedOut() const { return timed_out_; }

 private:
  struct Entry {
    Quantity value;
    std::uint32_t offset;  ///< the optimal multiset of starts, in decisions_
    std::uint32_t count;
  };
  struct Best {
    Quantity value;
    std::vector<ProcessIndex> starts;
  };

  /// Scratch buffer for depth `depth` (pool allocation, reused across the search).
  std::int64_t* scratch(std::size_t depth, const std::int64_t* copy_of) {
    if (scratch_.size() <= depth) scratch_.resize(depth + 1);
    scratch_[depth].assign(copy_of, copy_of + rules_.width());
    return scratch_[depth].data();
  }

  /// Enumerates start multisets (non-decreasing process index, from the state's `from`) at
  /// the current tick; updates `best`. Returns false if aborted.
  bool bestStarts(std::size_t depth, const std::int64_t* state, std::vector<ProcessIndex>& starts,
                  Best& best) {
    // Option 1: start nothing more at this tick.
    Quantity v = 0;
    if (const auto next = rules_.nextEvent(state)) {
      std::int64_t* advanced = scratch(depth, state);
      rules_.advanceTo(advanced, *next);
      rules_.setFrom(advanced, 0);
      const auto sub = value(depth + 1, advanced);
      if (!sub) return false;
      v = *sub;
    } else {
      v = rules_.finalValue(state);
    }
    if (v > best.value) best = {v, starts};  // ties keep the earlier (fewer starts) option
    // Option 2: start one more process p >= from.
    for (ProcessIndex p = rules_.from(state); p < instance_.processes.size(); ++p) {
      if (!rules_.canStart(state, p)) continue;
      std::int64_t* child = scratch(depth, state);
      rules_.start(child, p);
      rules_.setFrom(child, p);
      starts.push_back(p);
      // Deeper calls only use deeper scratch buffers, so `child` stays intact until the
      // next sibling overwrites it.
      const bool ok = bestStarts(depth + 1, child, starts, best);
      starts.pop_back();
      if (!ok) return false;
    }
    return true;
  }

  const Instance& instance_;
  detail::CompactRules rules_;
  detail::FlatStateMap<Entry> memo_;
  std::vector<ProcessIndex> decisions_;
  std::vector<std::vector<std::int64_t>> scratch_;
  std::int64_t max_states_;
  const Deadline& deadline_;
  std::int64_t hits_ = 0;
  std::int64_t expansions_ = 0;
  bool state_limit_hit_ = false;
  bool timed_out_ = false;
};

}  // namespace

RawSolution DPSolver::solveRaw(const Instance& instance, const SolveOptions& options,
                               const Deadline& deadline, ProgressReporter& progress) {
  const auto max_states = static_cast<std::int64_t>(
      options.param("max_states", static_cast<double>(kDefaultMaxStates)));
  DynamicProgram dp(instance, max_states, deadline);
  std::vector<std::int64_t> root(dp.rules().width());
  dp.rules().initial(root.data());
  const std::optional<Quantity> best = dp.value(0, root.data());

  RawSolution result;
  result.stats["states"] = dp.states();
  result.stats["memo_hits"] = dp.hits();
  if (!best) {
    result.error = dp.stateLimitHit()
                       ? "DP state limit exceeded (" + std::to_string(max_states) + " states)"
                       : std::string("DP ran out of time before finishing");
    return result;
  }
  result.runs = dp.reconstruct(root.data());
  result.proven_optimal = true;
  progress.improved(*best, dp.states());
  return result;
}

}  // namespace pf
