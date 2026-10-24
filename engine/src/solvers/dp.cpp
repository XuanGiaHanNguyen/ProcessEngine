#include "pf/solvers/dp.hpp"

#include "pf/rules.hpp"

#include <limits>
#include <optional>
#include <unordered_map>
#include <utility>

#include "search_state.hpp"

namespace pf {

namespace {

class DynamicProgram {
 public:
  DynamicProgram(const Instance& instance, std::int64_t max_states, const Deadline& deadline)
      : instance_(instance),
        capped_(detail::cappedProcesses(instance)),
        max_states_(max_states),
        deadline_(deadline) {}

  /// Optimal earnings from an event state, or nullopt if the search was aborted.
  std::optional<Quantity> value(const SimState& state) {
    detail::StateKey key = detail::makeKey(state, capped_);
    if (const auto it = memo_.find(key); it != memo_.end()) {
      ++hits_;
      return it->second.value;
    }
    if (++expansions_ % 1024 == 0 && deadline_.expired()) {
      timed_out_ = true;
      return std::nullopt;
    }
    Entry best{std::numeric_limits<Quantity>::min(), {}};
    std::vector<ProcessIndex> starts;
    if (!bestStarts(state, 0, starts, best)) return std::nullopt;
    if (std::cmp_greater_equal(memo_.size(), max_states_)) {
      state_limit_hit_ = true;
      return std::nullopt;
    }
    memo_.emplace(std::move(key), best);
    return best.value;
  }

  /// The optimal schedule from tick 0, read back from the memo table.
  Schedule reconstruct(SimState state) const {
    Schedule schedule;
    while (true) {
      const auto it = memo_.find(detail::makeKey(state, capped_));
      if (it == memo_.end()) break;
      for (const ProcessIndex p : it->second.starts) {
        schedule.push_back({p, state.tick()});
        state.start(p);
      }
      const auto next = state.nextEventTick();
      if (!next) break;
      state.advanceTo(*next);
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
    std::vector<ProcessIndex> starts;  ///< the optimal multiset of starts at this tick
  };

  /// Enumerates start multisets (non-decreasing process index, from `from`) at the current
  /// tick; updates `best` with the best (value, starts). Returns false if aborted.
  bool bestStarts(const SimState& state, ProcessIndex from, std::vector<ProcessIndex>& starts,
                  Entry& best) {
    // Option 1: start nothing more at this tick.
    std::optional<Quantity> v;
    if (const auto next = state.nextEventTick()) {
      SimState advanced = state;
      advanced.advanceTo(*next);
      v = value(advanced);
      if (!v) return false;
    } else {
      SimState done = state;
      done.advanceTo(instance_.horizon);
      v = done.inventoryValue();
    }
    if (*v > best.value) best = {*v, starts};  // ties keep the earlier (fewer starts) option
    // Option 2: start one more process p >= from.
    for (ProcessIndex p = from; p < instance_.processes.size(); ++p) {
      if (!state.canStart(p)) continue;
      SimState child = state;
      child.start(p);
      starts.push_back(p);
      const bool ok = bestStarts(child, p, starts, best);
      starts.pop_back();
      if (!ok) return false;
    }
    return true;
  }

  const Instance& instance_;
  std::vector<ProcessIndex> capped_;
  std::int64_t max_states_;
  const Deadline& deadline_;
  std::unordered_map<detail::StateKey, Entry, detail::StateKeyHash> memo_;
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
  const SimState root(instance);
  const std::optional<Quantity> best = dp.value(root);

  RawSolution result;
  result.stats["states"] = dp.states();
  result.stats["memo_hits"] = dp.hits();
  if (!best) {
    result.error = dp.stateLimitHit()
                       ? "DP state limit exceeded (" + std::to_string(max_states) + " states)"
                       : std::string("DP ran out of time before finishing");
    return result;
  }
  result.runs = dp.reconstruct(root);
  result.proven_optimal = true;
  progress.improved(*best, dp.states());
  return result;
}

}  // namespace pf
