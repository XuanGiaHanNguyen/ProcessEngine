#include "pf/model.hpp"

#include "pf/limits.hpp"

#include <stdexcept>
#include <unordered_set>

namespace pf {

std::optional<ResourceIndex> Instance::findResource(std::string_view id) const {
  for (ResourceIndex i = 0; i < resources.size(); ++i) {
    if (resources[i].id == id) return i;
  }
  return std::nullopt;
}

std::optional<ProcessIndex> Instance::findProcess(std::string_view id) const {
  for (ProcessIndex i = 0; i < processes.size(); ++i) {
    if (processes[i].id == id) return i;
  }
  return std::nullopt;
}

namespace {

void checkAmounts(const Instance& instance, const Process& process, const char* field,
                  const std::vector<ResourceAmount>& amounts, std::vector<std::string>& errors) {
  const std::string where = "process '" + process.id + "' " + field;
  for (std::size_t i = 0; i < amounts.size(); ++i) {
    const ResourceAmount& a = amounts[i];
    if (a.resource >= instance.resources.size()) {
      errors.push_back(where + ": resource index " + std::to_string(a.resource) + " out of range");
      continue;
    }
    if (a.qty <= 0 || a.qty > limits::kMaxRecipeQty) {
      errors.push_back(where + ": quantity of '" + instance.resources[a.resource].id +
                       "' must be in [1, " + std::to_string(limits::kMaxRecipeQty) + "]");
    }
    if (i > 0 && amounts[i - 1].resource >= a.resource) {
      errors.push_back(where + ": entries must be sorted by resource with no duplicates");
    }
  }
}

}  // namespace

std::vector<std::string> checkInstance(const Instance& instance) {
  std::vector<std::string> errors;

  std::unordered_set<std::string> ids;
  for (const Resource& r : instance.resources) {
    if (r.id.empty()) errors.emplace_back("resource with empty id");
    if (!ids.insert(r.id).second) errors.push_back("duplicate resource id '" + r.id + "'");
    if (r.sell_price < 0 || r.sell_price > limits::kMaxPrice) {
      errors.push_back("resource '" + r.id + "': sell_price must be in [0, " +
                       std::to_string(limits::kMaxPrice) + "]");
    }
  }

  ids.clear();
  for (const Process& p : instance.processes) {
    if (p.id.empty()) errors.emplace_back("process with empty id");
    if (!ids.insert(p.id).second) errors.push_back("duplicate process id '" + p.id + "'");
    if (p.duration < 1 || p.duration > limits::kMaxHorizon) {
      errors.push_back("process '" + p.id + "': duration must be in [1, " +
                       std::to_string(limits::kMaxHorizon) + "]");
    }
    if (p.max_runs && *p.max_runs < 0) {
      errors.push_back("process '" + p.id + "': max_runs must be >= 0");
    }
    checkAmounts(instance, p, "inputs", p.inputs, errors);
    checkAmounts(instance, p, "outputs", p.outputs, errors);
  }

  if (instance.start_inventory.size() != instance.resources.size()) {
    errors.emplace_back("start_inventory must have one entry per resource");
  } else {
    for (std::size_t i = 0; i < instance.resources.size(); ++i) {
      const Quantity q = instance.start_inventory[i];
      if (q < 0 || q > limits::kMaxStartQty) {
        errors.push_back("start_inventory of '" + instance.resources[i].id + "' must be in [0, " +
                         std::to_string(limits::kMaxStartQty) + "]");
      }
    }
  }

  if (instance.workers < 1 || instance.workers > limits::kMaxWorkers) {
    errors.push_back("workers must be in [1, " + std::to_string(limits::kMaxWorkers) + "]");
  }
  if (instance.horizon < 0 || instance.horizon > limits::kMaxHorizon) {
    errors.push_back("horizon must be in [0, " + std::to_string(limits::kMaxHorizon) + "]");
  }
  if (instance.start_money < 0 || instance.start_money > limits::kMaxStartMoney) {
    errors.push_back("start_money must be in [0, " + std::to_string(limits::kMaxStartMoney) + "]");
  }
  return errors;
}

Quantity inventoryValue(const Instance& instance, const std::vector<Quantity>& inventory) {
  Quantity total = instance.start_money;
  for (std::size_t i = 0; i < inventory.size() && i < instance.resources.size(); ++i) {
    Quantity value = 0;
    if (__builtin_mul_overflow(inventory[i], instance.resources[i].sell_price, &value) ||
        __builtin_add_overflow(total, value, &total)) {
      throw std::overflow_error("earnings do not fit in a 64-bit integer");
    }
  }
  return total;
}

}  // namespace pf
