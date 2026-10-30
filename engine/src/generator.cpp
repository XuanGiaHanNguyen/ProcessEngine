#include "pf/generator.hpp"

#include <algorithm>
#include <array>
#include <map>
#include <random>
#include <string>

namespace pf {

GeneratorParams GeneratorParams::small(std::uint64_t seed) {
  std::mt19937_64 rng(seed);
  auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
  GeneratorParams p;
  p.seed = seed;
  p.chain_depth = pick(1, 2);
  p.resources = pick(3, 4);
  p.processes = pick(2, 4);
  p.workers = pick(1, 2);
  p.horizon = pick(3, 7);
  p.max_price = 10;
  p.max_qty = 2;
  p.max_duration = 3;
  return p;
}

GeneratorParams GeneratorParams::medium(std::uint64_t seed) {
  GeneratorParams p;
  p.seed = seed;
  p.chain_depth = 3;
  p.resources = 8;
  p.processes = 10;
  p.workers = 3;
  p.horizon = 30;
  p.max_duration = 5;
  return p;
}

GeneratorParams GeneratorParams::large(std::uint64_t seed) {
  GeneratorParams p;
  p.seed = seed;
  p.chain_depth = 5;
  p.resources = 20;
  p.processes = 30;
  p.workers = 5;
  p.horizon = 100;
  p.max_price = 50;
  p.max_duration = 8;
  return p;
}

namespace {

constexpr std::array<std::array<const char*, 4>, 4> kLevelWords = {{
    {"Ore", "Wood", "Clay", "Fiber"},
    {"Ingot", "Plank", "Brick", "Thread"},
    {"Part", "Beam", "Tile", "Cloth"},
    {"Gadget", "Frame", "Kiln", "Garment"},
}};

class Builder {
 public:
  explicit Builder(const GeneratorParams& params) : p_(params), rng_(params.seed) {}

  GeneratedInstance build() {
    const std::int32_t depth = std::max<std::int32_t>(1, p_.chain_depth);
    const std::int32_t resources = std::max(p_.resources, depth + 1);
    const std::int32_t processes = std::max(p_.processes, depth);

    // Levels: the chain occupies resources 0..depth (one per level); the rest are random.
    levels_.resize(static_cast<std::size_t>(resources));
    for (std::int32_t r = 0; r < resources; ++r) {
      levels_[static_cast<std::size_t>(r)] = r <= depth ? r : integer(0, depth);
    }
    for (std::int32_t r = 0; r < resources; ++r) addResource(r);

    // The guaranteed chain: step L turns q[L] of r_{L-1} into q[L+1] of r_L (q[depth+1] = 1),
    // so running each step once consumes exactly what the previous step produced.
    std::vector<Quantity> q(static_cast<std::size_t>(depth) + 2, 1);
    for (std::int32_t level = 1; level <= depth; ++level) {
      q[static_cast<std::size_t>(level)] = integer<Quantity>(1, p_.max_qty);
    }
    Tick chain_duration = 0;
    for (std::int32_t level = 1; level <= depth; ++level) {
      const auto l = static_cast<std::size_t>(level);
      Process proc;
      proc.inputs = {{static_cast<ResourceIndex>(level - 1), q[l]}};
      proc.outputs = {{static_cast<ResourceIndex>(level), q[l + 1]}};
      proc.duration = integer<Tick>(1, p_.max_duration);
      chain_duration += proc.duration;
      addProcess(std::move(proc));
    }
    const Quantity raw_needed = q[1];
    for (std::int32_t i = depth; i < processes; ++i) addProcess(randomProcess());

    instance_.workers = std::max(1, p_.workers);
    instance_.horizon = std::max(p_.horizon, chain_duration);
    instance_.start_money = 0;
    instance_.start_inventory.assign(static_cast<std::size_t>(resources), 0);
    for (std::size_t r = 0; r < instance_.resources.size(); ++r) {
      if (levels_[r] == 0) instance_.start_inventory[r] = integer<Quantity>(0, 4 * p_.max_qty);
    }
    instance_.start_inventory[0] = std::max(instance_.start_inventory[0], raw_needed);

    // Make the chain strictly profitable: one top product beats the raw it consumes (every
    // intermediate it makes is consumed again, so only the two ends matter).
    Resource& top = instance_.resources[static_cast<std::size_t>(depth)];
    top.sell_price = std::max(top.sell_price, raw_needed * instance_.resources[0].sell_price + 1);

    GeneratedInstance out;
    out.instance = std::move(instance_);
    Tick t = 0;
    for (std::int32_t level = 1; level <= depth; ++level) {
      const auto proc = static_cast<ProcessIndex>(level - 1);
      out.witness.push_back({proc, t});
      t += out.instance.processes[proc].duration;
    }
    return out;
  }

 private:
  template <typename Int = int>
  Int integer(Int lo, Int hi) {
    return std::uniform_int_distribution<Int>(lo, std::max(lo, hi))(rng_);
  }
  bool chance(double probability) { return std::bernoulli_distribution(probability)(rng_); }

  void addResource(std::int32_t r) {
    const std::int32_t level = levels_[static_cast<std::size_t>(r)];
    const auto& words = kLevelWords[std::min<std::size_t>(static_cast<std::size_t>(level), 3)];
    Resource res;
    res.id = "r" + std::to_string(r);
    res.name =
        std::string(words[static_cast<std::size_t>(r) % words.size()]) + " " + std::to_string(r);
    // Prices grow with level; intermediates are sometimes unsellable.
    const Quantity lo = p_.min_price * (level + 1);
    const Quantity hi = std::max(lo, p_.max_price * (level + 1) / 2);
    const bool intermediate = level > 0 && level < std::max<std::int32_t>(1, p_.chain_depth);
    res.sell_price =
        (intermediate && chance(p_.zero_price_probability)) ? 0 : integer<Quantity>(lo, hi);
    instance_.resources.push_back(std::move(res));
  }

  ResourceIndex resourceAtLevel(std::int32_t level) {
    std::vector<ResourceIndex> candidates;
    for (std::size_t r = 0; r < levels_.size(); ++r) {
      if (levels_[r] == level) candidates.push_back(r);
    }
    return candidates[integer<std::size_t>(0, candidates.size() - 1)];
  }

  Process randomProcess() {
    const std::int32_t depth = std::max<std::int32_t>(1, p_.chain_depth);
    Process proc;
    if (chance(p_.generator_probability)) {
      // A source: produces level-0 resources from nothing, slowly.
      proc.outputs = {{resourceAtLevel(0), integer<Quantity>(1, p_.max_qty)}};
      proc.duration = integer<Tick>(1, p_.max_duration) + 1;
    } else {
      const std::int32_t out_level = integer(1, depth);
      std::map<ResourceIndex, Quantity> inputs;
      const int n_inputs = integer(1, 2);
      for (int i = 0; i < n_inputs; ++i) {
        inputs[resourceAtLevel(integer(0, out_level - 1))] += integer<Quantity>(1, p_.max_qty);
      }
      for (const auto& [r, q] : inputs) proc.inputs.push_back({r, q});
      proc.outputs = {{resourceAtLevel(out_level), integer<Quantity>(1, p_.max_qty)}};
      proc.duration = integer<Tick>(1, p_.max_duration);
    }
    if (chance(p_.max_runs_probability)) proc.max_runs = integer(1, 4);
    return proc;
  }

  void addProcess(Process proc) {
    const std::size_t index = instance_.processes.size();
    proc.id = "p" + std::to_string(index);
    const Resource& out = instance_.resources[proc.outputs.front().resource];
    proc.name = "Make " + out.name;
    instance_.processes.push_back(std::move(proc));
  }

  GeneratorParams p_;
  std::mt19937_64 rng_;
  std::vector<std::int32_t> levels_;
  Instance instance_;
};

}  // namespace

GeneratedInstance generate(const GeneratorParams& params) {
  GeneratedInstance out = Builder(params).build();
  out.instance.name = "generated-" + std::to_string(params.seed);
  return out;
}

}  // namespace pf
