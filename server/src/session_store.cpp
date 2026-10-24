#include "session_store.hpp"

#include "job_manager.hpp"  // randomId

namespace pf::server {

SessionStore::SessionStore(std::size_t max_sessions, std::chrono::seconds ttl, Clock clock)
    : max_sessions_(max_sessions), ttl_(ttl), clock_(std::move(clock)) {}

std::optional<std::string> SessionStore::create(Instance instance) {
  const std::scoped_lock lock(mutex_);
  if (sessions_.size() >= max_sessions_) sweepLocked();
  if (sessions_.size() >= max_sessions_) return std::nullopt;
  std::string id = randomId();
  sessions_.emplace(id, std::make_shared<Entry>(std::move(instance), clock_()));
  return id;
}

bool SessionStore::with(const std::string& id, const std::function<void(GameSession&)>& fn) {
  std::shared_ptr<Entry> entry;
  {
    const std::scoped_lock lock(mutex_);
    const auto it = sessions_.find(id);
    if (it == sessions_.end()) return false;
    const auto now = clock_();
    if (now - it->second->last_used > ttl_) {
      sessions_.erase(it);
      return false;
    }
    it->second->last_used = now;
    entry = it->second;
  }
  const std::scoped_lock lock(entry->mutex);
  fn(entry->session);
  return true;
}

bool SessionStore::erase(const std::string& id) {
  const std::scoped_lock lock(mutex_);
  return sessions_.erase(id) > 0;
}

std::size_t SessionStore::sweep() {
  const std::scoped_lock lock(mutex_);
  return sweepLocked();
}

std::size_t SessionStore::sweepLocked() {
  const auto now = clock_();
  std::size_t removed = 0;
  for (auto it = sessions_.begin(); it != sessions_.end();) {
    if (now - it->second->last_used > ttl_) {
      it = sessions_.erase(it);
      ++removed;
    } else {
      ++it;
    }
  }
  return removed;
}

std::size_t SessionStore::size() const {
  const std::scoped_lock lock(mutex_);
  return sessions_.size();
}

}  // namespace pf::server
