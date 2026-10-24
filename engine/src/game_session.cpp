#include "pf/game_session.hpp"

#include "pf/bounds.hpp"
#include "pf/solvers/branch_and_bound.hpp"
#include "pf/solvers/greedy.hpp"
#include "pf/validator.hpp"

#include <algorithm>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace pf {

const char* toString(Action::Type type) {
  switch (type) {
    case Action::Type::StartRun:
      return "start_run";
    case Action::Type::Wait:
      return "wait";
    case Action::Type::AdvanceTo:
      return "advance_to";
    case Action::Type::Finish:
      return "finish";
  }
  return "unknown";
}

GameSession::GameSession(Instance instance)
    : instance_(std::make_shared<const Instance>(std::move(instance))) {
  snapshots_.push_back({SimState(*instance_), false});
}

SessionState GameSession::state() const {
  const SimState& s = sim();
  SessionState out;
  out.tick = s.tick();
  out.horizon = instance_->horizon;
  out.inventory = s.inventory();
  out.money = instance_->start_money;
  SimState projected = s;
  projected.advanceTo(instance_->horizon);
  try {
    out.projected_earnings = projected.inventoryValue();
  } catch (const std::overflow_error&) {
    out.projected_earnings = std::numeric_limits<Quantity>::max();
  }
  for (const ActiveRun& run : s.activeRuns()) {
    out.runs_in_progress.push_back({run.process, run.start, run.end, run.end - s.tick()});
  }
  out.free_workers = s.freeWorkers();
  out.run_counts = s.runCounts();
  out.history = history_;
  out.redo_available = redo_.size();
  out.finished = finished();
  return out;
}

std::vector<Action> GameSession::legalActions() const {
  std::vector<Action> actions;
  if (finished()) return actions;
  const SimState& s = sim();
  for (ProcessIndex p = 0; p < instance_->processes.size(); ++p) {
    if (!s.checkStart(p)) actions.push_back(Action::startRun(p));
  }
  if (s.tick() < instance_->horizon) {
    actions.push_back(Action::wait());
    actions.push_back(Action::advanceTo(s.nextEventTick().value_or(instance_->horizon)));
  }
  return actions;
}

std::optional<ActionError> GameSession::check(const Action& action) const {
  if (finished()) return ActionError{"game_finished", "the game is already finished"};
  const SimState& s = sim();
  switch (action.type) {
    case Action::Type::StartRun:
      if (auto e = s.checkStart(action.process)) return ActionError{toString(e->kind), e->message};
      return std::nullopt;
    case Action::Type::Wait:
      if (s.tick() >= instance_->horizon) {
        return ActionError{"at_horizon", "already at the horizon (tick " +
                                             std::to_string(instance_->horizon) + ")"};
      }
      return std::nullopt;
    case Action::Type::AdvanceTo:
      if (action.tick <= s.tick() || action.tick > instance_->horizon) {
        return ActionError{"invalid_tick", "can only advance to a tick in (" +
                                               std::to_string(s.tick()) + ", " +
                                               std::to_string(instance_->horizon) + "]"};
      }
      return std::nullopt;
    case Action::Type::Finish:
      return std::nullopt;
  }
  return ActionError{"invalid_action", "unknown action type"};
}

void GameSession::applyUnchecked(const Action& action) {
  Snapshot next = snapshots_.back();
  switch (action.type) {
    case Action::Type::StartRun:
      next.sim.start(action.process);
      break;
    case Action::Type::Wait:
      next.sim.advanceTo(next.sim.tick() + 1);
      break;
    case Action::Type::AdvanceTo:
      next.sim.advanceTo(action.tick);
      break;
    case Action::Type::Finish:
      next.sim.advanceTo(instance_->horizon);
      next.finished = true;
      break;
  }
  snapshots_.push_back(std::move(next));
  history_.push_back(action);
}

std::optional<ActionError> GameSession::apply(const Action& action) {
  if (auto error = check(action)) return error;
  applyUnchecked(action);
  redo_.clear();
  return std::nullopt;
}

std::optional<ActionError> GameSession::undo() {
  if (history_.empty()) return ActionError{"nothing_to_undo", "there is nothing to undo"};
  redo_.push_back(history_.back());
  history_.pop_back();
  snapshots_.pop_back();
  return std::nullopt;
}

std::optional<ActionError> GameSession::redo() {
  if (redo_.empty()) return ActionError{"nothing_to_redo", "there is nothing to redo"};
  const Action action = redo_.back();
  if (auto error = check(action)) return error;  // only possible for a loaded schedule
  applyUnchecked(action);
  redo_.pop_back();
  return std::nullopt;
}

Solution GameSession::finish() {
  if (!finished()) applyUnchecked(Action::finish());
  redo_.clear();
  Solution solution;
  solution.solver_name = "manual";
  solution.runs = exportSchedule();
  const ValidationReport report = validate(*instance_, solution.runs);
  solution.valid = report.valid;
  solution.earnings = report.earnings;
  if (report.violation) solution.error = report.violation->message;
  return solution;
}

Schedule GameSession::exportSchedule() const {
  Schedule schedule;
  Tick tick = 0;
  for (const Action& a : history_) {
    switch (a.type) {
      case Action::Type::StartRun:
        schedule.push_back({a.process, tick});
        break;
      case Action::Type::Wait:
        ++tick;
        break;
      case Action::Type::AdvanceTo:
        tick = a.tick;
        break;
      case Action::Type::Finish:
        tick = instance_->horizon;
        break;
    }
  }
  return schedule;
}

std::vector<Action> GameSession::actionsFor(const Schedule& schedule) {
  std::vector<std::size_t> order(schedule.size());
  std::iota(order.begin(), order.end(), std::size_t{0});
  std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return schedule[a].start_tick < schedule[b].start_tick;
  });
  std::vector<Action> actions;
  Tick tick = 0;
  for (const std::size_t i : order) {
    if (schedule[i].start_tick > tick) {
      tick = schedule[i].start_tick;
      actions.push_back(Action::advanceTo(tick));
    }
    actions.push_back(Action::startRun(schedule[i].process));
  }
  return actions;
}

std::optional<ActionError> GameSession::loadSchedule(const Schedule& schedule) {
  const ValidationReport report = validate(*instance_, schedule);
  if (!report.valid) {
    return ActionError{report.violation ? toString(report.violation->kind) : "invalid",
                       report.violation ? report.violation->message : "invalid schedule"};
  }
  restart();
  std::vector<Action> actions = actionsFor(schedule);
  redo_.assign(actions.rbegin(), actions.rend());
  return std::nullopt;
}

void GameSession::restart() {
  snapshots_.erase(snapshots_.begin() + 1, snapshots_.end());
  history_.clear();
  redo_.clear();
}

std::optional<Hint> GameSession::hint(const std::string& solver, double time_limit_ms) const {
  if (finished()) return std::nullopt;
  const SimState& now = sim();
  Hint hint;
  hint.solver = solver;
  Schedule continuation;
  if (solver == "greedy" || solver == "greedy_potential") {
    const GreedyScore score =
        solver == "greedy" ? GreedyScore::MarketValuePerTick : GreedyScore::PotentialValuePerTick;
    const PotentialValues potential(*instance_);
    Rollout r = greedyRollout(now, score, &potential);
    continuation = std::move(r.runs);
    hint.projected_earnings = r.earnings;
  } else if (solver == "branch_and_bound") {
    SolveOptions options;
    options.time_limit_ms = time_limit_ms;
    const Deadline deadline(options);
    const DualBound bound(*instance_);
    BranchAndBoundSearch search(*instance_, bound, deadline, nullptr,
                                BranchAndBoundSolver::kDefaultMaxTranspositions);
    SearchResult r = search.run(now);
    continuation = std::move(r.runs);
    hint.projected_earnings = r.earnings;
    hint.optimal = r.completed;
  } else {
    return std::nullopt;
  }

  // Translate the first step of the continuation into a session action.
  if (!continuation.empty() && continuation.front().start_tick == now.tick()) {
    hint.action = Action::startRun(continuation.front().process);
  } else if (!continuation.empty()) {
    hint.action = Action::advanceTo(continuation.front().start_tick);
  } else {
    hint.action = Action::finish();  // nothing worth starting any more
  }
  return hint;
}

}  // namespace pf
