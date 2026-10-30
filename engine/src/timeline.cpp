#include "pf/timeline.hpp"

#include "pf/rules.hpp"
#include "pf/validator.hpp"

#include <algorithm>
#include <functional>
#include <numeric>
#include <queue>
#include <set>

namespace pf {

std::optional<Timeline> buildTimeline(const Instance& instance, const Schedule& schedule) {
  const ValidationReport report = validate(instance, schedule);
  if (!report.valid) return std::nullopt;

  std::vector<std::size_t> order(schedule.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return schedule[a].start_tick < schedule[b].start_tick;
  });

  // Ticks where something changes.
  std::set<Tick> ticks{0, instance.horizon};
  for (const ScheduledRun& r : schedule) {
    ticks.insert(r.start_tick);
    ticks.insert(r.start_tick + instance.processes[r.process].duration);
  }

  Timeline out;
  out.earnings = report.earnings;
  SimState sim(instance);
  // (end, slot) of busy slots, earliest end first; free slots, lowest first.
  using Busy = std::pair<Tick, std::int32_t>;
  std::priority_queue<Busy, std::vector<Busy>, std::greater<>> busy;
  std::set<std::int32_t> free;
  for (std::int32_t s = 0; s < instance.workers; ++s) free.insert(s);

  std::size_t next = 0;
  for (const Tick t : ticks) {
    sim.advanceTo(t);
    while (!busy.empty() && busy.top().first <= t) {
      free.insert(busy.top().second);
      busy.pop();
    }
    for (; next < order.size() && schedule[order[next]].start_tick == t; ++next) {
      const ScheduledRun& r = schedule[order[next]];
      sim.start(r.process);  // valid: checked by validate() above
      const std::int32_t slot = *free.begin();
      free.erase(free.begin());
      const Tick end = t + instance.processes[r.process].duration;
      busy.emplace(end, slot);
      out.runs.push_back({order[next], r.process, t, end, slot});
    }
    out.points.push_back(
        {t, sim.inventory(), instance.workers - sim.freeWorkers(), sim.inventoryValue()});
  }
  return out;
}

}  // namespace pf
