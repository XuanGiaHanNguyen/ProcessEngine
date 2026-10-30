#pragma once

#include "pf/model.hpp"

#include <algorithm>
#include <optional>
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

}  // namespace pf::test
