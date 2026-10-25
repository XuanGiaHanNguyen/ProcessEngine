#include "pf/factory/sim.hpp"

#include <algorithm>
#include <cstdint>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace pf::factory {

namespace {

using Qty = std::int64_t;

/// The outgoing belts of one node for one resource, in design order.
struct BeltGroup {
  Index resource = 0;
  std::vector<Index> belts;
};

struct MachineState {
  const Recipe* recipe = nullptr;
  std::vector<Qty> in;      // per level resource
  std::vector<Qty> in_cap;  // per level resource (0 = not an input)
  bool busy = false;
  int started = 0;
  int batches_done = 0;
};

}  // namespace

SimResult simulate(const Level& level, const Design& design, bool with_report) {
  if (!checkDesign(level, design).empty()) {
    throw std::invalid_argument("simulate: the design is not valid for this level");
  }
  const Index n_res = level.resources.size();
  const Index n_sup = level.suppliers.size();
  const Index n_mach = design.machines.size();
  const Qty k = level.buffer_batches;

  // Machines in level slot order (dispatch order); node ids: suppliers, then machines.
  std::vector<Index> order(n_mach);
  std::iota(order.begin(), order.end(), Index{0});
  std::sort(order.begin(), order.end(),
            [&](Index a, Index b) { return design.machines[a].slot < design.machines[b].slot; });
  std::vector<int> machine_of_slot(level.slots.size(), -1);
  for (Index m = 0; m < n_mach; ++m) {
    machine_of_slot[design.machines[m].slot] = static_cast<int>(m);
  }
  auto nodeOf = [&](const NodeRef& n) -> int {
    switch (n.kind) {
      case NodeKind::Supplier:
        return static_cast<int>(n.index);
      case NodeKind::Slot:
        return static_cast<int>(n_sup) + machine_of_slot[n.index];
      case NodeKind::Market:
        return -1;
    }
    return -1;
  };

  std::vector<MachineState> machines(n_mach);
  for (Index m = 0; m < n_mach; ++m) {
    MachineState& ms = machines[m];
    ms.recipe = &recipeOf(level, design.machines[m]);
    ms.in.assign(n_res, 0);
    ms.in_cap.assign(n_res, 0);
    for (const Amount& a : ms.recipe->inputs) ms.in_cap[a.resource] = k * a.qty;
  }
  const Index n_nodes = n_sup + n_mach;
  std::vector<std::vector<Qty>> out(n_nodes, std::vector<Qty>(n_res, 0));

  // Belt endpoints as node ids, and per-node groups ordered by resource.
  const Index n_belts = design.belts.size();
  std::vector<int> belt_to(n_belts);
  std::vector<std::vector<BeltGroup>> groups(n_nodes);
  for (Index b = 0; b < n_belts; ++b) {
    const Belt& belt = design.belts[b];
    belt_to[b] = nodeOf(belt.to);
    auto& g = groups[static_cast<Index>(nodeOf(belt.from))];
    auto it = std::find_if(g.begin(), g.end(),
                           [&](const BeltGroup& x) { return x.resource == belt.resource; });
    if (it == g.end()) {
      g.push_back({belt.resource, {}});
      it = g.end() - 1;
    }
    it->belts.push_back(b);
  }
  for (auto& g : groups) {
    std::sort(g.begin(), g.end(),
              [](const BeltGroup& a, const BeltGroup& b) { return a.resource < b.resource; });
  }
  std::vector<Index> node_order(n_nodes);
  for (Index s = 0; s < n_sup; ++s) node_order[s] = s;
  for (Index i = 0; i < n_mach; ++i) node_order[n_sup + i] = n_sup + order[i];

  SimResult result;
  result.build = buildCost(level, design);
  result.sold.assign(n_res, 0);
  Cents money = -result.build;

  std::vector<Qty> credit(n_belts, 0);
  std::vector<int> transit(n_belts, 0);
  std::vector<int> sent(n_belts, 0);
  std::vector<Qty> sold_h(n_res, 0);
  std::vector<std::vector<Qty>> incoming(n_mach, std::vector<Qty>(n_res, 0));
  std::vector<MachineHour> status(n_mach);
  std::vector<Index> eligible;

  for (int h = 0; h < level.hours; ++h) {
    // 1. Arrive.
    std::fill(sold_h.begin(), sold_h.end(), 0);
    for (Index b = 0; b < n_belts; ++b) {
      if (transit[b] == 0) continue;
      const Index r = design.belts[b].resource;
      if (belt_to[b] < 0) {
        sold_h[r] += transit[b];
      } else {
        machines[static_cast<Index>(belt_to[b]) - n_sup].in[r] += transit[b];
      }
    }
    Cents revenue = 0;
    for (Index r = 0; r < n_res; ++r) {
      if (sold_h[r] == 0) continue;
      const Listing& l = *level.listingFor(r);
      const Qty full = std::min<Qty>(sold_h[r], l.demand);
      revenue += full * l.price + (sold_h[r] - full) * l.overflow_price;
      result.sold[r] += sold_h[r];
    }

    // 2. Complete.
    for (Index m = 0; m < n_mach; ++m) {
      MachineState& ms = machines[m];
      if (ms.busy && ms.started + ms.recipe->hours == h) {
        for (const Amount& a : ms.recipe->outputs) out[n_sup + m][a.resource] += a.qty;
        ms.busy = false;
        ++ms.batches_done;
      }
    }

    // 3. Start.
    for (Index m = 0; m < n_mach; ++m) {
      MachineState& ms = machines[m];
      MachineHour& st = status[m];
      st.missing.clear();
      if (!ms.busy) {
        bool inputs_ready = true;
        for (const Amount& a : ms.recipe->inputs) {
          if (ms.in[a.resource] < a.qty) {
            inputs_ready = false;
            st.missing.push_back(a.resource);
          }
        }
        bool room = true;
        for (const Amount& a : ms.recipe->outputs) {
          if (out[n_sup + m][a.resource] + a.qty > k * a.qty) room = false;
        }
        if (inputs_ready && room) {
          for (const Amount& a : ms.recipe->inputs) ms.in[a.resource] -= a.qty;
          ms.busy = true;
          ms.started = h;
        } else {
          st.status = inputs_ready ? MachineStatus::Blocked : MachineStatus::Starved;
        }
      }
      if (ms.busy) {
        st.status = MachineStatus::Busy;
        st.progress = h - ms.started + 1;
      } else {
        st.progress = 0;
      }
      st.total = ms.recipe->hours;
      st.batches_done = ms.batches_done;
    }

    // 4. Deliver.
    Cents purchases = 0;
    for (Index s = 0; s < n_sup; ++s) {
      const int q = design.orders[s];
      out[s][level.suppliers[s].resource] += q;
      purchases += static_cast<Cents>(q) * level.suppliers[s].unit_cost;
    }

    // 5. Dispatch, by smooth weighted round-robin over the belts that can take a unit.
    std::fill(sent.begin(), sent.end(), 0);
    for (auto& inc : incoming) std::fill(inc.begin(), inc.end(), 0);
    for (const Index node : node_order) {
      for (const BeltGroup& g : groups[node]) {
        const Index r = g.resource;
        Qty& available = out[node][r];
        while (available > 0) {
          eligible.clear();
          Qty total_weight = 0;
          for (const Index b : g.belts) {
            if (sent[b] >= level.belt_capacity) continue;
            if (belt_to[b] >= 0) {
              const Index tm = static_cast<Index>(belt_to[b]) - n_sup;
              if (machines[tm].in[r] + incoming[tm][r] >= machines[tm].in_cap[r]) continue;
            }
            eligible.push_back(b);
            total_weight += design.belts[b].weight;
          }
          if (eligible.empty()) break;
          Index best = eligible.front();
          for (const Index b : eligible) {
            credit[b] += design.belts[b].weight;
            if (credit[b] > credit[best]) best = b;
          }
          credit[best] -= total_weight;
          ++sent[best];
          --available;
          if (belt_to[best] >= 0) ++incoming[static_cast<Index>(belt_to[best]) - n_sup][r];
        }
      }
    }
    transit = sent;

    money += revenue - purchases;
    result.sales += revenue;
    result.purchases += purchases;

    if (with_report) {
      HourReport rep;
      rep.hour = h;
      rep.revenue = revenue;
      rep.purchases = purchases;
      rep.profit = money;
      rep.sold = sold_h;
      rep.belt_moved = sent;
      rep.stock.resize(n_sup);
      for (Index s = 0; s < n_sup; ++s) rep.stock[s] = out[s][level.suppliers[s].resource];
      rep.machines = status;
      for (Index m = 0; m < n_mach; ++m) {
        MachineHour& mh = rep.machines[m];
        mh.inputs.clear();
        mh.outputs.clear();
        for (const Amount& a : machines[m].recipe->inputs)
          mh.inputs.push_back(machines[m].in[a.resource]);
        for (const Amount& a : machines[m].recipe->outputs)
          mh.outputs.push_back(out[n_sup + m][a.resource]);
      }
      result.hours.push_back(std::move(rep));
    }
  }
  result.profit = money;
  return result;
}

}  // namespace pf::factory
