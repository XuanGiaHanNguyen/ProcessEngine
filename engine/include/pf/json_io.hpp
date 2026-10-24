#pragma once

#include "pf/game_session.hpp"
#include "pf/model.hpp"
#include "pf/solver.hpp"
#include "pf/validator.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

namespace pf::json {

/// Result of parsing: either a value, or every error found (each prefixed by a JSON path
/// such as "processes[2].inputs.iron").
template <typename T>
struct ParseResult {
  std::optional<T> value;
  std::vector<std::string> errors;

  [[nodiscard]] bool ok() const { return value.has_value(); }
};

/// Parses and validates an instance. Accepts the metadata fields "description", "comment",
/// and "reference_solutions" (ignored); any other unknown field is an error.
/// The result, when present, always satisfies checkInstance().
[[nodiscard]] ParseResult<Instance> parseInstance(const nlohmann::json& j);
/// Same as above, starting from JSON text (syntax errors are reported, not thrown).
[[nodiscard]] ParseResult<Instance> parseInstance(std::string_view text);

/// Serializes an instance; parseInstance(toJson(i)) == i.
[[nodiscard]] nlohmann::json toJson(const Instance& instance);

/// Parses a schedule: either an array of {"process_id", "start_tick"} or an object with
/// a "runs" array of them. Process ids are resolved against `instance`.
[[nodiscard]] ParseResult<Schedule> parseSchedule(const nlohmann::json& j,
                                                  const Instance& instance);

/// Serializes a schedule as an array of {"process_id", "start_tick"}.
[[nodiscard]] nlohmann::json scheduleToJson(const Schedule& schedule, const Instance& instance);

/// Serializes a validation report: {"valid", "earnings", "final_inventory", "violation"}.
[[nodiscard]] nlohmann::json toJson(const ValidationReport& report, const Instance& instance);

/// Serializes a solution: {"runs", "earnings", "proven_optimal", "runtime_ms",
/// "solver_name", "stats", "valid", "error"}.
[[nodiscard]] nlohmann::json toJson(const Solution& solution, const Instance& instance);

/// {"type": "start_run", "process_id": "saw"} | {"type": "wait"} |
/// {"type": "advance_to", "tick": 5} | {"type": "finish"}.
[[nodiscard]] nlohmann::json toJson(const Action& action, const Instance& instance);
[[nodiscard]] ParseResult<Action> parseAction(const nlohmann::json& j, const Instance& instance);

/// The display state of a session (resource and process ids instead of indices).
[[nodiscard]] nlohmann::json toJson(const SessionState& state, const Instance& instance);

/// A complete, restorable session: {"instance", "history", "redo"}.
[[nodiscard]] nlohmann::json sessionToJson(const GameSession& session);
/// Rebuilds a session by replaying its history (and redo list) through the rules, so a
/// tampered document cannot produce an illegal state.
[[nodiscard]] ParseResult<GameSession> parseSession(const nlohmann::json& j);

}  // namespace pf::json
