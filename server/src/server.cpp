#include "server.hpp"

#include "pf/factory/json.hpp"
#include "pf/factory/sim.hpp"
#include "pf/factory/solvers.hpp"
#include "pf/generator.hpp"
#include "pf/json_io.hpp"
#include "pf/registry.hpp"
#include "pf/timeline.hpp"
#include "pf/validator.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <future>
#include <set>
#include <sstream>

#include "logging.hpp"

namespace pf::server {

using Json = nlohmann::json;

// --- Middleware ----------------------------------------------------------------------------

void RequestLogger::before_handle(crow::request& /*req*/, crow::response& /*res*/, context& ctx) {
  ctx.start = std::chrono::steady_clock::now();
}

void RequestLogger::after_handle(crow::request& req, crow::response& res, context& ctx) {
  const double ms =
      std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ctx.start)
          .count();
  Log::info("request", {{"method", crow::method_name(req.method)},
                        {"path", req.url},
                        {"status", res.code},
                        {"ms", ms}});
}

void Cors::after_handle(crow::request& /*req*/, crow::response& res, context& /*ctx*/) const {
  res.set_header("Access-Control-Allow-Origin", origin);
  res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
  res.set_header("Access-Control-Allow-Headers", "Content-Type");
  res.set_header("Access-Control-Max-Age", "600");
  if (origin != "*") res.set_header("Vary", "Origin");
}

namespace {

// --- Response helpers ----------------------------------------------------------------------

crow::response jsonResponse(int status, const Json& body) {
  crow::response res(status);
  res.set_header("Content-Type", "application/json");
  res.body = body.dump();
  return res;
}

/// Every error has the same shape: {"error": machine code, "reason": human text, ...}.
crow::response errorResponse(int status, const std::string& error, const std::string& reason,
                             const Json& extra = Json::object()) {
  Json body = {{"error", error}, {"reason", reason}};
  body.update(extra);
  return jsonResponse(status, body);
}

/// Parses a JSON request body, enforcing the size limit. On failure fills `error`.
std::optional<Json> parseBody(const crow::request& req, std::size_t max_bytes,
                              crow::response& error, bool allow_empty = false) {
  if (req.body.size() > max_bytes) {
    error = errorResponse(413, "payload_too_large",
                          "request body exceeds " + std::to_string(max_bytes) + " bytes");
    return std::nullopt;
  }
  if (req.body.empty() && allow_empty) return Json::object();
  Json body = Json::parse(req.body, nullptr, /*allow_exceptions=*/false);
  if (body.is_discarded()) {
    error = errorResponse(400, "invalid_json", "request body is not valid JSON");
    return std::nullopt;
  }
  if (!body.is_object()) {
    error = errorResponse(400, "invalid_request", "request body must be a JSON object");
    return std::nullopt;
  }
  return body;
}

std::optional<Instance> parseInstanceField(const Json& j, crow::response& error) {
  auto parsed = json::parseInstance(j);
  if (!parsed.value) {
    error = errorResponse(400, "invalid_instance",
                          parsed.errors.empty() ? "invalid instance" : parsed.errors.front(),
                          {{"errors", parsed.errors}});
    return std::nullopt;
  }
  return std::move(parsed.value);
}

/// {"timeLimitMs", "seed", "params"} (snake_case spellings accepted too).
std::optional<SolveOptions> parseOptions(const Json& j, crow::response& error) {
  SolveOptions options;
  if (j.is_null()) return options;
  if (!j.is_object()) {
    error = errorResponse(400, "invalid_options", "options must be an object");
    return std::nullopt;
  }
  for (const auto& [key, value] : j.items()) {
    if (key == "timeLimitMs" || key == "time_limit_ms") {
      if (!value.is_number() || value.get<double>() <= 0) {
        error = errorResponse(400, "invalid_options", "timeLimitMs must be a positive number");
        return std::nullopt;
      }
      options.time_limit_ms = value.get<double>();
    } else if (key == "seed") {
      if (!value.is_number_integer() || value.get<std::int64_t>() < 0) {
        error = errorResponse(400, "invalid_options", "seed must be a non-negative integer");
        return std::nullopt;
      }
      options.seed = value.get<std::uint64_t>();
    } else if (key == "params") {
      if (!value.is_object()) {
        error = errorResponse(400, "invalid_options", "params must be an object of numbers");
        return std::nullopt;
      }
      for (const auto& [name, v] : value.items()) {
        if (!v.is_number()) {
          error = errorResponse(400, "invalid_options", "params." + name + " must be a number");
          return std::nullopt;
        }
        options.params[name] = v.get<double>();
      }
    } else {
      error = errorResponse(400, "invalid_options", "unknown option '" + key + "'");
      return std::nullopt;
    }
  }
  return options;
}

std::optional<std::vector<std::string>> parseSolvers(const Json& j, std::size_t max,
                                                     crow::response& error) {
  if (!j.is_array() || j.empty()) {
    error = errorResponse(400, "invalid_solvers", "solvers must be a non-empty array of names");
    return std::nullopt;
  }
  if (j.size() > max) {
    error = errorResponse(400, "invalid_solvers",
                          "at most " + std::to_string(max) + " solvers per request");
    return std::nullopt;
  }
  std::vector<std::string> names;
  std::set<std::string> seen;
  for (const Json& item : j) {
    if (!item.is_string()) {
      error = errorResponse(400, "invalid_solvers", "solver names must be strings");
      return std::nullopt;
    }
    const std::string name = item.get<std::string>();
    if (SolverRegistry::builtin().find(name) == nullptr) {
      error = errorResponse(400, "unknown_solver", "unknown solver '" + name + "'");
      return std::nullopt;
    }
    if (!seen.insert(name).second) {
      error = errorResponse(400, "invalid_solvers", "solver '" + name + "' listed twice");
      return std::nullopt;
    }
    names.push_back(name);
  }
  return names;
}

Json eventJson(const JobEvent& e) {
  return {{"seq", e.seq},
          {"solver", e.solver},
          {"event", e.event},
          {"elapsedMs", e.elapsed_ms},
          {"bestEarnings", e.best_earnings},
          {"nodes", e.nodes}};
}

Json jobJson(const JobSnapshot& s, const Instance& instance) {
  Json results = Json::array();
  for (const SolverRun& run : s.runs) {
    Json r = {{"solver", run.solver}, {"status", run.status}};
    r["solution"] = run.solution ? json::toJson(*run.solution, instance) : Json(nullptr);
    results.push_back(std::move(r));
  }
  return {{"jobId", s.id},
          {"status", s.status},
          {"timeLimitMs", s.time_limit_ms},
          {"events", s.events},
          {"results", std::move(results)}};
}

Json actionsJson(const std::vector<Action>& actions, const Instance& instance) {
  Json out = Json::array();
  for (const Action& a : actions) out.push_back(json::toJson(a, instance));
  return out;
}

/// What the Play screen needs after every move: state, legal actions, and for every
/// process whether it can start now and, if not, why (for the disabled-button tooltip).
Json sessionView(const std::string& id, const GameSession& session, bool full) {
  const Instance& inst = session.instance();
  Json status = Json::array();
  for (ProcessIndex p = 0; p < inst.processes.size(); ++p) {
    Json item = {{"processId", inst.processes[p].id}, {"name", inst.processes[p].name}};
    const auto error = session.check(Action::startRun(p));
    item["legal"] = !error.has_value();
    item["code"] = error ? Json(error->code) : Json(nullptr);
    item["reason"] = error ? Json(error->message) : Json(nullptr);
    status.push_back(std::move(item));
  }
  Json view = {{"sessionId", id},
               {"state", json::toJson(session.state(), inst)},
               {"legalActions", actionsJson(session.legalActions(), inst)},
               {"processStatus", std::move(status)}};
  if (full) {
    view["instance"] = json::toJson(inst);
    std::vector<Action> redo(session.redoStack().rbegin(), session.redoStack().rend());
    view["redo"] = actionsJson(redo, inst);
  }
  return view;
}

crow::response sessionNotFound(const std::string& id) {
  return errorResponse(404, "session_not_found", "no session '" + id + "' (it may have expired)");
}

}  // namespace

// --- Server --------------------------------------------------------------------------------

Server::Server(Config config)
    : config_(std::move(config)),
      jobs_(config_.solver_threads, config_.job_limits),
      sessions_(config_.max_sessions, config_.session_ttl, config_.clock) {
  app_.loglevel(crow::LogLevel::Warning);
  app_.get_middleware<Cors>().origin = config_.cors_origin;
  loadSamples();
  registerRoutes();
  sweeper_ = std::thread([this] {
    std::unique_lock lock(sweeper_mutex_);
    while (!sweeper_cv_.wait_for(lock, config_.sweep_interval, [this] { return stopping_; })) {
      if (const std::size_t n = sessions_.sweep(); n > 0) {
        Log::info("sessions_expired", {{"count", n}, {"remaining", sessions_.size()}});
      }
    }
  });
}

Server::~Server() { shutdown(); }

void Server::loadSamples() {
  namespace fs = std::filesystem;
  if (config_.instances_dir.empty() || !fs::is_directory(config_.instances_dir)) return;
  std::vector<fs::path> files;
  for (const auto& e : fs::recursive_directory_iterator(config_.instances_dir)) {
    if (e.is_regular_file() && e.path().extension() == ".json") files.push_back(e.path());
  }
  std::sort(files.begin(), files.end());
  for (const fs::path& f : files) {
    std::ifstream in(f);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const Json doc = Json::parse(buffer.str(), nullptr, false);
    if (doc.is_object() && doc.contains("format")) {
      auto level = factory::json::parseLevel(doc);
      if (!level.value) {
        Log::warn("factory_level_skipped", {{"file", f.string()}, {"errors", level.errors}});
        continue;
      }
      factory_levels_.push_back({level.value->id, doc, std::move(*level.value)});
      continue;
    }
    auto parsed = json::parseInstance(doc);
    if (!parsed.value) {
      Log::warn("sample_skipped", {{"file", f.string()}, {"errors", parsed.errors}});
      continue;
    }
    samples_.push_back({f.stem().string(), doc.value("description", ""), std::move(*parsed.value)});
  }
  Log::info("samples_loaded", {{"count", samples_.size()},
                               {"factory_levels", factory_levels_.size()},
                               {"dir", config_.instances_dir}});
}

void Server::registerRoutes() {
  CROW_ROUTE(app_, "/api/health")([] { return jsonResponse(200, {{"status", "ok"}}); });

  CROW_ROUTE(app_, "/api/solvers")([] {
    Json out = Json::array();
    for (const SolverInfo& info : SolverRegistry::builtin().list()) {
      Json options = Json::array();
      for (const SolverOption& o : info.options) {
        options.push_back(
            {{"name", o.name}, {"description", o.description}, {"default", o.default_value}});
      }
      out.push_back({{"name", info.name},
                     {"description", info.description},
                     {"exact", info.exact},
                     {"options", std::move(options)}});
    }
    return jsonResponse(200, out);
  });

  CROW_ROUTE(app_, "/api/instances/samples")([this] {
    Json out = Json::array();
    for (const Sample& s : samples_) {
      out.push_back({{"id", s.id},
                     {"name", s.instance.name},
                     {"description", s.description},
                     {"resources", s.instance.resources.size()},
                     {"processes", s.instance.processes.size()},
                     {"workers", s.instance.workers},
                     {"horizon", s.instance.horizon}});
    }
    return jsonResponse(200, out);
  });

  CROW_ROUTE(app_, "/api/instances/samples/<string>")([this](const std::string& id) {
    for (const Sample& s : samples_) {
      if (s.id == id) return jsonResponse(200, json::toJson(s.instance));
    }
    return errorResponse(404, "sample_not_found", "no sample instance '" + id + "'");
  });

  CROW_ROUTE(app_, "/api/instances/validate")
      .methods("POST"_method)([this](const crow::request& req) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        const auto parsed = json::parseInstance(*body);
        return jsonResponse(200, {{"valid", parsed.ok()}, {"errors", parsed.errors}});
      });

  CROW_ROUTE(app_, "/api/instances/generate")
      .methods("POST"_method)([this](const crow::request& req) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error, /*allow_empty=*/true);
        if (!body) return error;
        const auto params = json::parseGeneratorParams(*body);
        if (!params.value) {
          return errorResponse(400, "invalid_generator_params", params.errors.front(),
                               {{"errors", params.errors}});
        }
        return jsonResponse(200, json::toJson(generate(*params.value).instance));
      });

  CROW_ROUTE(app_, "/api/schedules/validate")
      .methods("POST"_method)([this](const crow::request& req) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        if (!body->contains("instance") || !body->contains("schedule")) {
          return errorResponse(400, "invalid_request", "expected {instance, schedule}");
        }
        const auto instance = parseInstanceField((*body)["instance"], error);
        if (!instance) return error;
        const auto schedule = json::parseSchedule((*body)["schedule"], *instance);
        if (!schedule.value) {
          return errorResponse(400, "invalid_schedule", schedule.errors.front(),
                               {{"errors", schedule.errors}});
        }
        return jsonResponse(200, json::toJson(validate(*instance, *schedule.value), *instance));
      });

  CROW_ROUTE(app_, "/api/schedules/timeline")
      .methods("POST"_method)([this](const crow::request& req) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        if (!body->contains("instance") || !body->contains("schedule")) {
          return errorResponse(400, "invalid_request", "expected {instance, schedule}");
        }
        const auto instance = parseInstanceField((*body)["instance"], error);
        if (!instance) return error;
        const auto schedule = json::parseSchedule((*body)["schedule"], *instance);
        if (!schedule.value) {
          return errorResponse(400, "invalid_schedule", schedule.errors.front(),
                               {{"errors", schedule.errors}});
        }
        const auto timeline = buildTimeline(*instance, *schedule.value);
        if (!timeline) {
          const ValidationReport report = validate(*instance, *schedule.value);
          return errorResponse(422, report.violation ? toString(report.violation->kind) : "invalid",
                               report.violation ? report.violation->message : "invalid schedule");
        }
        return jsonResponse(200, json::toJson(*timeline, *instance));
      });

  registerSolveRoutes();
  registerSessionRoutes();
  registerFactoryRoutes();
  registerWebSocket();
}

void Server::registerSolveRoutes() {
  CROW_ROUTE(app_, "/api/solve").methods("POST"_method)([this](const crow::request& req) {
    crow::response error;
    const auto body = parseBody(req, config_.max_body_bytes, error);
    if (!body) return error;
    if (!body->contains("instance")) {
      return errorResponse(400, "invalid_request", "missing 'instance'");
    }
    auto instance = parseInstanceField((*body)["instance"], error);
    if (!instance) return error;
    auto solvers =
        parseSolvers(body->value("solvers", Json()), config_.job_limits.max_solvers_per_job, error);
    if (!solvers) return error;
    auto options = parseOptions(body->value("options", Json()), error);
    if (!options) return error;
    const auto job = jobs_.submit(std::move(*instance), std::move(*solvers), std::move(*options));
    Log::info("job_submitted", {{"jobId", job->id()}, {"solvers", job->snapshot().runs.size()}});
    return jsonResponse(202, {{"jobId", job->id()}});
  });

  CROW_ROUTE(app_, "/api/solve/<string>")
      .methods("GET"_method,
               "DELETE"_method)([this](const crow::request& req, const std::string& id) {
        const auto job = jobs_.find(id);
        if (!job) return errorResponse(404, "job_not_found", "no job '" + id + "'");
        if (req.method == "DELETE"_method) {
          job->cancel();
          Log::info("job_cancelled", {{"jobId", id}});
        }
        return jsonResponse(200, jobJson(job->snapshot(), job->instance()));
      });
}

void Server::registerSessionRoutes() {
  CROW_ROUTE(app_, "/api/sessions").methods("POST"_method)([this](const crow::request& req) {
    crow::response error;
    const auto body = parseBody(req, config_.max_body_bytes, error);
    if (!body) return error;
    std::optional<Instance> instance;
    if (body->contains("instance")) {
      instance = parseInstanceField((*body)["instance"], error);
      if (!instance) return error;
    } else if (body->contains("instanceId") && (*body)["instanceId"].is_string()) {
      const std::string sid = (*body)["instanceId"].get<std::string>();
      for (const Sample& s : samples_) {
        if (s.id == sid) instance = s.instance;
      }
      if (!instance)
        return errorResponse(404, "sample_not_found", "no sample instance '" + sid + "'");
    } else {
      return errorResponse(400, "invalid_request", "expected 'instance' or 'instanceId'");
    }
    const auto id = sessions_.create(std::move(*instance));
    if (!id) {
      return errorResponse(503, "session_limit",
                           "too many active sessions (limit " +
                               std::to_string(sessions_.capacity()) + "); try again later");
    }
    Json view;
    sessions_.with(*id, [&](GameSession& s) { view = sessionView(*id, s, true); });
    Log::info("session_created", {{"sessionId", *id}, {"active", sessions_.size()}});
    return jsonResponse(201, view);
  });

  CROW_ROUTE(app_, "/api/sessions/<string>")
      .methods("GET"_method,
               "DELETE"_method)([this](const crow::request& req, const std::string& id) {
        if (req.method == "DELETE"_method) {
          return sessions_.erase(id) ? jsonResponse(200, {{"deleted", true}}) : sessionNotFound(id);
        }
        Json view;
        if (!sessions_.with(id, [&](GameSession& s) { view = sessionView(id, s, true); })) {
          return sessionNotFound(id);
        }
        return jsonResponse(200, view);
      });

  CROW_ROUTE(app_, "/api/sessions/<string>/actions")
      .methods("POST"_method)([this](const crow::request& req, const std::string& id) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        crow::response res;
        const bool found = sessions_.with(id, [&](GameSession& s) {
          const auto action = json::parseAction(*body, s.instance());
          if (!action.value) {
            res = errorResponse(400, "invalid_action", action.errors.front(),
                                {{"errors", action.errors}});
            return;
          }
          if (action.value->type == Action::Type::Finish) {
            res = errorResponse(400, "invalid_action", "use POST /api/sessions/{id}/finish");
            return;
          }
          if (const auto illegal = s.apply(*action.value)) {
            // The state is unchanged; send it back so the client can resynchronize.
            res = errorResponse(409, illegal->code, illegal->message, sessionView(id, s, false));
            return;
          }
          res = jsonResponse(200, sessionView(id, s, false));
        });
        return found ? std::move(res) : sessionNotFound(id);
      });

  // undo / redo / restart share a shape: no body, respond with the new view.
  const auto simple = [this](const char* path,
                             std::function<std::optional<ActionError>(GameSession&)> op) {
    app_.route_dynamic(path).methods("POST"_method)(
        [this, op](const crow::request& /*req*/, std::string id) {
          crow::response res;
          const bool found = sessions_.with(id, [&](GameSession& s) {
            if (const auto e = op(s)) {
              res = errorResponse(409, e->code, e->message, sessionView(id, s, false));
            } else {
              res = jsonResponse(200, sessionView(id, s, false));
            }
          });
          return found ? std::move(res) : sessionNotFound(id);
        });
  };
  simple("/api/sessions/<string>/undo", [](GameSession& s) { return s.undo(); });
  simple("/api/sessions/<string>/redo", [](GameSession& s) { return s.redo(); });
  simple("/api/sessions/<string>/restart", [](GameSession& s) -> std::optional<ActionError> {
    s.restart();
    return std::nullopt;
  });

  CROW_ROUTE(app_, "/api/sessions/<string>/finish")
      .methods("POST"_method)([this](const crow::request& /*req*/, const std::string& id) {
        crow::response res;
        const bool found = sessions_.with(id, [&](GameSession& s) {
          const Solution solution = s.finish();
          // Re-score the exported schedule independently, as the API promises.
          const ValidationReport check = validate(s.instance(), s.exportSchedule());
          Json view = sessionView(id, s, false);
          view["solution"] = json::toJson(solution, s.instance());
          view["validated"] = check.valid && check.earnings == solution.earnings;
          res = jsonResponse(200, view);
        });
        return found ? std::move(res) : sessionNotFound(id);
      });

  CROW_ROUTE(app_, "/api/sessions/<string>/hint")
      .methods("POST"_method)([this](const crow::request& req, const std::string& id) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error, /*allow_empty=*/true);
        if (!body) return error;
        const std::string solver = body->value("solver", std::string("branch_and_bound"));
        const double limit = std::clamp(body->value("timeLimitMs", 250.0), 1.0, 2000.0);
        crow::response res;
        const bool found = sessions_.with(id, [&](GameSession& s) {
          if (s.finished()) {
            res = errorResponse(409, "game_finished", "the game is already finished");
            return;
          }
          const auto hint = s.hint(solver, limit);
          if (!hint) {
            res = errorResponse(400, "unknown_solver",
                                "hints support branch_and_bound, greedy and greedy_potential");
            return;
          }
          res = jsonResponse(200, {{"suggestedAction", json::toJson(hint->action, s.instance())},
                                   {"projectedEarnings", hint->projected_earnings},
                                   {"optimal", hint->optimal},
                                   {"solver", hint->solver}});
        });
        return found ? std::move(res) : sessionNotFound(id);
      });

  CROW_ROUTE(app_, "/api/sessions/<string>/compare")
      .methods("POST"_method)([this](const crow::request& req, const std::string& id) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        auto solvers = parseSolvers(body->value("solvers", Json()),
                                    config_.job_limits.max_solvers_per_job, error);
        if (!solvers) return error;
        auto options = parseOptions(body->value("options", Json()), error);
        if (!options) return error;
        std::optional<Instance> instance;
        Quantity player = 0;
        bool finished = false;
        if (!sessions_.with(id, [&](GameSession& s) {
              instance = s.instance();
              player = s.state().projected_earnings;
              finished = s.finished();
            })) {
          return sessionNotFound(id);
        }
        const auto job =
            jobs_.submit(std::move(*instance), std::move(*solvers), std::move(*options));
        return jsonResponse(
            202, {{"jobId", job->id()}, {"playerEarnings", player}, {"playerFinished", finished}});
      });

  CROW_ROUTE(app_, "/api/sessions/<string>/load")
      .methods("POST"_method)([this](const crow::request& req, const std::string& id) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        if (!body->contains("schedule")) {
          return errorResponse(400, "invalid_request", "missing 'schedule'");
        }
        crow::response res;
        const bool found = sessions_.with(id, [&](GameSession& s) {
          const auto schedule = json::parseSchedule((*body)["schedule"], s.instance());
          if (!schedule.value) {
            res = errorResponse(400, "invalid_schedule", schedule.errors.front(),
                                {{"errors", schedule.errors}});
            return;
          }
          if (const auto e = s.loadSchedule(*schedule.value)) {
            res = errorResponse(422, e->code, e->message);
            return;
          }
          res = jsonResponse(200, sessionView(id, s, true));
        });
        return found ? std::move(res) : sessionNotFound(id);
      });
}

void Server::registerWebSocket() {
  CROW_WEBSOCKET_ROUTE(app_, "/ws/solve/<string>")
      .onaccept([this](const crow::request& req, void** userdata) {
        static const std::string prefix = "/ws/solve/";
        if (!req.url.starts_with(prefix)) return false;
        const std::string id = req.url.substr(prefix.size());
        if (!jobs_.find(id)) return false;
        *userdata = new std::string(id);  // freed in onclose
        return true;
      })
      .onopen([this](crow::websocket::connection& conn) {
        auto* id = static_cast<std::string*>(conn.userdata());
        const auto job = jobs_.find(*id);
        if (!job) {
          conn.close("job not found");
          return;
        }
        // Hold ws_mutex_ while subscribing and sending the backlog, so live events (which
        // also take ws_mutex_) cannot overtake it.
        const std::scoped_lock lock(ws_mutex_);
        std::vector<JobEvent> backlog;
        crow::websocket::connection* c = &conn;
        const std::int64_t token = job->subscribe(
            [this, c](const JobEvent& e) {
              const std::scoped_lock l(ws_mutex_);
              if (ws_.contains(c)) c->send_text(eventJson(e).dump());
            },
            backlog);
        ws_[c] = {job, token};
        for (const JobEvent& e : backlog) conn.send_text(eventJson(e).dump());
      })
      .onclose([this](crow::websocket::connection& conn, const std::string& /*reason*/,
                      std::uint16_t /*code*/) {
        {
          const std::scoped_lock lock(ws_mutex_);
          if (const auto it = ws_.find(&conn); it != ws_.end()) {
            it->second.job->unsubscribe(it->second.token);
            ws_.erase(it);
          }
        }
        delete static_cast<std::string*>(conn.userdata());
        conn.userdata(nullptr);
      })
      .onmessage([](crow::websocket::connection& /*conn*/, const std::string& /*data*/,
                    bool /*binary*/) {});
}

void Server::start() {
  app_.bindaddr(config_.bind).port(config_.port).concurrency(config_.http_threads);
  server_thread_ = std::thread([this] { app_.run(); });
  app_.wait_for_server_start();
  Log::info("server_started", {{"bind", config_.bind}, {"port", config_.port}});
}

void Server::run() {
  app_.bindaddr(config_.bind).port(config_.port).concurrency(config_.http_threads);
  Log::info("server_starting", {{"bind", config_.bind}, {"port", config_.port}});
  app_.run();  // returns on SIGINT/SIGTERM (Crow installs the handlers) or stop()
  shutdown();
}

void Server::stop() {
  app_.stop();
  if (server_thread_.joinable()) server_thread_.join();
  shutdown();
}

void Server::shutdown() {
  {
    const std::scoped_lock lock(sweeper_mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  sweeper_cv_.notify_all();
  if (sweeper_.joinable()) sweeper_.join();
  jobs_.shutdown();  // cancels running solves, joins solver threads
  Log::info("server_stopped");
}

void Server::registerFactoryRoutes() {
  CROW_ROUTE(app_, "/api/factory/levels")([this] {
    Json out = Json::array();
    for (const FactoryLevelFile& l : factory_levels_) {
      out.push_back({{"id", l.id}, {"name", l.level.name}, {"description", l.level.description}});
    }
    return jsonResponse(200, out);
  });

  CROW_ROUTE(app_, "/api/factory/levels/<string>")([this](const std::string& id) {
    for (const FactoryLevelFile& l : factory_levels_) {
      if (l.id == id) return jsonResponse(200, l.doc);
    }
    return errorResponse(404, "level_not_found", "no factory level '" + id + "'");
  });

  CROW_ROUTE(app_, "/api/factory/solvers")([] {
    Json out = Json::array();
    for (const auto& info : factory::factorySolvers()) {
      out.push_back({{"name", info.name}, {"description", info.description}});
    }
    return jsonResponse(200, out);
  });

  // Shared by validate, simulate and solve: the bundled level named by body["level"].
  auto findLevel = [this](const Json& body, crow::response& error) -> const FactoryLevelFile* {
    if (!body.contains("level") || !body["level"].is_string()) {
      error = errorResponse(400, "invalid_request", R"("level" must be a level id)");
      return nullptr;
    }
    const std::string id = body["level"].get<std::string>();
    for (const FactoryLevelFile& l : factory_levels_) {
      if (l.id == id) return &l;
    }
    error = errorResponse(404, "level_not_found", "no factory level '" + id + "'");
    return nullptr;
  };

  CROW_ROUTE(app_, "/api/factory/validate")
      .methods("POST"_method)([this, findLevel](const crow::request& req) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        const FactoryLevelFile* level = findLevel(*body, error);
        if (level == nullptr) return error;
        const auto parsed =
            factory::json::parseDesign(body->value("design", Json::object()), level->level);
        const auto warnings = parsed.design ? factory::designWarnings(level->level, *parsed.design)
                                            : std::vector<factory::DesignIssue>{};
        const factory::Cents build =
            parsed.design ? factory::buildCost(level->level, *parsed.design) : 0;
        return jsonResponse(200, {{"valid", parsed.design.has_value()},
                                  {"errors", factory::json::issuesToJson(parsed.errors)},
                                  {"warnings", factory::json::issuesToJson(warnings)},
                                  {"build", build}});
      });

  CROW_ROUTE(app_, "/api/factory/simulate")
      .methods("POST"_method)([this, findLevel](const crow::request& req) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        const FactoryLevelFile* level = findLevel(*body, error);
        if (level == nullptr) return error;
        const auto parsed =
            factory::json::parseDesign(body->value("design", Json::object()), level->level);
        if (!parsed.design) {
          return errorResponse(422, "invalid_design", parsed.errors.front().message,
                               {{"errors", factory::json::issuesToJson(parsed.errors)}});
        }
        const auto result = factory::simulate(level->level, *parsed.design);
        return jsonResponse(
            200, {{"result", factory::json::toJson(result, level->level, *parsed.design)},
                  {"warnings", factory::json::issuesToJson(
                                   factory::designWarnings(level->level, *parsed.design))}});
      });

  CROW_ROUTE(app_, "/api/factory/solve")
      .methods("POST"_method)([this, findLevel](const crow::request& req) {
        crow::response error;
        const auto body = parseBody(req, config_.max_body_bytes, error);
        if (!body) return error;
        const FactoryLevelFile* level = findLevel(*body, error);
        if (level == nullptr) return error;
        const Json solvers = body->value("solvers", Json::array());
        if (!solvers.is_array() || solvers.empty() || solvers.size() > 8) {
          return errorResponse(400, "invalid_solvers",
                               "solvers must be a non-empty array of at most 8 names");
        }
        std::vector<std::string> names;
        for (const Json& n : solvers) {
          if (!n.is_string() || !factory::makeFactorySolver(n.get<std::string>())) {
            return errorResponse(400, "unknown_solver", "unknown factory solver " + n.dump());
          }
          names.push_back(n.get<std::string>());
        }
        SolveOptions options;
        options.time_limit_ms = 2000;
        if (body->contains("timeLimitMs")) {
          const Json& t = (*body)["timeLimitMs"];
          if (!t.is_number() || t.get<double>() <= 0) {
            return errorResponse(400, "invalid_options", "timeLimitMs must be a positive number");
          }
          options.time_limit_ms = t.get<double>();
        }
        // Factory solves run while the request waits, so they get a shorter cap than jobs.
        options.time_limit_ms =
            std::min({options.time_limit_ms, 10'000.0, config_.job_limits.max_time_limit_ms});
        if (body->contains("seed") && (*body)["seed"].is_number_unsigned()) {
          options.seed = (*body)["seed"].get<std::uint64_t>();
        }
        std::vector<std::future<factory::FactorySolution>> running;
        running.reserve(names.size());
        for (const std::string& n : names) {
          running.push_back(std::async(std::launch::async, [level, options, n] {
            return factory::makeFactorySolver(n)->solve(level->level, options);
          }));
        }
        const auto bound = factory::flowUpperBound(level->level, options);
        Json results = Json::array();
        for (auto& f : running) {
          const factory::FactorySolution s = f.get();
          results.push_back({{"solver_name", s.solver_name},
                             {"profit", s.profit},
                             {"runtime_ms", s.runtime_ms},
                             {"valid", s.valid},
                             {"error", s.error ? Json(*s.error) : Json(nullptr)},
                             {"stats", s.stats},
                             {"design", factory::json::designToJson(s.design, level->level)}});
        }
        return jsonResponse(
            200, {{"bound", bound.bound}, {"boundSolved", bound.solved}, {"results", results}});
      });
}

}  // namespace pf::server
