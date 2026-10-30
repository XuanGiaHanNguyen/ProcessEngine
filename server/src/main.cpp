// pf_server: the ProcessEngine HTTP + WebSocket API (docs/api.md).
//
//   pf_server [--port 8080] [--bind 0.0.0.0] [--instances DIR] [--solver-threads N]
//             [--http-threads N] [--max-sessions N] [--session-ttl SECONDS]
//             [--max-time-limit MS] [--cors-origin ORIGIN] [--log-level info]
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "logging.hpp"
#include "server.hpp"

int main(int argc, char** argv) {
  pf::server::Config config;
  config.solver_threads = std::max(1U, std::thread::hardware_concurrency());
  if (const char* dir = std::getenv("PF_INSTANCES_DIR")) config.instances_dir = dir;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--help" || arg == "-h" || i + 1 >= argc) {
      std::cerr << "usage: pf_server [--port N] [--bind ADDR] [--instances DIR]\n"
                   "                 [--solver-threads N] [--http-threads N] [--max-sessions N]\n"
                   "                 [--session-ttl SECONDS] [--max-time-limit MS]\n"
                   "                 [--cors-origin ORIGIN] [--log-level debug|info|warn|error]\n";
      return arg == "--help" || arg == "-h" ? 0 : 2;
    }
    const std::string value = argv[++i];
    try {
      if (arg == "--port") {
        config.port = static_cast<std::uint16_t>(std::stoi(value));
      } else if (arg == "--bind") {
        config.bind = value;
      } else if (arg == "--instances") {
        config.instances_dir = value;
      } else if (arg == "--solver-threads") {
        config.solver_threads = std::stoul(value);
      } else if (arg == "--http-threads") {
        config.http_threads = static_cast<std::uint16_t>(std::stoi(value));
      } else if (arg == "--max-sessions") {
        config.max_sessions = std::stoul(value);
      } else if (arg == "--session-ttl") {
        config.session_ttl = std::chrono::seconds(std::stol(value));
      } else if (arg == "--max-time-limit") {
        config.job_limits.max_time_limit_ms = std::stod(value);
      } else if (arg == "--cors-origin") {
        config.cors_origin = value;
      } else if (arg == "--log-level") {
        pf::server::Log::setLevel(pf::server::Log::parseLevel(value));
      } else {
        std::cerr << "unknown option " << arg << "\n";
        return 2;
      }
    } catch (const std::exception&) {
      std::cerr << "bad value for " << arg << ": " << value << "\n";
      return 2;
    }
  }
  pf::server::Server server(config);
  server.run();
  return 0;
}
