#include "pf/solver.hpp"

#include "pf/validator.hpp"

#include <exception>

namespace pf {

double SolveOptions::param(const std::string& key, double fallback) const {
  const auto it = params.find(key);
  return it == params.end() ? fallback : it->second;
}

Deadline::Deadline(const SolveOptions& options)
    : start_(std::chrono::steady_clock::now()),
      end_(start_ + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                        std::chrono::duration<double, std::milli>(options.time_limit_ms))),
      cancel_(options.cancel) {}

bool Deadline::expired() const {
  if (cancel_ != nullptr && cancel_->load(std::memory_order_relaxed)) return true;
  return std::chrono::steady_clock::now() >= end_;
}

double Deadline::elapsedMs() const {
  return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_)
      .count();
}

void ProgressReporter::improved(Quantity earnings, std::int64_t nodes) {
  if (best_ && earnings <= *best_) return;
  best_ = earnings;
  if (sink_ != nullptr) {
    sink_->onProgress(
        {ProgressEvent::Kind::Improved, solver_, deadline_->elapsedMs(), earnings, nodes});
  }
}

void ProgressReporter::heartbeat(std::int64_t nodes) {
  if (sink_ != nullptr) {
    sink_->onProgress({ProgressEvent::Kind::Heartbeat, solver_, deadline_->elapsedMs(),
                       best_.value_or(0), nodes});
  }
}

Solution Solver::solve(const Instance& instance, const SolveOptions& options, ProgressSink* sink) {
  const Deadline deadline(options);
  ProgressReporter progress(name(), sink, deadline);
  RawSolution raw;
  try {
    raw = solveRaw(instance, options, deadline, progress);
  } catch (const std::exception& e) {
    raw.runs.clear();
    raw.error = std::string("solver failed: ") + e.what();
  }

  Solution solution;
  solution.runtime_ms = deadline.elapsedMs();
  solution.solver_name = name();
  solution.stats = std::move(raw.stats);
  solution.runs = std::move(raw.runs);
  if (raw.error) {
    solution.error = std::move(raw.error);
    return solution;
  }
  const ValidationReport report = validate(instance, solution.runs);
  if (!report.valid) {
    solution.error = "solver produced an invalid schedule: " +
                     (report.violation ? report.violation->message : std::string("unknown"));
    return solution;
  }
  solution.valid = true;
  solution.earnings = report.earnings;
  solution.proven_optimal = raw.proven_optimal;
  return solution;
}

}  // namespace pf
