#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace pf::server {

/// Structured logging: one JSON object per line on stderr, e.g.
///   {"ts":"2026-10-24T10:00:00.123Z","level":"info","event":"request",...}
class Log {
 public:
  enum class Level : std::uint8_t { Debug, Info, Warn, Error, Off };

  static void setLevel(Level level);
  static Level parseLevel(const std::string& name);  ///< unknown -> Info
  static void write(Level level, const std::string& event, const nlohmann::json& fields = {});

  static void info(const std::string& event, const nlohmann::json& fields = {}) {
    write(Level::Info, event, fields);
  }
  static void warn(const std::string& event, const nlohmann::json& fields = {}) {
    write(Level::Warn, event, fields);
  }
  static void error(const std::string& event, const nlohmann::json& fields = {}) {
    write(Level::Error, event, fields);
  }
};

}  // namespace pf::server
