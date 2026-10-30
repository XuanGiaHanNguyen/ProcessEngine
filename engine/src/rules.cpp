#include "pf/rules.hpp"

#include <algorithm>
#include <tuple>

namespace pf {

const char* toString(RuleViolation kind) {
  switch (kind) {
    case RuleViolation::UnknownProcess:
      return "unknown_process";
    case RuleViolation::StartInPast:
      return "start_in_past";
    case RuleViolation::PastHorizon:
      return "past_horizon";
    case RuleViolation::InsufficientInventory:
      return "insufficient_inventory";
    case RuleViolation::NoFreeWorker:
      return "no_free_worker";
    case RuleViolation::MaxRunsExceeded:
      return "max_runs_exceeded";
    case RuleViolation::Overflow:
      return "overflow";
  }
  return "unknown";
}

namespace {

const std::string& displayName(const Resource& r) { return r.name.empty() ? r.id : r.name; }

bool runOrder(const ActiveRun& a, const ActiveRun& b) {
  return std::tie(a.end, a.start, a.process) < std::tie(b.end, b.start, b.process);
}

}  // namespace

SimState::SimState(const Instance& instance)
    : instance_(&instance),
      inventory_(instance.start_inventory),
      run_counts_(instance.processes.size(), 0) {}

std::int32_t SimState::freeWorkers() const {
  return instance_->workers - static_cast<std::int32_t>(active_.size());
}

std::optional<RuleError> SimState::checkStart(ProcessIndex process) const {
  if (process >= instance_->processes.size()) {
    return RuleError{RuleViolation::UnknownProcess,
                     "unknown process index " + std::to_string(process)};
  }
  const Process& p = instance_->processes[process];
  const Tick end = tick_ + p.duration;
  if (end > instance_->horizon) {
    return RuleError{RuleViolation::PastHorizon, "'" + p.id + "' would finish at tick " +
                                                     std::to_string(end) + ", after the horizon " +
                                                     std::to_string(instance_->horizon)};
  }
  if (p.max_runs && run_counts_[process] >= *p.max_runs) {
    return RuleError{RuleViolation::MaxRunsExceeded,
                     "'" + p.id + "' already ran " + std::to_string(run_counts_[process]) +
                         " times (max_runs " + std::to_string(*p.max_runs) + ")"};
  }
  if (freeWorkers() <= 0) {
    return RuleError{RuleViolation::NoFreeWorker,
                     "no free worker (" + std::to_string(instance_->workers) + "/" +
                         std::to_string(instance_->workers) + " busy)"};
  }
  for (const ResourceAmount& in : p.inputs) {
    const Quantity have = inventory_[in.resource];
    if (have < in.qty) {
      return RuleError{RuleViolation::InsufficientInventory,
                       "not enough " + displayName(instance_->resources[in.resource]) + " (need " +
                           std::to_string(in.qty) + ", have " + std::to_string(have) + ")"};
    }
  }
  return std::nullopt;
}

void SimState::start(ProcessIndex process) {
  const Process& p = instance_->processes[process];
  for (const ResourceAmount& in : p.inputs) inventory_[in.resource] -= in.qty;
  ++run_counts_[process];
  const ActiveRun run{process, tick_, tick_ + p.duration};
  active_.insert(std::upper_bound(active_.begin(), active_.end(), run, runOrder), run);
}

void SimState::advanceTo(Tick t) {
  // active_ is sorted by end tick, so completed runs form a prefix.
  auto done = active_.begin();
  for (; done != active_.end() && done->end <= t; ++done) {
    for (const ResourceAmount& out : instance_->processes[done->process].outputs) {
      inventory_[out.resource] += out.qty;
    }
  }
  active_.erase(active_.begin(), done);
  tick_ = t;
}

std::optional<Tick> SimState::nextEventTick() const {
  if (active_.empty()) return std::nullopt;
  return active_.front().end;
}

Quantity SimState::inventoryValue() const { return pf::inventoryValue(*instance_, inventory_); }

bool SimState::operator==(const SimState& other) const {
  return instance_ == other.instance_ && tick_ == other.tick_ && inventory_ == other.inventory_ &&
         active_ == other.active_ && run_counts_ == other.run_counts_;
}

}  // namespace pf
