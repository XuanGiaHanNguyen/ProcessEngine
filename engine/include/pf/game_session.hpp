#pragma once

#include "pf/model.hpp"
#include "pf/rules.hpp"
#include "pf/solver.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace pf {

/// A player move in Play mode.
struct Action {
  enum class Type : std::uint8_t {
    StartRun,   ///< start `process` at the current tick
    Wait,       ///< advance one tick
    AdvanceTo,  ///< advance to `tick` (> current tick, <= horizon)
    Finish,     ///< advance to the horizon and end the game
  };
  Type type = Type::Wait;
  ProcessIndex process = 0;  ///< StartRun only
  Tick tick = 0;             ///< AdvanceTo only

  static Action startRun(ProcessIndex p) { return {Type::StartRun, p, 0}; }
  static Action wait() { return {Type::Wait, 0, 0}; }
  static Action advanceTo(Tick t) { return {Type::AdvanceTo, 0, t}; }
  static Action finish() { return {Type::Finish, 0, 0}; }

  bool operator==(const Action&) const = default;
};

/// Stable machine-readable name of an action type, e.g. "start_run".
[[nodiscard]] const char* toString(Action::Type type);

/// Why an action was rejected. `code` is a RuleViolation name (e.g. "no_free_worker") or one
/// of "at_horizon", "invalid_tick", "game_finished", "nothing_to_undo", "nothing_to_redo".
struct ActionError {
  std::string code;
  std::string message;  ///< e.g. "not enough iron (need 3, have 1)"
};

/// A run in progress as shown to the player.
struct RunInProgress {
  ProcessIndex process = 0;
  Tick start = 0;
  Tick end = 0;
  Tick remaining = 0;  ///< end - current tick

  bool operator==(const RunInProgress&) const = default;
};

/// Snapshot of a session for display (what GET /api/sessions/{id} returns as "state").
struct SessionState {
  Tick tick = 0;
  Tick horizon = 0;
  std::vector<Quantity> inventory;
  Quantity money = 0;  ///< start_money; v1 has no mid-game sales
  /// Earnings if the player starts nothing more: runs in progress complete, then everything
  /// is sold at T. Equals what finish() would return right now.
  Quantity projected_earnings = 0;
  std::vector<RunInProgress> runs_in_progress;
  std::int32_t free_workers = 0;
  std::vector<std::int32_t> run_counts;
  std::vector<Action> history;
  std::size_t redo_available = 0;
  bool finished = false;

  bool operator==(const SessionState&) const = default;
};

/// Interactive, undoable simulator for Play mode. Uses SimState, the same rule code as the
/// validator, so a manual game and a solver schedule are scored identically.
///
/// Every successful action is recorded in the history; undo() restores the exact earlier
/// state and redo() re-applies undone actions. Applying a new action clears the redo list.
/// Loading a schedule (loadSchedule) puts its actions on the redo list, so a replay is just
/// repeated redo().
///
/// The session keeps its own copy of the instance; it is deterministic: the same action
/// list always produces the same state.
class GameSession {
 public:
  /// Starts at tick 0 with the start inventory. Precondition: checkInstance(instance) is empty.
  explicit GameSession(Instance instance);

  [[nodiscard]] const Instance& instance() const { return *instance_; }
  [[nodiscard]] SessionState state() const;
  [[nodiscard]] const SimState& sim() const { return snapshots_.back().sim; }
  [[nodiscard]] const std::vector<Action>& history() const { return history_; }
  /// Undone actions, next-to-redo last.
  [[nodiscard]] const std::vector<Action>& redoStack() const { return redo_; }
  [[nodiscard]] bool finished() const { return snapshots_.back().finished; }

  /// Every action allowed right now: StartRun for each legal process, Wait (if before T),
  /// AdvanceTo(next completion, or T if nothing is running), and nothing once finished.
  [[nodiscard]] std::vector<Action> legalActions() const;

  /// Why `action` is illegal right now, or nullopt if it is legal. Never changes state.
  [[nodiscard]] std::optional<ActionError> check(const Action& action) const;

  /// Applies `action`, or returns why it is illegal (state unchanged).
  [[nodiscard]] std::optional<ActionError> apply(const Action& action);

  [[nodiscard]] std::optional<ActionError> undo();
  [[nodiscard]] std::optional<ActionError> redo();

  /// Applies Finish (advance to T, end the game) if not already finished, and returns the
  /// exported schedule scored by the validator.
  Solution finish();

  /// The runs started so far, as a normal schedule.
  [[nodiscard]] Schedule exportSchedule() const;

  /// Resets to tick 0 and queues the actions that replay `schedule` on the redo list.
  /// Returns an error (state unchanged) if the schedule is invalid for this instance.
  [[nodiscard]] std::optional<ActionError> loadSchedule(const Schedule& schedule);

  /// Back to tick 0 with an empty history and redo list.
  void restart();

  /// A suggested next action from the current state: the legal start with the best
  /// time-aware potential gain per tick, otherwise advance to the next event.
  /// (Milestone 4 will offer exact-solver hints on the residual problem.)
  [[nodiscard]] std::optional<Action> hint() const;

  /// Converts a schedule into the session actions that reproduce it.
  [[nodiscard]] static std::vector<Action> actionsFor(const Schedule& schedule);

 private:
  struct Snapshot {
    SimState sim;
    bool finished = false;
  };

  void applyUnchecked(const Action& action);

  /// Shared and immutable, so SimState's pointer to it stays valid when sessions are copied.
  std::shared_ptr<const Instance> instance_;
  std::vector<Snapshot> snapshots_;  ///< snapshots_[i] = state after history_[0..i)
  std::vector<Action> history_;
  std::vector<Action> redo_;
};

}  // namespace pf
