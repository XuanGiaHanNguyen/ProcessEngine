#pragma once

#include "pf/model.hpp"

#include <algorithm>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

namespace pf::test {

/// Fluent helper for building small instances in tests, using resource ids.
class InstanceBuilder {
 public:
  InstanceBuilder& resource(const std::string& id, Quantity price, Quantity start = 0) {
    instance_.resources.push_back({id, id, price});
    instance_.start_inventory.push_back(start);
    return *this;
  }

  InstanceBuilder& process(const std::string& id,
                           const std::vector<std::pair<std::string, Quantity>>& inputs,
                           const std::vector<std::pair<std::string, Quantity>>& outputs,
                           Tick duration, std::optional<std::int32_t> max_runs = std::nullopt) {
    Process p{id, id, amounts(inputs), amounts(outputs), duration, max_runs};
    instance_.processes.push_back(std::move(p));
    return *this;
  }

  InstanceBuilder& workers(std::int32_t w) {
    instance_.workers = w;
    return *this;
  }
  InstanceBuilder& horizon(Tick t) {
    instance_.horizon = t;
    return *this;
  }
  InstanceBuilder& startMoney(Quantity m) {
    instance_.start_money = m;
    return *this;
  }

  [[nodiscard]] Instance build() const { return instance_; }

 private:
  std::vector<ResourceAmount> amounts(const std::vector<std::pair<std::string, Quantity>>& in) {
    std::vector<ResourceAmount> out;
    for (const auto& [id, qty] : in) out.push_back({*instance_.findResource(id), qty});
    std::sort(out.begin(), out.end(), [](const ResourceAmount& a, const ResourceAmount& b) {
      return a.resource < b.resource;
    });
    return out;
  }

  Instance instance_;
};

/// A deliberately simple tiny-instance maker (2-4 resources, 1-4 processes, horizon <= 6),
/// small enough for brute force. Milestone 3 adds the real generator.
inline Instance tinyRandom(std::mt19937_64& rng) {
  auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
  InstanceBuilder b;
  const int resources = pick(2, 4);
  for (int r = 0; r < resources; ++r) {
    b.resource("r" + std::to_string(r), pick(0, 6), pick(0, 4));
  }
  const int processes = pick(1, 4);
  for (int p = 0; p < processes; ++p) {
    const int in = pick(0, resources - 1);
    int out = pick(0, resources - 1);
    if (out == in) out = (out + 1) % resources;
    std::vector<std::pair<std::string, Quantity>> inputs;
    if (pick(0, 4) != 0) inputs.emplace_back("r" + std::to_string(in), pick(1, 3));
    std::optional<std::int32_t> max_runs;
    if (pick(0, 3) == 0) max_runs = pick(0, 3);
    b.process("p" + std::to_string(p), inputs, {{"r" + std::to_string(out), pick(1, 3)}},
              pick(1, 3), max_runs);
  }
  return b.workers(pick(1, 2)).horizon(pick(1, 6)).build();
}

}  // namespace pf::test
