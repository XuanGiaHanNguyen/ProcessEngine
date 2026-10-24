#include "pf/json_io.hpp"

#include <algorithm>
#include <limits>
#include <ranges>

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

json toJson(const Solution& solution, const Instance& instance) {
  json out = {{"runs", scheduleToJson(solution.runs, instance)},
              {"earnings", solution.earnings},
              {"proven_optimal", solution.proven_optimal},
              {"runtime_ms", solution.runtime_ms},
              {"solver_name", solution.solver_name},
              {"stats", solution.stats},
              {"valid", solution.valid}};
  out["error"] = solution.error ? json(*solution.error) : json(nullptr);
  return out;
}

json toJson(const Action& action, const Instance& instance) {
  json out = {{"type", toString(action.type)}};
  if (action.type == Action::Type::StartRun) {
    out["process_id"] = action.process < instance.processes.size()
                            ? json(instance.processes[action.process].id)
                            : json(nullptr);
  }
  if (action.type == Action::Type::AdvanceTo) out["tick"] = action.tick;
  return out;
}

ParseResult<Action> parseAction(const json& j, const Instance& instance) {
  Reader r;
  ParseResult<Action> result;
  if (!r.object(j, "", {"type", "process_id", "processId", "tick"})) {
    result.errors = std::move(r.errors());
    return result;
  }
  const std::optional<std::string> type =
      j.contains("type") ? r.string(j["type"], "type") : std::nullopt;
  if (!j.contains("type")) r.error("", "missing required field 'type'");
  Action action;
  if (type == "start_run") {
    // Accept both snake_case (engine) and camelCase (API body) spellings.
    const char* key = j.contains("process_id") ? "process_id" : "processId";
    if (!j.contains(key)) {
      r.error("", "start_run requires 'process_id'");
    } else if (auto id = r.string(j[key], key)) {
      if (auto p = instance.findProcess(*id)) {
        action = Action::startRun(*p);
      } else {
        r.error(key, "unknown process '" + *id + "'");
      }
    }
  } else if (type == "wait") {
    action = Action::wait();
  } else if (type == "advance_to") {
    if (!j.contains("tick")) {
      r.error("", "advance_to requires 'tick'");
    } else if (auto t = r.integer<Tick>(j["tick"], "tick", 0, kInt32Max)) {
      action = Action::advanceTo(*t);
    }
  } else if (type == "finish") {
    action = Action::finish();
  } else if (type) {
    r.error("type",
            "unknown action type '" + *type + "' (expected start_run, wait, advance_to or finish)");
  }
  result.errors = std::move(r.errors());
  if (result.errors.empty()) result.value = action;
  return result;
}

json toJson(const SessionState& state, const Instance& instance) {
  json inventory = json::object();
  for (std::size_t i = 0; i < state.inventory.size(); ++i) {
    inventory[instance.resources[i].id] = state.inventory[i];
  }
  json runs = json::array();
  for (const RunInProgress& run : state.runs_in_progress) {
    runs.push_back({{"process_id", instance.processes[run.process].id},
                    {"start", run.start},
                    {"end", run.end},
                    {"remaining", run.remaining}});
  }
  json counts = json::object();
  for (std::size_t p = 0; p < state.run_counts.size(); ++p) {
    counts[instance.processes[p].id] = state.run_counts[p];
  }
  json history = json::array();
  for (const Action& a : state.history) history.push_back(toJson(a, instance));
  return {{"tick", state.tick},
          {"horizon", state.horizon},
          {"inventory", std::move(inventory)},
          {"money", state.money},
          {"projected_earnings", state.projected_earnings},
          {"runs_in_progress", std::move(runs)},
          {"free_workers", state.free_workers},
          {"workers", instance.workers},
          {"run_counts", std::move(counts)},
          {"history", std::move(history)},
          {"redo_available", state.redo_available},
          {"finished", state.finished}};
}

json sessionToJson(const GameSession& session) {
  const Instance& instance = session.instance();
  json history = json::array();
  for (const Action& a : session.history()) history.push_back(toJson(a, instance));
  json redo = json::array();  // next action to redo first
  for (const Action& a : std::views::reverse(session.redoStack())) {
    redo.push_back(toJson(a, instance));
  }
  return {
      {"instance", toJson(instance)}, {"history", std::move(history)}, {"redo", std::move(redo)}};
}

ParseResult<GameSession> parseSession(const json& j) {
  ParseResult<GameSession> result;
  if (!j.is_object() || !j.contains("instance")) {
    result.errors.emplace_back("expected an object with an 'instance' field");
    return result;
  }
  auto instance = parseInstance(j["instance"]);
  if (!instance.value) {
    for (const std::string& e : instance.errors) result.errors.push_back("instance: " + e);
    return result;
  }
  GameSession session(std::move(*instance.value));

  // Replay history, then the redo list, then undo the redo part to restore it.
  std::size_t redo_count = 0;
  for (const char* field : {"history", "redo"}) {
    if (!j.contains(field)) continue;
    const json& actions = j[field];
    if (!actions.is_array()) {
      result.errors.push_back(std::string(field) + ": expected an array");
      return result;
    }
    for (std::size_t i = 0; i < actions.size(); ++i) {
      const std::string path = std::string(field) + "[" + std::to_string(i) + "]";
      auto action = parseAction(actions[i], session.instance());
      if (!action.value) {
        for (const std::string& e : action.errors) result.errors.push_back(path + ": " + e);
        return result;
      }
      if (auto error = session.apply(*action.value)) {
        result.errors.push_back(path + ": illegal action: " + error->message);
        return result;
      }
    }
    if (std::string_view(field) == "redo") redo_count = actions.size();
  }
  for (std::size_t i = 0; i < redo_count; ++i) (void)session.undo();
  result.value = std::move(session);
  return result;
}

json toJson(const Timeline& timeline, const Instance& instance) {
  json runs = json::array();
  for (const TimelineRun& r : timeline.runs) {
    runs.push_back({{"run_index", r.run_index},
                    {"process_id", instance.processes[r.process].id},
                    {"start", r.start},
                    {"end", r.end},
                    {"slot", r.slot}});
  }
  json points = json::array();
  for (const TimelinePoint& p : timeline.points) {
    json inventory = json::object();
    for (std::size_t i = 0; i < p.inventory.size(); ++i) {
      inventory[instance.resources[i].id] = p.inventory[i];
    }
    points.push_back({{"tick", p.tick},
                      {"inventory", std::move(inventory)},
                      {"busy_workers", p.busy_workers},
                      {"value", p.value}});
  }
  return {
      {"earnings", timeline.earnings}, {"runs", std::move(runs)}, {"points", std::move(points)}};
}

ParseResult<GeneratorParams> parseGeneratorParams(const json& j) {
  Reader r;
  ParseResult<GeneratorParams> result;
  if (!r.object(j, "",
                {"size", "seed", "resources", "processes", "chain_depth", "workers", "horizon",
                 "min_price", "max_price", "max_qty", "max_duration"})) {
    result.errors = std::move(r.errors());
    return result;
  }
  std::uint64_t seed = 1;
  if (j.contains("seed")) {
    if (auto s = r.integer<std::int64_t>(j["seed"], "seed", 0,
                                         std::numeric_limits<std::int64_t>::max())) {
      seed = static_cast<std::uint64_t>(*s);
    }
  }
  GeneratorParams params;
  params.seed = seed;
  if (j.contains("size")) {
    const auto size = r.string(j["size"], "size");
    if (size == "small") {
      params = GeneratorParams::small(seed);
    } else if (size == "medium") {
      params = GeneratorParams::medium(seed);
    } else if (size == "large") {
      params = GeneratorParams::large(seed);
    } else if (size) {
      r.error("size", "expected small, medium or large");
    }
  }
  const auto read32 = [&](const char* key, std::int32_t lo, std::int32_t hi, std::int32_t& out) {
    if (!j.contains(key)) return;
    if (auto v = r.integer<std::int32_t>(j[key], key, lo, hi)) out = *v;
  };
  read32("resources", 1, 200, params.resources);
  read32("processes", 1, 500, params.processes);
  read32("chain_depth", 1, 20, params.chain_depth);
  read32("workers", 1, 100, params.workers);
  read32("horizon", 1, 10'000, params.horizon);
  read32("max_duration", 1, 1'000, params.max_duration);
  const auto read64 = [&](const char* key, Quantity lo, Quantity hi, Quantity& out) {
    if (!j.contains(key)) return;
    if (auto v = r.integer<Quantity>(j[key], key, lo, hi)) out = *v;
  };
  read64("min_price", 0, 10'000, params.min_price);
  read64("max_price", 0, 10'000, params.max_price);
  read64("max_qty", 1, 1'000, params.max_qty);
  if (params.min_price > params.max_price) r.error("min_price", "must be <= max_price");
  result.errors = std::move(r.errors());
  if (result.errors.empty()) result.value = params;
  return result;
}

}  // namespace pf::json
