#include "pf/factory/json.hpp"

#include <algorithm>
#include <initializer_list>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace pf::factory::json {

using Json = nlohmann::json;

namespace {

/// Collects errors while reading fields, each prefixed with its JSON path.
class Reader {
 public:
  std::vector<std::string> errors;

  void fail(const std::string& path, const std::string& message) {
    errors.push_back(path + ": " + message);
  }

  bool object(const Json& j, const std::string& path) {
    if (j.is_object()) return true;
    fail(path, "must be an object");
    return false;
  }

  bool array(const Json& j, const std::string& path) {
    if (j.is_array()) return true;
    fail(path, "must be an array");
    return false;
  }

  void onlyKeys(const Json& j, const std::string& path, std::initializer_list<const char*> keys) {
    for (const auto& [key, value] : j.items()) {
      if (std::none_of(keys.begin(), keys.end(), [&](const char* k) { return key == k; })) {
        fail(path, "unknown field \"" + key + "\"");
      }
    }
  }

  std::string str(const Json& j, const char* key, const std::string& path, bool required,
                  std::string fallback = {}) {
    if (!j.contains(key)) {
      if (required) fail(path, std::string("missing \"") + key + "\"");
      return fallback;
    }
    if (!j[key].is_string()) {
      fail(path + "." + key, "must be a string");
      return fallback;
    }
    return j[key].get<std::string>();
  }

  std::int64_t integer(const Json& j, const char* key, const std::string& path, bool required,
                       std::int64_t fallback = 0) {
    if (!j.contains(key)) {
      if (required) fail(path, std::string("missing \"") + key + "\"");
      return fallback;
    }
    if (!j[key].is_number_integer()) {
      fail(path + "." + key, "must be an integer");
      return fallback;
    }
    const std::int64_t v = j[key].get<std::int64_t>();
    if (v < -1'000'000'000'000LL || v > 1'000'000'000'000LL) {
      fail(path + "." + key, "out of range");
      return fallback;
    }
    return v;
  }
};

int small(std::int64_t v) {
  return static_cast<int>(std::clamp<std::int64_t>(v, -1'000'000'000, 1'000'000'000));
}

std::vector<Amount> amounts(Reader& rd, const Json& j, const std::string& path,
                            const std::vector<Resource>& resources) {
  std::vector<Amount> out;
  if (!rd.object(j, path)) return out;
  for (const auto& [id, qty] : j.items()) {
    auto it = std::find_if(resources.begin(), resources.end(),
                           [&](const Resource& r) { return r.id == id; });
    if (it == resources.end()) {
      rd.fail(path + "." + id, "unknown resource");
      continue;
    }
    if (!qty.is_number_integer()) {
      rd.fail(path + "." + id, "must be an integer");
      continue;
    }
    out.push_back({static_cast<Index>(it - resources.begin()), small(qty.get<std::int64_t>())});
  }
  std::sort(out.begin(), out.end(),
            [](const Amount& a, const Amount& b) { return a.resource < b.resource; });
  return out;
}

std::optional<Index> resourceRef(Reader& rd, const Json& j, const char* key,
                                 const std::string& path, const Level& level) {
  const std::string id = rd.str(j, key, path, true);
  if (id.empty()) return std::nullopt;
  auto r = level.findResource(id);
  if (!r) rd.fail(path + "." + key, "unknown resource \"" + id + "\"");
  return r;
}

ResourceKind kindOf(Reader& rd, const std::string& kind, const std::string& path) {
  if (kind == "raw") return ResourceKind::Raw;
  if (kind == "part") return ResourceKind::Part;
  if (kind == "product") return ResourceKind::Product;
  rd.fail(path, "kind must be raw, part or product");
  return ResourceKind::Raw;
}

std::string_view statusName(MachineStatus s) {
  switch (s) {
    case MachineStatus::Busy:
      return "busy";
    case MachineStatus::Blocked:
      return "blocked";
    case MachineStatus::Starved:
      return "starved";
  }
  return "starved";
}

Json perResource(const std::vector<std::int64_t>& values, const Level& level) {
  Json out = Json::object();
  for (Index r = 0; r < values.size(); ++r) {
    if (values[r] != 0) out[level.resources[r].id] = values[r];
  }
  return out;
}

}  // namespace

pf::json::ParseResult<Level> parseLevel(const Json& j) {
  Reader rd;
  Level level;
  if (!rd.object(j, "level")) return {std::nullopt, rd.errors};
  rd.onlyKeys(j, "level",
              {"format", "id", "name", "description", "comment", "hours", "belt_capacity",
               "buffer_batches", "budget", "storage", "resources", "suppliers", "machine_types",
               "market", "slots", "objectives", "reference_designs"});
  if (rd.str(j, "format", "level", true) != "processforge-factory/1") {
    rd.fail("level.format", "must be \"processforge-factory/1\"");
  }
  level.id = rd.str(j, "id", "level", true);
  level.name = rd.str(j, "name", "level", false, level.id);
  level.description = rd.str(j, "description", "level", false);
  level.hours = small(rd.integer(j, "hours", "level", false, 24));
  level.belt_capacity = small(rd.integer(j, "belt_capacity", "level", false, 10));
  level.buffer_batches = small(rd.integer(j, "buffer_batches", "level", false, 2));
  if (j.contains("budget") && !j["budget"].is_null())
    level.budget = rd.integer(j, "budget", "level", true);
  if (j.contains("storage") && !j["storage"].is_null())
    level.storage = small(rd.integer(j, "storage", "level", true));

  if (j.contains("resources") && rd.array(j["resources"], "resources")) {
    for (Index i = 0; i < j["resources"].size(); ++i) {
      const Json& r = j["resources"][i];
      const std::string path = "resources[" + std::to_string(i) + "]";
      if (!rd.object(r, path)) continue;
      rd.onlyKeys(r, path, {"id", "name", "kind"});
      Resource res;
      res.id = rd.str(r, "id", path, true);
      res.name = rd.str(r, "name", path, false, res.id);
      res.kind = kindOf(rd, rd.str(r, "kind", path, false, "raw"), path + ".kind");
      level.resources.push_back(std::move(res));
    }
  } else if (!j.contains("resources")) {
    rd.fail("level", "missing \"resources\"");
  }

  if (j.contains("suppliers") && rd.array(j["suppliers"], "suppliers")) {
    for (Index i = 0; i < j["suppliers"].size(); ++i) {
      const Json& s = j["suppliers"][i];
      const std::string path = "suppliers[" + std::to_string(i) + "]";
      if (!rd.object(s, path)) continue;
      rd.onlyKeys(s, path, {"id", "resource", "max_rate", "unit_cost", "x", "y"});
      Supplier sup;
      sup.id = rd.str(s, "id", path, true);
      if (auto r = resourceRef(rd, s, "resource", path, level)) sup.resource = *r;
      sup.max_rate = small(rd.integer(s, "max_rate", path, true));
      sup.unit_cost = rd.integer(s, "unit_cost", path, true);
      level.suppliers.push_back(std::move(sup));
    }
  }

  if (j.contains("machine_types") && rd.array(j["machine_types"], "machine_types")) {
    for (Index i = 0; i < j["machine_types"].size(); ++i) {
      const Json& t = j["machine_types"][i];
      const std::string path = "machine_types[" + std::to_string(i) + "]";
      if (!rd.object(t, path)) continue;
      rd.onlyKeys(t, path, {"id", "name", "cost", "recipes"});
      MachineType type;
      type.id = rd.str(t, "id", path, true);
      type.name = rd.str(t, "name", path, false, type.id);
      type.cost = rd.integer(t, "cost", path, true);
      if (t.contains("recipes") && rd.array(t["recipes"], path + ".recipes")) {
        for (Index k = 0; k < t["recipes"].size(); ++k) {
          const Json& r = t["recipes"][k];
          const std::string rpath = path + ".recipes[" + std::to_string(k) + "]";
          if (!rd.object(r, rpath)) continue;
          rd.onlyKeys(r, rpath, {"id", "name", "inputs", "outputs", "hours"});
          Recipe rec;
          rec.id = rd.str(r, "id", rpath, true);
          rec.name = rd.str(r, "name", rpath, false, rec.id);
          rec.inputs =
              amounts(rd, r.value("inputs", Json::object()), rpath + ".inputs", level.resources);
          rec.outputs =
              amounts(rd, r.value("outputs", Json::object()), rpath + ".outputs", level.resources);
          rec.hours = small(rd.integer(r, "hours", rpath, true));
          type.recipes.push_back(std::move(rec));
        }
      }
      level.machine_types.push_back(std::move(type));
    }
  }

  if (j.contains("market") && rd.object(j["market"], "market")) {
    const Json& m = j["market"];
    rd.onlyKeys(m, "market", {"x", "y", "listings"});
    if (m.contains("listings") && rd.array(m["listings"], "market.listings")) {
      for (Index i = 0; i < m["listings"].size(); ++i) {
        const Json& l = m["listings"][i];
        const std::string path = "market.listings[" + std::to_string(i) + "]";
        if (!rd.object(l, path)) continue;
        rd.onlyKeys(l, path, {"resource", "price", "demand", "overflow_price"});
        Listing listing;
        if (auto r = resourceRef(rd, l, "resource", path, level)) listing.resource = *r;
        listing.price = rd.integer(l, "price", path, true);
        listing.demand = small(rd.integer(l, "demand", path, true));
        listing.overflow_price = rd.integer(l, "overflow_price", path, false, listing.price);
        level.listings.push_back(listing);
      }
    }
  }

  if (j.contains("slots") && rd.array(j["slots"], "slots")) {
    for (Index i = 0; i < j["slots"].size(); ++i) {
      const Json& s = j["slots"][i];
      const std::string path = "slots[" + std::to_string(i) + "]";
      if (!rd.object(s, path)) continue;
      rd.onlyKeys(s, path, {"id", "x", "y"});
      level.slots.push_back({rd.str(s, "id", path, true)});
    }
  }

  if (rd.errors.empty()) {
    for (std::string& e : checkLevel(level)) rd.errors.push_back(std::move(e));
  }
  if (!rd.errors.empty()) return {std::nullopt, rd.errors};
  return {std::move(level), {}};
}

DesignParse parseDesign(const Json& j, const Level& level) {
  DesignParse result;
  auto add = [&](std::string code, std::string message) {
    result.errors.push_back({std::move(code), std::move(message)});
  };
  if (!j.is_object()) {
    add("bad_design", "a design must be an object");
    return result;
  }
  for (const auto& [key, value] : j.items()) {
    if (key != "orders" && key != "machines" && key != "belts" && key != "level") {
      add("bad_design", "unknown field \"" + key + "\"");
    }
  }
  Design d = emptyDesign(level);

  const Json orders = j.value("orders", Json::object());
  if (!orders.is_object()) {
    add("bad_design", "orders must be an object");
  } else {
    for (const auto& [id, q] : orders.items()) {
      auto s = level.findSupplier(id);
      if (!s) {
        add("unknown_supplier", "no supplier \"" + id + "\"");
      } else if (!q.is_number_integer()) {
        add("bad_design", "order for " + id + " must be an integer");
      } else {
        d.orders[*s] = small(q.get<std::int64_t>());
      }
    }
  }

  const Json machines = j.value("machines", Json::array());
  if (!machines.is_array()) {
    add("bad_design", "machines must be an array");
  } else {
    for (const Json& m : machines) {
      if (!m.is_object() || !m.contains("slot") || !m["slot"].is_string() || !m.contains("type") ||
          !m["type"].is_string() || !m.contains("recipe") || !m["recipe"].is_string()) {
        add("bad_design", R"(each machine needs string "slot", "type" and "recipe")");
        continue;
      }
      const std::string slot_id = m["slot"].get<std::string>();
      const std::string type_id = m["type"].get<std::string>();
      const std::string recipe_id = m["recipe"].get<std::string>();
      auto slot = level.findSlot(slot_id);
      auto type = level.findMachineType(type_id);
      if (!slot) {
        add("unknown_slot", "no slot \"" + slot_id + "\"");
        continue;
      }
      if (!type) {
        add("unknown_machine_type", slot_id + ": no machine type \"" + type_id + "\"");
        continue;
      }
      const auto& recipes = level.machine_types[*type].recipes;
      auto it = std::find_if(recipes.begin(), recipes.end(),
                             [&](const Recipe& r) { return r.id == recipe_id; });
      if (it == recipes.end()) {
        add("unknown_recipe", slot_id + ": " + type_id + " has no recipe \"" + recipe_id + "\"");
        continue;
      }
      d.machines.push_back({*slot, *type, static_cast<Index>(it - recipes.begin())});
    }
  }

  const Json belts = j.value("belts", Json::array());
  if (!belts.is_array()) {
    add("bad_design", "belts must be an array");
  } else {
    auto node = [&](const std::string& id) -> std::optional<NodeRef> {
      if (id == "market") return NodeRef{NodeKind::Market, 0};
      if (auto s = level.findSupplier(id)) return NodeRef{NodeKind::Supplier, *s};
      if (auto s = level.findSlot(id)) return NodeRef{NodeKind::Slot, *s};
      return std::nullopt;
    };
    for (const Json& b : belts) {
      if (!b.is_object() || !b.contains("from") || !b["from"].is_string() || !b.contains("to") ||
          !b["to"].is_string() || !b.contains("resource") || !b["resource"].is_string()) {
        add("bad_design", R"(each belt needs string "from", "to" and "resource")");
        continue;
      }
      const std::string from = b["from"].get<std::string>();
      const std::string to = b["to"].get<std::string>();
      const std::string res = b["resource"].get<std::string>();
      auto f = node(from);
      auto t = node(to);
      auto r = level.findResource(res);
      if (!f || !t) {
        add("unknown_node", "belt " + from + " -> " + to + ": unknown node");
        continue;
      }
      if (!r) {
        add("belt_wrong_resource",
            "belt " + from + " -> " + to + ": unknown resource \"" + res + "\"");
        continue;
      }
      int weight = 1;
      if (b.contains("weight")) {
        if (!b["weight"].is_number_integer()) {
          add("weight_out_of_range", "belt " + from + " -> " + to + ": weight must be an integer");
          continue;
        }
        weight = small(b["weight"].get<std::int64_t>());
      }
      d.belts.push_back({*f, *t, *r, weight});
    }
  }

  if (result.errors.empty()) result.errors = checkDesign(level, d);
  if (result.errors.empty()) result.design = std::move(d);
  return result;
}

Json designToJson(const Design& design, const Level& level) {
  Json orders = Json::object();
  for (Index s = 0; s < level.suppliers.size() && s < design.orders.size(); ++s) {
    if (design.orders[s] != 0) orders[level.suppliers[s].id] = design.orders[s];
  }
  Json machines = Json::array();
  for (const PlacedMachine& m : design.machines) {
    const MachineType& t = level.machine_types[m.type];
    machines.push_back(
        {{"slot", level.slots[m.slot].id}, {"type", t.id}, {"recipe", t.recipes[m.recipe].id}});
  }
  Json belts = Json::array();
  for (const Belt& b : design.belts) {
    belts.push_back({{"from", nodeId(level, b.from)},
                     {"to", nodeId(level, b.to)},
                     {"resource", level.resources[b.resource].id},
                     {"weight", b.weight}});
  }
  return {{"orders", orders}, {"machines", machines}, {"belts", belts}};
}

Json issuesToJson(const std::vector<DesignIssue>& issues) {
  Json out = Json::array();
  for (const DesignIssue& i : issues) out.push_back({{"code", i.code}, {"message", i.message}});
  return out;
}

Json toJson(const SimResult& result, const Level& level, const Design& design) {
  Json hours = Json::array();
  for (const HourReport& h : result.hours) {
    Json machines = Json::array();
    for (Index m = 0; m < h.machines.size(); ++m) {
      const MachineHour& mh = h.machines[m];
      const Recipe& rec = recipeOf(level, design.machines[m]);
      Json in = Json::object();
      for (Index k = 0; k < rec.inputs.size() && k < mh.inputs.size(); ++k) {
        in[level.resources[rec.inputs[k].resource].id] = mh.inputs[k];
      }
      Json out = Json::object();
      for (Index k = 0; k < rec.outputs.size() && k < mh.outputs.size(); ++k) {
        out[level.resources[rec.outputs[k].resource].id] = mh.outputs[k];
      }
      Json missing = Json::array();
      for (Index r : mh.missing) missing.push_back(level.resources[r].id);
      machines.push_back({{"slot", level.slots[design.machines[m].slot].id},
                          {"status", statusName(mh.status)},
                          {"progress", mh.progress},
                          {"total", mh.total},
                          {"missing", missing},
                          {"inputs", in},
                          {"outputs", out},
                          {"batches_done", mh.batches_done}});
    }
    Json stock = Json::object();
    for (Index s = 0; s < h.stock.size(); ++s) stock[level.suppliers[s].id] = h.stock[s];
    Json delivered = Json::object();
    for (Index s = 0; s < h.delivered.size(); ++s)
      delivered[level.suppliers[s].id] = h.delivered[s];
    hours.push_back({{"hour", h.hour},
                     {"revenue", h.revenue},
                     {"purchases", h.purchases},
                     {"profit", h.profit},
                     {"sold", perResource(h.sold, level)},
                     {"machines", machines},
                     {"belts", h.belt_moved},
                     {"stock", stock},
                     {"delivered", delivered}});
  }
  return {{"profit", result.profit},
          {"sales", result.sales},
          {"purchases", result.purchases},
          {"build", result.build},
          {"sold", perResource(result.sold, level)},
          {"hours", hours}};
}

}  // namespace pf::factory::json
