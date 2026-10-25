#include "pf/factory/level.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <tuple>

namespace pf::factory {

namespace {

template <typename T>
std::optional<Index> findById(const std::vector<T>& items, std::string_view id) {
  for (Index i = 0; i < items.size(); ++i) {
    if (items[i].id == id) return i;
  }
  return std::nullopt;
}

bool validId(const std::string& id) {
  if (id.empty() || id.size() > 40) return false;
  return std::all_of(id.begin(), id.end(), [](char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
  });
}

bool hasResource(const std::vector<Amount>& amounts, Index resource) {
  return std::any_of(amounts.begin(), amounts.end(),
                     [&](const Amount& a) { return a.resource == resource; });
}

void checkAmounts(const std::vector<Amount>& amounts, const Level& level, const std::string& where,
                  std::vector<std::string>& errors) {
  for (Index i = 0; i < amounts.size(); ++i) {
    const Amount& a = amounts[i];
    if (a.resource >= level.resources.size()) {
      errors.push_back(where + ": unknown resource");
      continue;
    }
    if (a.qty < 1 || a.qty > limits::kMaxRecipeQty) {
      errors.push_back(where + ": quantity of " + level.resources[a.resource].id + " must be 1.." +
                       std::to_string(limits::kMaxRecipeQty));
    }
    if (i > 0 && amounts[i - 1].resource >= a.resource) {
      errors.push_back(where + ": resources must be sorted and unique");
    }
  }
}

bool moneyOk(Cents c) { return c >= 0 && c <= limits::kMaxMoney; }

/// Index of the machine built in each slot, or -1.
std::vector<int> machineBySlot(const Level& level, const Design& design) {
  std::vector<int> by_slot(level.slots.size(), -1);
  for (Index m = 0; m < design.machines.size(); ++m) {
    const Index slot = design.machines[m].slot;
    if (slot < by_slot.size() && by_slot[slot] < 0) by_slot[slot] = static_cast<int>(m);
  }
  return by_slot;
}

}  // namespace

std::optional<Index> Level::findResource(std::string_view key) const {
  return findById(resources, key);
}
std::optional<Index> Level::findSupplier(std::string_view key) const {
  return findById(suppliers, key);
}
std::optional<Index> Level::findMachineType(std::string_view key) const {
  return findById(machine_types, key);
}
std::optional<Index> Level::findSlot(std::string_view key) const { return findById(slots, key); }

const Listing* Level::listingFor(Index resource) const {
  for (const Listing& l : listings) {
    if (l.resource == resource) return &l;
  }
  return nullptr;
}

std::vector<std::string> checkLevel(const Level& level) {
  std::vector<std::string> errors;
  if (level.hours < 1 || level.hours > limits::kMaxHours) {
    errors.push_back("hours must be 1.." + std::to_string(limits::kMaxHours));
  }
  if (level.belt_capacity < 1 || level.belt_capacity > limits::kMaxBeltCapacity) {
    errors.push_back("belt_capacity must be 1.." + std::to_string(limits::kMaxBeltCapacity));
  }
  if (level.buffer_batches < 1 || level.buffer_batches > limits::kMaxBufferBatches) {
    errors.push_back("buffer_batches must be 1.." + std::to_string(limits::kMaxBufferBatches));
  }
  if (level.budget && (*level.budget < 0 || *level.budget > 1000 * limits::kMaxMoney)) {
    errors.emplace_back("budget out of range");
  }
  if (level.resources.empty() || level.resources.size() > limits::kMaxResources) {
    errors.push_back("resources: need 1.." + std::to_string(limits::kMaxResources));
  }
  if (level.suppliers.size() > limits::kMaxSuppliers) errors.emplace_back("too many suppliers");
  if (level.machine_types.size() > limits::kMaxMachineTypes) {
    errors.emplace_back("too many machine_types");
  }
  if (level.slots.size() > limits::kMaxSlots) errors.emplace_back("too many slots");

  std::set<std::string> node_ids;
  auto nodeIdOk = [&](const std::string& id, const std::string& where) {
    if (!validId(id)) errors.push_back(where + ": id must be 1-40 characters of [a-z0-9-]");
    if (id == "market") errors.push_back(where + ": id \"market\" is reserved");
    if (!node_ids.insert(id).second) errors.push_back(where + ": duplicate node id " + id);
  };

  std::set<std::string> ids;
  for (const Resource& r : level.resources) {
    if (!validId(r.id)) errors.push_back("resource " + r.id + ": bad id");
    if (!ids.insert(r.id).second) errors.push_back("duplicate resource id " + r.id);
  }
  for (const Supplier& s : level.suppliers) {
    nodeIdOk(s.id, "supplier " + s.id);
    if (s.resource >= level.resources.size())
      errors.push_back("supplier " + s.id + ": bad resource");
    if (s.max_rate < 0 || s.max_rate > limits::kMaxRate) {
      errors.push_back("supplier " + s.id + ": max_rate out of range");
    }
    if (!moneyOk(s.unit_cost)) errors.push_back("supplier " + s.id + ": unit_cost out of range");
  }
  for (const Slot& s : level.slots) nodeIdOk(s.id, "slot " + s.id);

  ids.clear();
  for (const MachineType& t : level.machine_types) {
    if (!validId(t.id)) errors.push_back("machine type " + t.id + ": bad id");
    if (!ids.insert(t.id).second) errors.push_back("duplicate machine type id " + t.id);
    if (!moneyOk(t.cost)) errors.push_back("machine type " + t.id + ": cost out of range");
    if (t.recipes.empty() || t.recipes.size() > limits::kMaxRecipes) {
      errors.push_back("machine type " + t.id + ": needs 1.." +
                       std::to_string(limits::kMaxRecipes) + " recipes");
    }
    std::set<std::string> recipe_ids;
    for (const Recipe& r : t.recipes) {
      const std::string where = "recipe " + t.id + "/" + r.id;
      if (!validId(r.id)) errors.push_back(where + ": bad id");
      if (!recipe_ids.insert(r.id).second) errors.push_back(where + ": duplicate id");
      if (r.hours < 1 || r.hours > limits::kMaxRecipeHours) {
        errors.push_back(where + ": hours must be 1.." + std::to_string(limits::kMaxRecipeHours));
      }
      if (r.outputs.empty()) errors.push_back(where + ": needs at least one output");
      checkAmounts(r.inputs, level, where + " inputs", errors);
      checkAmounts(r.outputs, level, where + " outputs", errors);
    }
  }

  std::set<Index> listed;
  for (const Listing& l : level.listings) {
    if (l.resource >= level.resources.size()) {
      errors.emplace_back("listing: bad resource");
      continue;
    }
    const std::string where = "listing " + level.resources[l.resource].id;
    if (!listed.insert(l.resource).second) errors.push_back(where + ": duplicate");
    if (!moneyOk(l.price) || !moneyOk(l.overflow_price)) errors.push_back(where + ": bad price");
    if (l.demand < 0 || l.demand > limits::kMaxDemand) errors.push_back(where + ": bad demand");
  }
  return errors;
}

const Recipe& recipeOf(const Level& level, const PlacedMachine& machine) {
  return level.machine_types[machine.type].recipes[machine.recipe];
}

std::string nodeId(const Level& level, const NodeRef& node) {
  switch (node.kind) {
    case NodeKind::Supplier:
      return node.index < level.suppliers.size() ? level.suppliers[node.index].id : "?";
    case NodeKind::Slot:
      return node.index < level.slots.size() ? level.slots[node.index].id : "?";
    case NodeKind::Market:
      return "market";
  }
  return "?";
}

Cents buildCost(const Level& level, const Design& design) {
  Cents total = 0;
  for (const PlacedMachine& m : design.machines) total += level.machine_types[m.type].cost;
  return total;
}

Design emptyDesign(const Level& level) {
  Design d;
  d.orders.assign(level.suppliers.size(), 0);
  return d;
}

std::vector<DesignIssue> checkDesign(const Level& level, const Design& design) {
  std::vector<DesignIssue> issues;
  auto add = [&](std::string code, std::string message) {
    issues.push_back({std::move(code), std::move(message)});
  };

  if (design.orders.size() != level.suppliers.size()) {
    add("unknown_supplier", "orders must list exactly the level's suppliers");
  } else {
    for (Index s = 0; s < level.suppliers.size(); ++s) {
      if (design.orders[s] < 0 || design.orders[s] > level.suppliers[s].max_rate) {
        add("order_out_of_range", level.suppliers[s].id + ": order must be 0.." +
                                      std::to_string(level.suppliers[s].max_rate));
      }
    }
  }

  std::vector<bool> used(level.slots.size(), false);
  bool machines_ok = true;
  for (const PlacedMachine& m : design.machines) {
    if (m.slot >= level.slots.size()) {
      add("unknown_slot", "machine in an unknown slot");
      machines_ok = false;
      continue;
    }
    const std::string& slot = level.slots[m.slot].id;
    if (used[m.slot]) add("slot_used_twice", slot + " has more than one machine");
    used[m.slot] = true;
    if (m.type >= level.machine_types.size()) {
      add("unknown_machine_type", slot + ": unknown machine type");
      machines_ok = false;
    } else if (m.recipe >= level.machine_types[m.type].recipes.size()) {
      add("unknown_recipe", slot + ": " + level.machine_types[m.type].id + " has no such recipe");
      machines_ok = false;
    }
  }
  if (!machines_ok) return issues;

  if (level.budget && buildCost(level, design) > *level.budget) {
    add("over_budget", "build cost " + std::to_string(buildCost(level, design)) +
                           " exceeds the budget of " + std::to_string(*level.budget));
  }

  if (design.belts.size() > limits::kMaxBelts) {
    add("too_many_belts", "at most " + std::to_string(limits::kMaxBelts) + " belts");
  }
  const std::vector<int> by_slot = machineBySlot(level, design);
  auto machineAt = [&](const NodeRef& n) -> const PlacedMachine* {
    if (n.kind != NodeKind::Slot || n.index >= by_slot.size() || by_slot[n.index] < 0) {
      return nullptr;
    }
    return &design.machines[static_cast<Index>(by_slot[n.index])];
  };
  std::set<std::tuple<int, Index, int, Index, Index>> seen;
  for (const Belt& b : design.belts) {
    const bool from_ok =
        (b.from.kind == NodeKind::Supplier && b.from.index < level.suppliers.size()) ||
        machineAt(b.from) != nullptr;
    const bool to_ok = b.to.kind == NodeKind::Market || machineAt(b.to) != nullptr;
    if (!from_ok || !to_ok || b.resource >= level.resources.size()) {
      add("unknown_node", "belt " + nodeId(level, b.from) + " -> " + nodeId(level, b.to) +
                              ": both ends must be a supplier, a built machine or the market");
      continue;
    }
    const std::string& res = level.resources[b.resource].id;
    const std::string name =
        nodeId(level, b.from) + " -> " + nodeId(level, b.to) + " (" + res + ")";
    if (b.from == b.to) add("self_belt", "belt " + name + " starts and ends at the same node");
    const bool emits = b.from.kind == NodeKind::Supplier
                           ? level.suppliers[b.from.index].resource == b.resource
                           : hasResource(recipeOf(level, *machineAt(b.from)).outputs, b.resource);
    if (!emits) add("belt_wrong_resource", nodeId(level, b.from) + " does not make " + res);
    const bool takes = b.to.kind == NodeKind::Market
                           ? level.listingFor(b.resource) != nullptr
                           : hasResource(recipeOf(level, *machineAt(b.to)).inputs, b.resource);
    if (!takes) add("belt_not_accepted", nodeId(level, b.to) + " does not take " + res);
    if (b.weight < 1 || b.weight > limits::kMaxWeight) {
      add("weight_out_of_range",
          "belt " + name + ": weight must be 1.." + std::to_string(limits::kMaxWeight));
    }
    const int to_kind = static_cast<int>(b.to.kind);
    if (!seen.insert({static_cast<int>(b.from.kind), b.from.index, to_kind,
                      b.to.kind == NodeKind::Market ? 0 : b.to.index, b.resource})
             .second) {
      add("duplicate_belt", "belt " + name + " appears twice");
    }
  }
  return issues;
}

std::vector<DesignIssue> designWarnings(const Level& level, const Design& design) {
  std::vector<DesignIssue> warnings;
  auto hasBelt = [&](const NodeRef& n, Index resource, bool outgoing) {
    return std::any_of(design.belts.begin(), design.belts.end(), [&](const Belt& b) {
      return b.resource == resource && (outgoing ? b.from == n : b.to == n);
    });
  };
  for (Index s = 0; s < level.suppliers.size(); ++s) {
    const NodeRef n{NodeKind::Supplier, s};
    if (s < design.orders.size() && design.orders[s] > 0 &&
        !hasBelt(n, level.suppliers[s].resource, true)) {
      warnings.push_back({"unused_supply", level.suppliers[s].id +
                                               " delivers (and charges for) units that no belt "
                                               "takes"});
    }
  }
  for (const PlacedMachine& m : design.machines) {
    const NodeRef n{NodeKind::Slot, m.slot};
    const Recipe& r = recipeOf(level, m);
    const std::string& slot = level.slots[m.slot].id;
    for (const Amount& a : r.inputs) {
      if (!hasBelt(n, a.resource, false)) {
        warnings.push_back({"unfed_input", slot + " gets no " + level.resources[a.resource].name});
      }
    }
    for (const Amount& a : r.outputs) {
      if (!hasBelt(n, a.resource, true)) {
        warnings.push_back({"output_blocked", slot + " has nowhere to send " +
                                                  level.resources[a.resource].name +
                                                  ", so it will stop when full"});
      }
    }
  }
  return warnings;
}

}  // namespace pf::factory
