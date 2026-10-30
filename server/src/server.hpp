#pragma once

#include "pf/model.hpp"

#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <crow.h>
#include <nlohmann/json.hpp>

#include "job_manager.hpp"
#include "session_store.hpp"

namespace pf::server {

/// Server configuration (command-line flags in main.cpp).
struct Config {
  std::string bind = "0.0.0.0";
  std::uint16_t port = 8080;
  std::uint16_t http_threads = 4;
  std::size_t solver_threads = 4;
  std::size_t max_body_bytes = std::size_t{2} * 1024 * 1024;
  std::size_t max_sessions = 1000;
  std::chrono::seconds session_ttl{3600};
  std::chrono::seconds sweep_interval{30};
  std::string cors_origin = "*";  ///< for local development
  std::string instances_dir;      ///< sample instances (optional)
  JobLimits job_limits;
  SessionStore::Clock clock = [] { return std::chrono::steady_clock::now(); };
};

/// Logs every request as one structured line (method, path, status, duration).
struct RequestLogger {
  struct context {
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
  };
  void before_handle(crow::request& req, crow::response& res, context& ctx);
  void after_handle(crow::request& req, crow::response& res, context& ctx);
};

/// Adds the CORS headers for local development to every response, including the
/// preflight (OPTIONS) responses Crow generates. The allowed origin is configurable.
struct Cors {
  struct context {};
  std::string origin = "*";
  void before_handle(crow::request& /*req*/, crow::response& /*res*/, context& /*ctx*/) {}
  void after_handle(crow::request& req, crow::response& res, context& ctx) const;
};

using App = crow::App<Cors, RequestLogger>;

/// A bundled sample instance (GET /api/instances/samples).
struct Sample {
  std::string id;
  std::string description;
  Instance instance;
};

/// The HTTP + WebSocket API (docs/api.md) over the engine. Thin: requests are parsed and
/// validated here, and all game rules and solving happen in the engine.
class Server {
 public:
  explicit Server(Config config);
  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;
  Server(Server&&) = delete;
  Server& operator=(Server&&) = delete;
  ~Server();

  /// Blocks until stop() or SIGINT/SIGTERM, then shuts down gracefully: running solves are
  /// cancelled and worker threads joined.
  void run();
  /// Starts serving on background threads and returns once the port is listening.
  void start();
  void stop();

  [[nodiscard]] App& app() { return app_; }
  [[nodiscard]] JobManager& jobs() { return jobs_; }
  [[nodiscard]] SessionStore& sessions() { return sessions_; }

 private:
  void loadSamples();
  void registerRoutes();
  void registerSolveRoutes();
  void registerSessionRoutes();
  void registerWebSocket();
  void shutdown();

  Config config_;
  App app_;
  JobManager jobs_;
  SessionStore sessions_;
  std::vector<Sample> samples_;

  std::thread server_thread_;
  std::thread sweeper_;
  std::mutex sweeper_mutex_;
  std::condition_variable sweeper_cv_;
  bool stopping_ = false;

  /// Open WebSocket connections and their job subscriptions.
  struct Subscription {
    std::shared_ptr<Job> job;
    std::int64_t token = -1;
  };
  std::mutex ws_mutex_;
  std::map<crow::websocket::connection*, Subscription> ws_;
};

}  // namespace pf::server
