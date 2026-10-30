#pragma once

#include "pf/game_session.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace pf::server {

/// In-memory Play-mode sessions with an inactivity expiry and a cap on how many exist.
/// Each session has its own mutex, so different sessions are used concurrently.
class SessionStore {
 public:
  using Clock = std::function<std::chrono::steady_clock::time_point()>;

  SessionStore(
      std::size_t max_sessions, std::chrono::seconds ttl,
      Clock clock = [] { return std::chrono::steady_clock::now(); });

  /// Creates a session; returns its id, or nullopt if the cap is reached even after
  /// dropping expired sessions.
  std::optional<std::string> create(Instance instance);

  /// Runs `fn` on the session under its lock and marks it as used. Returns false if the
  /// session does not exist (or has expired).
  bool with(const std::string& id, const std::function<void(GameSession&)>& fn);

  bool erase(const std::string& id);
  /// Drops expired sessions; returns how many.
  std::size_t sweep();
  [[nodiscard]] std::size_t size() const;
  [[nodiscard]] std::size_t capacity() const { return max_sessions_; }
  [[nodiscard]] std::chrono::seconds ttl() const { return ttl_; }

 private:
  struct Entry {
    std::mutex mutex;  ///< guards `session`
    GameSession session;
    std::chrono::steady_clock::time_point last_used;  ///< guarded by SessionStore::mutex_

    Entry(Instance instance, std::chrono::steady_clock::time_point now)
        : session(std::move(instance)), last_used(now) {}
  };
  std::size_t sweepLocked();

  std::size_t max_sessions_;
  std::chrono::seconds ttl_;
  Clock clock_;
  mutable std::mutex mutex_;
  std::map<std::string, std::shared_ptr<Entry>> sessions_;
};

}  // namespace pf::server
