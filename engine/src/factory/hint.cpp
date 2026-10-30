#include "pf/factory/hint.hpp"

#include "pf/factory/sim.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <utility>
#include <vector>

namespace pf::factory {

namespace {

const PlacedMachine* machineIn(const Design& design, Index slot) {
  for (const PlacedMachine& m : design.machines) {
    if (m.slot == slot) return &m;
  }
  return nullptr;
}

/// A node as a player reads it: "Cotton supply", "Loom (s1)", "Market".
std::string nodeName(const Level& level, const Design& design, const NodeRef& node) {
  switch (node.kind) {
    case NodeKind::Supplier:
      return level.resources[level.suppliers[node.index].resource].name + " supply";
    case NodeKind::Slot: {
      const PlacedMachine* m = machineIn(design, node.index);
      const std::string& slot = level.slots[node.index].id;
      return m != nullptr ? level.machine_types[m->type].name + " (" + slot + ")" : slot;
    }
    case NodeKind::Market:
      break;
  }
  return "Market";
}

/// Everything that makes or takes each resource in `design`, as belt endpoints.
struct Ends {
  std::vector<std::vector<NodeRef>> producers;
  std::vector<std::vector<NodeRef>> consumers;
};

Ends endsOf(const Level& level, const Design& design) {
  Ends e;
  e.producers.resize(level.resources.size());
  e.consumers.resize(level.resources.size());
  for (Index s = 0; s < level.suppliers.size(); ++s) {
    e.producers[level.suppliers[s].resource].push_back({NodeKind::Supplier, s});
  }
  for (const PlacedMachine& m : design.machines) {
    const Recipe& rec = recipeOf(level, m);
    for (const Amount& a : rec.outputs) e.producers[a.resource].push_back({NodeKind::Slot, m.slot});
    for (const Amount& a : rec.inputs) e.consumers[a.resource].push_back({NodeKind::Slot, m.slot});
  }
  for (const Listing& l : level.listings) e.consumers[l.resource].push_back({NodeKind::Market, 0});
  return e;
}

bool hasBelt(const Design& design, const NodeRef& from, const NodeRef& to, Index resource) {
  return std::any_of(design.belts.begin(), design.belts.end(), [&](const Belt& b) {
    return b.from == from && b.to == to && b.resource == resource;
  });
}

/// `design` without the machine in `slot` and without its belts.
Design withoutMachine(const Design& design, Index slot) {
  Design d = design;
  std::erase_if(d.machines, [&](const PlacedMachine& m) { return m.slot == slot; });
  const NodeRef node{NodeKind::Slot, slot};
  std::erase_if(d.belts, [&](const Belt& b) { return b.from == node || b.to == node; });
  return d;
}

/// Adds `machine` (its slot must be empty) with a belt from every producer of each input and
/// to every consumer of each output.
Design withWiredMachine(const Level& level, const Design& design, const PlacedMachine& machine) {
  Design d = design;
  d.machines.push_back(machine);
  const Ends ends = endsOf(level, d);
  const NodeRef self{NodeKind::Slot, machine.slot};
  const Recipe& rec = recipeOf(level, machine);
  for (const Amount& a : rec.inputs) {
    for (const NodeRef& p : ends.producers[a.resource]) {
      if (!(p == self) && !hasBelt(d, p, self, a.resource))
        d.belts.push_back({p, self, a.resource, 1});
    }
  }
  for (const Amount& a : rec.outputs) {
    for (const NodeRef& c : ends.consumers[a.resource]) {
      if (!(c == self) && !hasBelt(d, self, c, a.resource))
        d.belts.push_back({self, c, a.resource, 1});
    }
  }
  return d;
}

/// Retunes `design`'s orders one step at a time while that raises profit (coordinate
/// descent), so a new or removed machine is judged with orders that suit it.
Design withTunedOrders(const Level& level, Design design) {
  if (!checkDesign(level, design).empty()) return design;
  Cents best = simulate(level, design, false).profit;
  for (int pass = 0; pass < 3; ++pass) {
    bool improved = false;
    for (Index s = 0; s < level.suppliers.size(); ++s) {
      for (const int step : {1, -1}) {
        while (true) {
          Design d = design;
          d.orders[s] += step;
          if (d.orders[s] < 0 || d.orders[s] > level.suppliers[s].max_rate) break;
          const Cents p = simulate(level, d, false).profit;
          if (p <= best) break;
          best = p;
          design = std::move(d);
          improved = true;
        }
      }
    }
    if (!improved) break;
  }
  return design;
}

}  // namespace

std::optional<DesignHint> suggestChange(const Level& level, const Design& design,
                                        double time_limit_ms) {
  const auto start = std::chrono::steady_clock::now();
  const auto expired = [&] {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
               .count() > time_limit_ms;
  };
  const Cents base = simulate(level, design, false).profit;
  std::optional<DesignHint> best;
  const auto consider = [&](Design candidate, const std::string& description) {
    if (expired() || candidate == design || !checkDesign(level, candidate).empty()) return;
    const Cents profit = simulate(level, candidate, false).profit;
    if (profit <= base || (best && profit <= best->profit)) return;
    best = DesignHint{std::move(candidate), profit, profit - base, description};
  };
  const auto resourceName = [&](Index r) { return level.resources[r].name; };
  const auto beltName = [&](const Belt& b) {
    return "the " + resourceName(b.resource) + " belt " + nodeName(level, design, b.from) + " → " +
           nodeName(level, design, b.to);
  };

  // Orders, in small and large steps.
  for (Index s = 0; s < level.suppliers.size(); ++s) {
    for (const int step : {1, -1, 3, -3}) {
      Design d = design;
      d.orders[s] = std::clamp(d.orders[s] + step, 0, level.suppliers[s].max_rate);
      const int diff = d.orders[s] - design.orders[s];
      const std::string what = resourceName(level.suppliers[s].resource);
      consider(std::move(d),
               diff > 0 ? "Order " + std::to_string(diff) + " more " + what + " per hour"
                        : "Order " + std::to_string(-diff) + " less " + what + " per hour");
    }
  }

  // Two orders at once, for resources that only pay off together.
  for (Index a = 0; a < level.suppliers.size(); ++a) {
    for (Index b = a + 1; b < level.suppliers.size(); ++b) {
      for (const int sa : {1, -1}) {
        for (const int sb : {1, -1}) {
          Design d = design;
          d.orders[a] = std::clamp(d.orders[a] + sa, 0, level.suppliers[a].max_rate);
          d.orders[b] = std::clamp(d.orders[b] + sb, 0, level.suppliers[b].max_rate);
          const auto step = [&](Index s, int diff) {
            return std::string(diff > 0 ? "1 more " : "1 less ") +
                   resourceName(level.suppliers[s].resource);
          };
          consider(std::move(d), "Order " + step(a, sa) + " and " + step(b, sb) + " per hour");
        }
      }
    }
  }

  // Belt weights and removals.
  for (Index i = 0; i < design.belts.size(); ++i) {
    const Belt& b = design.belts[i];
    for (const int step : {1, -1}) {
      Design d = design;
      d.belts[i].weight = std::clamp(b.weight + step, 1, limits::kMaxWeight);
      consider(std::move(d),
               std::string(step > 0 ? "Send more" : "Send less") + " along " + beltName(b) +
                   " (split weight " +
                   std::to_string(std::clamp(b.weight + step, 1, limits::kMaxWeight)) + ")");
    }
    Design d = design;
    d.belts.erase(d.belts.begin() + static_cast<std::ptrdiff_t>(i));
    consider(std::move(d), "Remove " + beltName(b));
  }

  // New belts between existing producers and consumers.
  const Ends ends = endsOf(level, design);
  for (Index r = 0; r < level.resources.size(); ++r) {
    for (const NodeRef& p : ends.producers[r]) {
      for (const NodeRef& c : ends.consumers[r]) {
        if (p == c || hasBelt(design, p, c, r)) continue;
        Design d = design;
        const Belt b{p, c, r, 1};
        d.belts.push_back(b);
        consider(std::move(d), "Lay " + beltName(b));
      }
    }
  }

  // Machines: remove or replace one, or build a new one in an empty slot.
  for (const PlacedMachine& m : design.machines) {
    const MachineType& type = level.machine_types[m.type];
    const std::string slot = level.slots[m.slot].id;
    const Design removed = withoutMachine(design, m.slot);
    consider(removed, "Remove the " + type.name + " in " + slot);
    consider(withTunedOrders(level, removed),
             "Remove the " + type.name + " in " + slot + ", then retune the orders");
    for (Index t = 0; t < level.machine_types.size(); ++t) {
      for (Index r = 0; r < level.machine_types[t].recipes.size(); ++r) {
        if (t == m.type && r == m.recipe) continue;
        const MachineType& other = level.machine_types[t];
        const Design d = withWiredMachine(level, removed, {m.slot, t, r});
        const std::string what =
            t == m.type ? "Switch the " + type.name + " in " + slot + " to " + other.recipes[r].name
                        : "Replace the " + type.name + " in " + slot + " with a " + other.name +
                              " (" + other.recipes[r].name + ")";
        consider(d, what);
        consider(withTunedOrders(level, d), what + ", then retune the orders");
      }
    }
  }
  for (Index s = 0; s < level.slots.size(); ++s) {
    if (machineIn(design, s) != nullptr) continue;
    for (Index t = 0; t < level.machine_types.size(); ++t) {
      for (Index r = 0; r < level.machine_types[t].recipes.size(); ++r) {
        const PlacedMachine m{s, t, r};
        const std::string what = "Build a " + level.machine_types[t].name + " (" +
                                 level.machine_types[t].recipes[r].name + ") in " +
                                 level.slots[s].id + " and belt it up";
        const Design wired = withWiredMachine(level, design, m);
        consider(wired, what);
        consider(withTunedOrders(level, wired), what + ", then retune the orders");
      }
    }
  }
  return best;
}

}  // namespace pf::factory
