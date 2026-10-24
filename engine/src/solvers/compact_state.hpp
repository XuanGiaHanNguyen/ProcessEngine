#pragma once

// Internal: a compact, fixed-width encoding of a search state for the exact solvers.
//
// A state is `width()` contiguous int64 values:
//   [0]                      tick
//   [1, 1+R)                 inventory
//   [1+R, 1+R+C)             run counts of the C processes that have max_runs
//   [1+R+C, 1+R+C+W)         runs in progress, packed as (end << 32 | process), sorted
//                            ascending, empty slots = kEmptySlot (so they sort last)
//   [width-1]                `from`: lowest process index that may still start this tick
// Because the layout is canonical (start ticks dropped, runs sorted), the state array is
// its own hash key. The rules mirror SimState; solver output is still validated by the
// independent validator, and compact_state_test cross-checks this against SimState.

#include "pf/bounds.hpp"
#include "pf/model.hpp"
#include "pf/rules.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace pf::detail {

inline constexpr std::int64_t kEmptySlot = std::numeric_limits<std::int64_t>::max();

inline std::int64_t packRun(Tick end, ProcessIndex process) {
  return static_cast<std::int64_t>((static_cast<std::uint64_t>(end) << 32) |
                                   static_cast<std::uint64_t>(process));
}
inline Tick runEnd(std::int64_t packed) { return static_cast<Tick>(packed >> 32); }
inline ProcessIndex runProcess(std::int64_t packed) {
  return static_cast<ProcessIndex>(static_cast<std::uint64_t>(packed) & 0xFFFFFFFFULL);
}

class CompactRules {
 public:
  explicit CompactRules(const Instance& instance)
      : inst_(instance),
        resources_(instance.resources.size()),
        workers_(static_cast<std::size_t>(instance.workers)),
        cap_index_(instance.processes.size(), -1) {
    for (ProcessIndex p = 0; p < instance.processes.size(); ++p) {
      if (instance.processes[p].max_runs) cap_index_[p] = static_cast<std::int32_t>(caps_++);
    }
    caps_off_ = 1 + resources_;
    runs_off_ = caps_off_ + caps_;
    from_off_ = runs_off_ + workers_;
    width_ = from_off_ + 1;
    runs_.reserve(workers_);
  }

  [[nodiscard]] std::size_t width() const { return width_; }
  [[nodiscard]] const Instance& instance() const { return inst_; }

  void initial(std::int64_t* s) const {
    s[0] = 0;
    for (std::size_t r = 0; r < resources_; ++r) s[1 + r] = inst_.start_inventory[r];
    std::fill(s + caps_off_, s + runs_off_, 0);
    std::fill(s + runs_off_, s + from_off_, kEmptySlot);
    s[from_off_] = 0;
  }

  void fromSim(const SimState& sim, std::int64_t* s) const {
    s[0] = sim.tick();
    for (std::size_t r = 0; r < resources_; ++r) s[1 + r] = sim.inventory()[r];
    for (ProcessIndex p = 0; p < cap_index_.size(); ++p) {
      if (cap_index_[p] >= 0)
        s[caps_off_ + static_cast<std::size_t>(cap_index_[p])] = sim.runCounts()[p];
    }
    std::fill(s + runs_off_, s + from_off_, kEmptySlot);
    std::size_t n = 0;
    for (const ActiveRun& run : sim.activeRuns())
      s[runs_off_ + n++] = packRun(run.end, run.process);
    std::sort(s + runs_off_, s + runs_off_ + n);
    s[from_off_] = 0;
  }

  [[nodiscard]] static Tick tick(const std::int64_t* s) { return static_cast<Tick>(s[0]); }
  [[nodiscard]] ProcessIndex from(const std::int64_t* s) const {
    return static_cast<ProcessIndex>(s[from_off_]);
  }
  void setFrom(std::int64_t* s, ProcessIndex from) const {
    s[from_off_] = static_cast<std::int64_t>(from);
  }
  [[nodiscard]] std::size_t activeCount(const std::int64_t* s) const {
    std::size_t n = 0;
    while (n < workers_ && s[runs_off_ + n] != kEmptySlot) ++n;
    return n;
  }

  [[nodiscard]] bool canStart(const std::int64_t* s, ProcessIndex p) const {
    const Process& proc = inst_.processes[p];
    if (s[0] + proc.duration > inst_.horizon) return false;
    if (cap_index_[p] >= 0 &&
        s[caps_off_ + static_cast<std::size_t>(cap_index_[p])] >= proc.max_runs.value_or(0)) {
      return false;
    }
    if (s[from_off_ - 1] != kEmptySlot) return false;  // last worker slot taken: all busy
    for (const ResourceAmount& in : proc.inputs) {
      if (s[1 + in.resource] < in.qty) return false;
    }
    return true;
  }

  void start(std::int64_t* s, ProcessIndex p) const {
    const Process& proc = inst_.processes[p];
    for (const ResourceAmount& in : proc.inputs) s[1 + in.resource] -= in.qty;
    if (cap_index_[p] >= 0) ++s[caps_off_ + static_cast<std::size_t>(cap_index_[p])];
    // Insert into the sorted slots (the last slot is free, checked by canStart).
    const std::int64_t packed = packRun(static_cast<Tick>(s[0]) + proc.duration, p);
    std::size_t i = from_off_ - 1;
    while (i > runs_off_ && s[i - 1] > packed) {
      s[i] = s[i - 1];
      --i;
    }
    s[i] = packed;
  }

  [[nodiscard]] std::optional<Tick> nextEvent(const std::int64_t* s) const {
    if (s[runs_off_] == kEmptySlot) return std::nullopt;
    return runEnd(s[runs_off_]);
  }

  void advanceTo(std::int64_t* s, Tick t) const {
    std::size_t done = 0;
    while (done < workers_ && s[runs_off_ + done] != kEmptySlot &&
           runEnd(s[runs_off_ + done]) <= t) {
      for (const ResourceAmount& out : inst_.processes[runProcess(s[runs_off_ + done])].outputs) {
        s[1 + out.resource] += out.qty;
      }
      ++done;
    }
    if (done > 0) {
      std::memmove(s + runs_off_, s + runs_off_ + done, (workers_ - done) * sizeof(std::int64_t));
      std::fill(s + from_off_ - done, s + from_off_, kEmptySlot);
    }
    s[0] = t;
  }

  /// Earnings if nothing more is started: running runs complete, everything is sold.
  [[nodiscard]] Quantity finalValue(const std::int64_t* s) const {
    Quantity total = inst_.start_money;
    auto add = [&](Quantity qty, Quantity price) {
      Quantity v = 0;
      if (__builtin_mul_overflow(qty, price, &v) || __builtin_add_overflow(total, v, &total)) {
        throw std::overflow_error("earnings do not fit in a 64-bit integer");
      }
    };
    for (std::size_t r = 0; r < resources_; ++r) add(s[1 + r], inst_.resources[r].sell_price);
    for (std::size_t i = runs_off_; i < from_off_ && s[i] != kEmptySlot; ++i) {
      for (const ResourceAmount& out : inst_.processes[runProcess(s[i])].outputs) {
        add(out.qty, inst_.resources[out.resource].sell_price);
      }
    }
    return total;
  }

  [[nodiscard]] Quantity upperBound(const std::int64_t* s, const DualBound& bound) const {
    runs_.clear();
    for (std::size_t i = runs_off_; i < from_off_ && s[i] != kEmptySlot; ++i) {
      runs_.push_back({runEnd(s[i]), runProcess(s[i])});
    }
    return bound.upperBound(tick(s), s + 1, runs_);
  }

  /// splitmix64-style hash over the whole state (including `from`).
  [[nodiscard]] std::uint64_t hash(const std::int64_t* s) const {
    std::uint64_t h = 0x9E3779B97F4A7C15ULL;
    for (std::size_t i = 0; i < width_; ++i) {
      std::uint64_t x =
          static_cast<std::uint64_t>(s[i]) + 0x9E3779B97F4A7C15ULL + (h << 6U) + (h >> 2U);
      x = (x ^ (x >> 30U)) * 0xBF58476D1CE4E5B9ULL;
      x = (x ^ (x >> 27U)) * 0x94D049BB133111EBULL;
      h ^= x ^ (x >> 31U);
    }
    return h;
  }

 private:
  const Instance& inst_;
  std::size_t resources_;
  std::size_t workers_;
  std::vector<std::int32_t> cap_index_;
  std::size_t caps_ = 0;
  std::size_t caps_off_ = 0;
  std::size_t runs_off_ = 0;
  std::size_t from_off_ = 0;
  std::size_t width_ = 0;
  mutable std::vector<RunRef> runs_;  // scratch for upperBound
};

/// Open-addressing hash map from fixed-width states to `Value`. Keys are copied into one
/// contiguous arena, so inserting allocates only when the arena or table grows.
template <typename Value>
class FlatStateMap {
 public:
  explicit FlatStateMap(std::size_t width) : width_(width), slots_(1024, 0) {}

  [[nodiscard]] std::size_t size() const { return values_.size(); }

  Value* find(const std::int64_t* key, std::uint64_t hash) {
    const std::size_t mask = slots_.size() - 1;
    for (std::size_t i = hash & mask;; i = (i + 1) & mask) {
      const std::uint32_t slot = slots_[i];
      if (slot == 0) return nullptr;
      const std::size_t e = slot - 1;
      if (hashes_[e] == hash &&
          std::memcmp(&keys_[e * width_], key, width_ * sizeof(std::int64_t)) == 0) {
        return &values_[e];
      }
    }
  }

  /// Inserts a key known to be absent.
  Value* insert(const std::int64_t* key, std::uint64_t hash, Value value) {
    if ((values_.size() + 1) * 4 > slots_.size() * 3) grow();
    const std::size_t e = values_.size();
    keys_.insert(keys_.end(), key, key + width_);
    hashes_.push_back(hash);
    values_.push_back(std::move(value));
    place(e);
    return &values_.back();
  }

 private:
  void place(std::size_t e) {
    const std::size_t mask = slots_.size() - 1;
    std::size_t i = hashes_[e] & mask;
    while (slots_[i] != 0) i = (i + 1) & mask;
    slots_[i] = static_cast<std::uint32_t>(e + 1);
  }
  void grow() {
    slots_.assign(slots_.size() * 2, 0);
    for (std::size_t e = 0; e < values_.size(); ++e) place(e);
  }

  std::size_t width_;
  std::vector<std::uint32_t> slots_;  // entry index + 1; 0 = empty
  std::vector<std::int64_t> keys_;
  std::vector<std::uint64_t> hashes_;
  std::vector<Value> values_;
};

}  // namespace pf::detail
