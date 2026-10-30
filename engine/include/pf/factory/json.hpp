#pragma once

#include "pf/factory/level.hpp"
#include "pf/factory/sim.hpp"
#include "pf/json_io.hpp"

#include <optional>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

/// JSON for Factory mode, in the formats of docs/factory-spec.md.
namespace pf::factory::json {

/// Parses and checks a level ("processengine-factory/1"). Layout hints (x, y), "objectives"
/// and "reference_designs" are metadata and are not part of the returned Level. The result,
/// when present, satisfies checkLevel().
[[nodiscard]] pf::json::ParseResult<Level> parseLevel(const nlohmann::json& j);

/// A parsed design, or every problem found. Id-resolution problems use the spec's codes
/// (unknown_supplier, unknown_slot, ...); the rest come from checkDesign().
struct DesignParse {
  std::optional<Design> design;
  std::vector<DesignIssue> errors;
};

/// Parses a design against a level, then runs checkDesign() on it.
[[nodiscard]] DesignParse parseDesign(const nlohmann::json& j, const Level& level);

/// Serializes a design with ids; parseDesign(designToJson(d, l), l) gives back d.
[[nodiscard]] nlohmann::json designToJson(const Design& design, const Level& level);

/// [{"code", "message"}, ...]
[[nodiscard]] nlohmann::json issuesToJson(const std::vector<DesignIssue>& issues);

/// {"profit", "sales", "purchases", "build", "sold": {id: n}, "hours": [...]} with ids for
/// resources, machines (by slot), belts (by design index) and suppliers.
[[nodiscard]] nlohmann::json toJson(const SimResult& result, const Level& level,
                                    const Design& design);

}  // namespace pf::factory::json
