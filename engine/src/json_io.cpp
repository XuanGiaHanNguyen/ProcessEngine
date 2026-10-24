#include "pf/json_io.hpp"

#include <algorithm>
#include <limits>

namespace pf::json {

using nlohmann::json;

namespace {

/// Accumulates errors while walking a JSON document.
class Reader {
 public:
  void error(const std::string& path, const std::string& message) {
    errors_.push_back(path.empty() ? message : path + ": " + message);
  }

  [[nodiscard]] std::vector<std::string>& errors() { return errors_; }

  /// Reads an integer in [lo, hi]. Returns nullopt (and records an error) otherwise.
  template <typename Int>
  std::optional<Int> integer(const json& j, const std::string& path, Int lo, Int hi) {
    if (!j.is_number_integer()) {
      error(path, std::string("expected an integer, got ") + j.type_name());
      return std::nullopt;
    }
    if (j.is_number_unsigned() &&
        j.get<std::uint64_t>() > static_cast<std::uint64_t>(std::numeric_limits<Int>::max())) {
      error(path, "integer out of range");
      return std::nullopt;
    }
    const auto v = j.get<std::int64_t>();
    if (v < static_cast<std::int64_t>(lo) || v > static_cast<std::int64_t>(hi)) {
      error(path, "must be in [" + std::to_string(lo) + ", " + std::to_string(hi) + "], got " +
                      std::to_string(v));
      return std::nullopt;
    }
    return static_cast<Int>(v);
  }

  std::optional<std::string> string(const json& j, const std::string& path) {
    if (!j.is_string()) {
      error(path, std::string("expected a string, got ") + j.type_name());
      return std::nullopt;
    }
    return j.get<std::string>();
  }

  /// Checks that `j` is an object and that every key is in `allowed`.
  bool object(const json& j, const std::string& path, std::initializer_list<const char*> allowed) {
    if (!j.is_object()) {
      error(path, std::string("expected an object, got ") + j.type_name());
      return false;
    }
    for (const auto& [key, _] : j.items()) {
      if (std::none_of(allowed.begin(), allowed.end(), [&](const char* a) { return key == a; })) {
        error(join(path, key), "unknown field");
      }
    }
    return true;
  }

  static std::string join(const std::string& path, const std::string& key) {
    return path.empty() ? key : path + "." + key;
  }
  static std::string index(const std::string& path, std::size_t i) {
    return path + "[" + std::to_string(i) + "]";
  }

 private:
  std::vector<std::string> errors_;
};

constexpr std::int64_t kInt32Max = std::numeric_limits<std::int32_t>::max();

std::vector<ResourceAmount> readAmounts(Reader& r, const json& j, const std::string& path,
                                        const Instance& instance) {
  std::vector<ResourceAmount> amounts;
  if (!j.is_object()) {
    r.error(path, std::string("expected an object mapping resource id to quantity, got ") +
                      j.type_name());
    return amounts;
  }
  for (const auto& [key, value] : j.items()) {
    const std::string item = Reader::join(path, key);
    const auto resource = instance.findResource(key);
    if (!resource) {
      r.error(item, "unknown resource '" + key + "'");
      continue;
    }
    if (auto qty = r.integer<Quantity>(value, item, 1, std::numeric_limits<Quantity>::max())) {
      amounts.push_back({*resource, *qty});
    }
  }
  std::sort(amounts.begin(), amounts.end(), [](const ResourceAmount& a, const ResourceAmount& b) {
    return a.resource < b.resource;
  });
  return amounts;
}

void readResources(Reader& r, const json& j, Instance& instance) {
  if (!j.is_array()) {
    r.error("resources", std::string("expected an array, got ") + j.type_name());
    return;
  }
  for (std::size_t i = 0; i < j.size(); ++i) {
    const std::string path = Reader::index("resources", i);
    const json& item = j[i];
    if (!r.object(item, path, {"id", "name", "sell_price"})) continue;
    Resource res;
    if (!item.contains("id")) {
      r.error(path, "missing required field 'id'");
    } else if (auto id = r.string(item["id"], path + ".id")) {
      res.id = *id;
    }
    if (item.contains("name")) {
      if (auto name = r.string(item["name"], path + ".name")) res.name = *name;
    } else {
      res.name = res.id;
    }
    if (item.contains("sell_price")) {
      if (auto price = r.integer<Quantity>(item["sell_price"], path + ".sell_price", 0,
                                           std::numeric_limits<Quantity>::max())) {
        res.sell_price = *price;
      }
    }
    instance.resources.push_back(std::move(res));
  }
}

void readProcesses(Reader& r, const json& j, Instance& instance) {
  if (!j.is_array()) {
    r.error("processes", std::string("expected an array, got ") + j.type_name());
    return;
  }
  for (std::size_t i = 0; i < j.size(); ++i) {
    const std::string path = Reader::index("processes", i);
    const json& item = j[i];
    if (!r.object(item, path, {"id", "name", "inputs", "outputs", "duration", "max_runs"})) {
      continue;
    }
    Process p;
    if (!item.contains("id")) {
      r.error(path, "missing required field 'id'");
    } else if (auto id = r.string(item["id"], path + ".id")) {
      p.id = *id;
    }
    if (item.contains("name")) {
      if (auto name = r.string(item["name"], path + ".name")) p.name = *name;
    } else {
      p.name = p.id;
    }
    if (item.contains("inputs"))
      p.inputs = readAmounts(r, item["inputs"], path + ".inputs", instance);
    if (item.contains("outputs")) {
      p.outputs = readAmounts(r, item["outputs"], path + ".outputs", instance);
    }
    if (!item.contains("duration")) {
      r.error(path, "missing required field 'duration'");
    } else if (auto d = r.integer<Tick>(item["duration"], path + ".duration", 1, kInt32Max)) {
      p.duration = *d;
    }
    if (item.contains("max_runs") && !item["max_runs"].is_null()) {
      if (auto m = r.integer<std::int32_t>(item["max_runs"], path + ".max_runs", 0, kInt32Max)) {
        p.max_runs = m;
      }
    }
    instance.processes.push_back(std::move(p));
  }
}

}  // namespace

ParseResult<Instance> parseInstance(const json& j) {
  Reader r;
  ParseResult<Instance> result;
  if (!r.object(j, "",
                {"name", "description", "comment", "reference_solutions", "resources", "processes",
                 "start_inventory", "workers", "horizon", "start_money"})) {
    result.errors = std::move(r.errors());
    return result;
  }

  Instance instance;
  if (j.contains("name")) {
    if (auto name = r.string(j["name"], "name")) instance.name = *name;
  }
  if (j.contains("resources")) {
    readResources(r, j["resources"], instance);
  } else {
    r.error("", "missing required field 'resources'");
  }
  if (j.contains("processes")) {
    readProcesses(r, j["processes"], instance);
  } else {
    r.error("", "missing required field 'processes'");
  }

  instance.start_inventory.assign(instance.resources.size(), 0);
  if (j.contains("start_inventory")) {
    for (const ResourceAmount& a :
         readAmounts(r, j["start_inventory"], "start_inventory", instance)) {
      instance.start_inventory[a.resource] = a.qty;
    }
  }
  if (!j.contains("workers")) {
    r.error("", "missing required field 'workers'");
  } else if (auto w = r.integer<std::int32_t>(j["workers"], "workers", 1, kInt32Max)) {
    instance.workers = *w;
  }
  if (!j.contains("horizon")) {
    r.error("", "missing required field 'horizon'");
  } else if (auto h = r.integer<Tick>(j["horizon"], "horizon", 0, kInt32Max)) {
    instance.horizon = *h;
  }
  if (j.contains("start_money")) {
    if (auto m = r.integer<Quantity>(j["start_money"], "start_money", 0,
                                     std::numeric_limits<Quantity>::max())) {
      instance.start_money = *m;
    }
  }

  // Structural checks (limits, duplicate ids, ...) only make sense on a complete parse.
  if (r.errors().empty()) {
    for (const std::string& e : checkInstance(instance)) r.error("", e);
  }
  result.errors = std::move(r.errors());
  if (result.errors.empty()) result.value = std::move(instance);
  return result;
}

ParseResult<Instance> parseInstance(std::string_view text) {
  json j = json::parse(text, nullptr, /*allow_exceptions=*/false, /*ignore_comments=*/true);
  if (j.is_discarded()) {
    ParseResult<Instance> result;
    result.errors.emplace_back("invalid JSON syntax");
    return result;
  }
  return parseInstance(j);
}

namespace {

json amountsToJson(const std::vector<ResourceAmount>& amounts, const Instance& instance) {
  json out = json::object();
  for (const ResourceAmount& a : amounts) out[instance.resources[a.resource].id] = a.qty;
  return out;
}

}  // namespace

json toJson(const Instance& instance) {
  json resources = json::array();
  for (const Resource& r : instance.resources) {
    resources.push_back({{"id", r.id}, {"name", r.name}, {"sell_price", r.sell_price}});
  }
  json processes = json::array();
  for (const Process& p : instance.processes) {
    json item = {{"id", p.id},
                 {"name", p.name},
                 {"inputs", amountsToJson(p.inputs, instance)},
                 {"outputs", amountsToJson(p.outputs, instance)},
                 {"duration", p.duration}};
    if (p.max_runs) item["max_runs"] = *p.max_runs;
    processes.push_back(std::move(item));
  }
  json inventory = json::object();
  for (std::size_t i = 0; i < instance.resources.size(); ++i) {
    if (instance.start_inventory[i] != 0) {
      inventory[instance.resources[i].id] = instance.start_inventory[i];
    }
  }
  return {{"name", instance.name},
          {"resources", std::move(resources)},
          {"processes", std::move(processes)},
          {"start_inventory", std::move(inventory)},
          {"workers", instance.workers},
          {"horizon", instance.horizon},
          {"start_money", instance.start_money}};
}

ParseResult<Schedule> parseSchedule(const json& j, const Instance& instance) {
  Reader r;
  ParseResult<Schedule> result;
  const json* runs = &j;
  std::string base;
  if (j.is_object()) {
    if (!j.contains("runs")) {
      result.errors.emplace_back("expected an array of runs or an object with a 'runs' array");
      return result;
    }
    runs = &j["runs"];
    base = "runs";
  }
  if (!runs->is_array()) {
    result.errors.push_back((base.empty() ? "" : base + ": ") + "expected an array, got " +
                            runs->type_name());
    return result;
  }

  Schedule schedule;
  for (std::size_t i = 0; i < runs->size(); ++i) {
    const std::string path = Reader::index(base, i);
    const json& item = (*runs)[i];
    if (!r.object(item, path, {"process_id", "start_tick"})) continue;
    ScheduledRun run;
    bool ok = true;
    if (!item.contains("process_id")) {
      r.error(path, "missing required field 'process_id'");
      ok = false;
    } else if (auto id = r.string(item["process_id"], path + ".process_id")) {
      if (auto p = instance.findProcess(*id)) {
        run.process = *p;
      } else {
        r.error(path + ".process_id", "unknown process '" + *id + "'");
        ok = false;
      }
    } else {
      ok = false;
    }
    if (!item.contains("start_tick")) {
      r.error(path, "missing required field 'start_tick'");
      ok = false;
    } else if (auto t = r.integer<Tick>(item["start_tick"], path + ".start_tick",
                                        std::numeric_limits<Tick>::min(), kInt32Max)) {
      run.start_tick = *t;
    } else {
      ok = false;
    }
    if (ok) schedule.push_back(run);
  }
  result.errors = std::move(r.errors());
  if (result.errors.empty()) result.value = std::move(schedule);
  return result;
}

json scheduleToJson(const Schedule& schedule, const Instance& instance) {
  json out = json::array();
  for (const ScheduledRun& run : schedule) {
    out.push_back(
        {{"process_id", instance.processes.at(run.process).id}, {"start_tick", run.start_tick}});
  }
  return out;
}

json toJson(const ValidationReport& report, const Instance& instance) {
  json out = {{"valid", report.valid}, {"earnings", report.earnings}};
  if (report.valid) {
    json inventory = json::object();
    for (std::size_t i = 0; i < report.final_inventory.size(); ++i) {
      inventory[instance.resources[i].id] = report.final_inventory[i];
    }
    out["final_inventory"] = std::move(inventory);
  }
  if (report.violation) {
    out["violation"] = {{"kind", toString(report.violation->kind)},
                        {"run_index", report.violation->run_index},
                        {"tick", report.violation->tick},
                        {"message", report.violation->message}};
  } else {
    out["violation"] = nullptr;
  }
  return out;
}

}  // namespace pf::json
