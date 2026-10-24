#include "pf/validator.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>

namespace pf {

namespace {

ValidationReport fail(RuleViolation kind, std::size_t run_index, Tick tick,
                      const std::string& reason) {
  ValidationReport report;
  report.violation = Violation{
      kind, run_index, tick,
      "run #" + std::to_string(run_index) + " at tick " + std::to_string(tick) + ": " + reason};
  return report;
}

}  // namespace

ValidationReport validate(const Instance& instance, const Schedule& schedule) {
  std::vector<std::size_t> order(schedule.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return schedule[a].start_tick < schedule[b].start_tick;
  });

  SimState sim(instance);
  for (const std::size_t i : order) {
    const ScheduledRun& run = schedule[i];
    if (run.process >= instance.processes.size()) {
      return fail(RuleViolation::UnknownProcess, i, run.start_tick,
                  "unknown process index " + std::to_string(run.process));
    }
    if (run.start_tick < 0) {
      return fail(RuleViolation::StartInPast, i, run.start_tick, "negative start tick");
    }
    if (run.start_tick > instance.horizon) {
      const Process& p = instance.processes[run.process];
      return fail(RuleViolation::PastHorizon, i, run.start_tick,
                  "'" + p.id + "' starts after the horizon " + std::to_string(instance.horizon));
    }
    sim.advanceTo(run.start_tick);
    if (auto error = sim.checkStart(run.process)) {
      return fail(error->kind, i, run.start_tick, error->message);
    }
    sim.start(run.process);
  }
  sim.advanceTo(instance.horizon);

  ValidationReport report;
  try {
    report.earnings = sim.inventoryValue();
  } catch (const std::overflow_error& e) {
    report.violation =
        Violation{RuleViolation::Overflow, schedule.size(), instance.horizon, e.what()};
    return report;
  }
  report.valid = true;
  report.final_inventory = sim.inventory();
  return report;
}

}  // namespace pf
