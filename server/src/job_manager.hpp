#pragma once

#include "pf/model.hpp"
#include "pf/solver.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace pf::server {

/// One message on a job's progress stream (also what the WebSocket sends).
struct JobEvent {
  std::int64_t seq = 0;  ///< 0, 1, 2, ... per job
  std::string solver;    ///< empty for job-level events
  /// "improved" (new best-so-far), "heartbeat", "solver_done", "solver_failed", "done".
  std::string event;
  double elapsed_ms = 0;  ///< since the job was submitted
  Quantity best_earnings = 0;
  std::int64_t nodes = 0;
};

/// Status of one solver within a job.
struct SolverRun {
  std::string solver;
  std::string status = "queued";  ///< queued | running | done | cancelled
  std::optional<Solution> solution;
};

/// A consistent copy of a job's state, safe to read without locks.
struct JobSnapshot {
  std::string id;
  std::string status;  ///< queued | running | done | cancelled
  double time_limit_ms = 0;
  std::vector<SolverRun> runs;
  std::size_t events = 0;
};

/// Limits enforced on solve requests.
struct JobLimits {
  double max_time_limit_ms = 30'000;  ///< requested limits are clamped to this
  std::size_t max_solvers_per_job = 16;
  std::size_t max_jobs = 200;  ///< oldest finished jobs are forgotten beyond this
  std::size_t max_events_per_job = 5'000;
  /// Minimum spacing of forwarded "improved" events per solver (the final value is always
  /// sent with "solver_done").
  double improved_interval_ms = 20;
};

/// A solve request: one instance, several solvers, shared options.
class Job {
 public:
  using Listener = std::function<void(const JobEvent&)>;

  Job(std::string id, Instance instance, std::vector<std::string> solvers, SolveOptions options,
      std::size_t max_events, double improved_interval_ms);

  [[nodiscard]] const std::string& id() const { return id_; }
  [[nodiscard]] const Instance& instance() const { return instance_; }
  [[nodiscard]] JobSnapshot snapshot() const;
  [[nodiscard]] bool finished() const;
  void cancel() { cancel_.store(true); }
  [[nodiscard]] bool cancelled() const { return cancel_.load(); }

  /// Atomically returns every event so far and registers `listener` for later ones.
  /// Returns a token for unsubscribe().
  std::int64_t subscribe(Listener listener, std::vector<JobEvent>& backlog);
  void unsubscribe(std::int64_t token);

 private:
  friend class JobManager;

  /// Records an event and forwards it to listeners (outside the lock).
  void emit(JobEvent event);
  void runSolver(std::size_t index);
  [[nodiscard]] double elapsedMs() const;

  std::string id_;
  Instance instance_;
  SolveOptions options_;
  std::atomic<bool> cancel_{false};
  std::chrono::steady_clock::time_point created_;
  std::optional<std::chrono::steady_clock::time_point> finished_at_;

  mutable std::mutex mutex_;
  std::vector<SolverRun> runs_;
  std::vector<JobEvent> events_;
  std::map<std::int64_t, Listener> listeners_;
  std::int64_t next_token_ = 0;
  std::size_t remaining_ = 0;
  std::size_t max_events_;
  double improved_interval_ms_;
};

/// Runs solver tasks on a fixed thread pool. Each (job, solver) pair is one task.
class JobManager {
 public:
  JobManager(std::size_t threads, JobLimits limits);
  JobManager(const JobManager&) = delete;
  JobManager& operator=(const JobManager&) = delete;
  JobManager(JobManager&&) = delete;
  JobManager& operator=(JobManager&&) = delete;
  /// Cancels every job and joins the workers (graceful shutdown).
  ~JobManager();

  [[nodiscard]] const JobLimits& limits() const { return limits_; }

  /// Queues a job. Solver names must be valid (checked by the caller). The time limit is
  /// clamped to limits().max_time_limit_ms.
  std::shared_ptr<Job> submit(Instance instance, std::vector<std::string> solvers,
                              SolveOptions options);
  [[nodiscard]] std::shared_ptr<Job> find(const std::string& id) const;
  /// Cancels a job; returns false if it does not exist.
  bool cancel(const std::string& id) const;
  /// Cancels everything and stops accepting work.
  void shutdown();

 private:
  struct Task {
    std::shared_ptr<Job> job;
    std::size_t index;
  };
  void worker();
  void forgetOldJobs();  // requires mutex_

  JobLimits limits_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Task> queue_;
  std::map<std::string, std::shared_ptr<Job>> jobs_;
  std::deque<std::string> order_;  // submission order, for retention
  bool stopping_ = false;
  std::vector<std::thread> workers_;
};

/// A random 128-bit id in hex.
[[nodiscard]] std::string randomId();

}  // namespace pf::server
