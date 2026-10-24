#pragma once

// Internal helpers shared by the exact search solvers (not a public header).

#include "pf/model.hpp"
#include "pf/rules.hpp"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace pf::detail {

/// Canonical encoding of everything that determines a state's future: tick, inventory,
/// runs in progress as sorted (end, process) pairs, and run counts of processes that have
/// a max_runs cap (counts of uncapped processes do not matter).
using StateKey = std::vector<std::int64_t>;

/// Processes whose run counts belong in the key.
inline std::vector<ProcessIndex> cappedProcesses(const Instance& instance) {
  std::vector<ProcessIndex> out;
  for (ProcessIndex p = 0; p < instance.processes.size(); ++p) {
    if (instance.processes[p].max_runs) out.push_back(p);
  }
  return out;
}

inline StateKey makeKey(const SimState& state, const std::vector<ProcessIndex>& capped) {
  StateKey key;
  key.reserve(1 + state.inventory().size() + capped.size() + 2 * state.activeRuns().size());
  key.push_back(state.tick());
  key.insert(key.end(), state.inventory().begin(), state.inventory().end());
  for (const ProcessIndex p : capped) key.push_back(state.runCounts()[p]);
  // activeRuns() is sorted by (end, start, process); drop start and re-sort by (end, process).
  const std::size_t first_run = key.size();
  for (const ActiveRun& run : state.activeRuns()) {
    key.push_back((static_cast<std::int64_t>(run.end) << 32) |
                  static_cast<std::int64_t>(run.process));
  }
  std::sort(key.begin() + static_cast<std::ptrdiff_t>(first_run), key.end());
  return key;
}

/// 64-bit mix (splitmix64 finalizer) combined over the key.
struct StateKeyHash {
  std::size_t operator()(const StateKey& key) const noexcept {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL ^ key.size();
    for (const std::int64_t v : key) {
      std::uint64_t x = static_cast<std::uint64_t>(v) + 0x9E3779B97F4A7C15ULL + (h << 6) + (h >> 2);
      x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
      x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
      h ^= x ^ (x >> 31);
    }
    return static_cast<std::size_t>(h);
  }
};

}  // namespace pf::detail
