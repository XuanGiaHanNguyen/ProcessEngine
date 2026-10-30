#include "logging.hpp"

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>

namespace pf::server {

namespace {

std::atomic<Log::Level> g_level{Log::Level::Info};
std::mutex g_mutex;

const char* name(Log::Level level) {
  switch (level) {
    case Log::Level::Debug:
      return "debug";
    case Log::Level::Info:
      return "info";
    case Log::Level::Warn:
      return "warn";
    case Log::Level::Error:
      return "error";
    case Log::Level::Off:
      return "off";
  }
  return "info";
}

std::string timestamp() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t t = std::chrono::system_clock::to_time_t(now);
  const auto ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
  std::tm tm{};
  gmtime_r(&t, &tm);
  std::array<char, 64> buf{};
  std::snprintf(buf.data(), buf.size(), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ", tm.tm_year + 1900,
                tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, static_cast<int>(ms));
  return buf.data();
}

}  // namespace

void Log::setLevel(Level level) { g_level = level; }

Log::Level Log::parseLevel(const std::string& n) {
  if (n == "debug") return Level::Debug;
  if (n == "warn") return Level::Warn;
  if (n == "error") return Level::Error;
  if (n == "off") return Level::Off;
  return Level::Info;
}

void Log::write(Level level, const std::string& event, const nlohmann::json& fields) {
  if (level < g_level.load() || g_level.load() == Level::Off) return;
  nlohmann::json line = {{"ts", timestamp()}, {"level", name(level)}, {"event", event}};
  if (fields.is_object()) line.update(fields);
  const std::string text = line.dump() + "\n";
  const std::scoped_lock lock(g_mutex);
  std::fputs(text.c_str(), stderr);
}

}  // namespace pf::server
