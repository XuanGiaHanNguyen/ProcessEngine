#pragma once

#include "pf/model.hpp"
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

}  // namespace pf::json
