#include "job_manager.hpp"

#include "pf/registry.hpp"

#include <algorithm>
#include <random>
#include <string_view>

namespace pf::server {

std::string randomId() {
  static std::mutex mutex;
  static std::mt19937_64 rng{std::random_device{}()};
  const std::scoped_lock lock(mutex);
  static constexpr std::string_view kHex = "0123456789abcdef";
  std::string id;
  for (int i = 0; i < 2; ++i) {
    std::uint64_t v = rng();
    for (int k = 0; k < 16; ++k, v >>= 4U) id += kHex[v & 0xFU];
  }
  return id;
}

Job::Job(std::string id, Instance instance, std::vector<std::string> solvers, SolveOptions options,
         std::size_t max_events, double improved_interval_ms)
    : id_(std::move(id)),
      instance_(std::move(instance)),
      options_(std::move(options)),
      created_(std::chrono::steady_clock::now()),
      remaining_(solvers.size()),
      max_events_(max_events),
      improved_interval_ms_(improved_interval_ms) {
  for (std::string& s : solvers) runs_.push_back({std::move(s), "queued", std::nullopt});
  options_.cancel = &cancel_;
}

double Job::elapsedMs() const {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - created_)
      .count();
}

JobSnapshot Job::snapshot() const {
  const std::scoped_lock lock(mutex_);
  JobSnapshot s;
  s.id = id_;
  s.time_limit_ms = options_.time_limit_ms;
  s.runs = runs_;
  s.events = events_.size();
  const bool any_running = std::any_of(runs_.begin(), runs_.end(),
                                       [](const SolverRun& r) { return r.status == "running"; });
  if (remaining_ == 0) {
    s.status = cancel_.load() ? "cancelled" : "done";
  } else {
    s.status = any_running ? "running" : "queued";
  }
  return s;
}

bool Job::finished() const {
  const std::scoped_lock lock(mutex_);
  return remaining_ == 0;
}

std::int64_t Job::subscribe(Listener listener, std::vector<JobEvent>& backlog) {
  const std::scoped_lock lock(mutex_);
  backlog = events_;
  const std::int64_t token = next_token_++;
  listeners_.emplace(token, std::move(listener));
  return token;
}

void Job::unsubscribe(std::int64_t token) {
  const std::scoped_lock lock(mutex_);
  listeners_.erase(token);
}

void Job::emit(JobEvent event) {
  std::vector<Listener> targets;
  {
    const std::scoped_lock lock(mutex_);
    event.seq = static_cast<std::int64_t>(events_.size());
    events_.push_back(event);
    for (const auto& [_, l] : listeners_) targets.push_back(l);
  }
  for (const Listener& l : targets) l(event);
}

namespace {

/// Forwards solver progress into the job's event stream, throttling "improved" events.
class JobSink final : public ProgressSink {
 public:
  JobSink(std::function<void(const ProgressEvent&)> forward, double interval_ms)
      : forward_(std::move(forward)), interval_ms_(interval_ms) {}

  void onProgress(const ProgressEvent& e) override {
    if (e.kind == ProgressEvent::Kind::Improved && sent_ &&
        e.elapsed_ms - last_ms_ < interval_ms_) {
      return;
    }
    sent_ = true;
    last_ms_ = e.elapsed_ms;
    forward_(e);
  }

 private:
  std::function<void(const ProgressEvent&)> forward_;
  double interval_ms_;
  bool sent_ = false;
  double last_ms_ = 0;
};

}  // namespace

void Job::runSolver(std::size_t index) {
  std::string name;
  {
    const std::scoped_lock lock(mutex_);
    name = runs_[index].solver;
    runs_[index].status = cancel_.load() ? "cancelled" : "running";
  }
  std::optional<Solution> solution;
  if (!cancel_.load()) {
    // The event cap guards memory; "solver_done" and "done" are always recorded.
    JobSink sink(
        [&](const ProgressEvent& p) {
          {
            const std::scoped_lock lock(mutex_);
            if (events_.size() >= max_events_) return;
          }
          emit({0, name, p.kind == ProgressEvent::Kind::Improved ? "improved" : "heartbeat",
                elapsedMs(), p.best_earnings, p.nodes});
        },
        improved_interval_ms_);
    const auto solver = SolverRegistry::builtin().create(name);
    solution = solver->solve(instance_, options_, &sink);
  }

  bool all_done = false;
  {
    const std::scoped_lock lock(mutex_);
    SolverRun& run = runs_[index];
    run.status = solution ? "done" : "cancelled";
    run.solution = solution;
    all_done = --remaining_ == 0;
    if (all_done) finished_at_ = std::chrono::steady_clock::now();
  }
  if (solution) {
    emit({0, name, solution->valid ? "solver_done" : "solver_failed", elapsedMs(),
          solution->earnings, 0});
  }
  if (all_done) emit({0, "", "done", elapsedMs(), 0, 0});
}

JobManager::JobManager(std::size_t threads, JobLimits limits) : limits_(limits) {
  threads = std::max<std::size_t>(1, threads);
  for (std::size_t i = 0; i < threads; ++i) workers_.emplace_back([this] { worker(); });
}

JobManager::~JobManager() { shutdown(); }

void JobManager::shutdown() {
  {
    const std::scoped_lock lock(mutex_);
    if (stopping_ && workers_.empty()) return;
    stopping_ = true;
    for (const auto& [_, job] : jobs_) job->cancel();
  }
  cv_.notify_all();
  for (std::thread& t : workers_) {
    if (t.joinable()) t.join();
  }
  workers_.clear();
}

std::shared_ptr<Job> JobManager::submit(Instance instance, std::vector<std::string> solvers,
                                        SolveOptions options) {
  options.time_limit_ms = std::clamp(options.time_limit_ms, 1.0, limits_.max_time_limit_ms);
  options.threads = 1;
  const std::size_t count = solvers.size();
  auto job =
      std::make_shared<Job>(randomId(), std::move(instance), std::move(solvers), std::move(options),
                            limits_.max_events_per_job, limits_.improved_interval_ms);
  {
    const std::scoped_lock lock(mutex_);
    jobs_[job->id()] = job;
    order_.push_back(job->id());
    for (std::size_t i = 0; i < count; ++i) queue_.push_back({job, i});
    forgetOldJobs();
  }
  cv_.notify_all();
  return job;
}

void JobManager::forgetOldJobs() {
  while (jobs_.size() > limits_.max_jobs && !order_.empty()) {
    // Forget the oldest finished job; stop if the oldest is still running.
    const auto it = jobs_.find(order_.front());
    if (it != jobs_.end() && !it->second->finished()) break;
    if (it != jobs_.end()) jobs_.erase(it);
    order_.pop_front();
  }
}

std::shared_ptr<Job> JobManager::find(const std::string& id) const {
  const std::scoped_lock lock(mutex_);
  const auto it = jobs_.find(id);
  return it == jobs_.end() ? nullptr : it->second;
}

bool JobManager::cancel(const std::string& id) const {
  const auto job = find(id);
  if (!job) return false;
  job->cancel();
  return true;
}

void JobManager::worker() {
  while (true) {
    Task task;
    {
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [&] { return stopping_ || !queue_.empty(); });
      if (queue_.empty()) return;  // stopping and drained
      task = std::move(queue_.front());
      queue_.pop_front();
    }
    task.job->runSolver(task.index);  // a cancelled job's tasks finish immediately
  }
}

}  // namespace pf::server
