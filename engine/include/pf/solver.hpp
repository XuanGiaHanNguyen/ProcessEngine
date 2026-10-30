#pragma once

#include "pf/model.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace pf {

/// Settings shared by every solver.
struct SolveOptions {
  double time_limit_ms = 10'000;  ///< wall-clock budget; solvers return their best when it runs out
  std::uint64_t seed = 1;         ///< for randomized solvers; same seed -> same result
  int threads = 1;
  /// Solver-specific numeric parameters (e.g. "beam_width"); see SolverInfo::options.
  std::map<std::string, double> params;
  /// Optional external cancel flag (e.g. set by the server's DELETE /api/solve/{id}).
  const std::atomic<bool>* cancel = nullptr;

  /// Returns params[key] if present, otherwise `fallback`.
  [[nodiscard]] double param(const std::string& key, double fallback) const;
};

/// Solver-specific counters (nodes explored, nodes pruned, states, ...).
using SolverStats = std::map<std::string, std::int64_t>;

/// The validated result of a solve. `earnings` always comes from the validator.
struct Solution {
  Schedule runs;
  Quantity earnings = 0;
  bool proven_optimal = false;
  double runtime_ms = 0;
  std::string solver_name;
  SolverStats stats;
  bool valid = false;                ///< validator accepted `runs`
  std::optional<std::string> error;  ///< why there is no valid solution, if any
};

/// A best-so-far update or a periodic heartbeat, streamed to the UI during a solve.
struct ProgressEvent {
  enum class Kind : std::uint8_t { Improved, Heartbeat };
  Kind kind = Kind::Improved;
  std::string solver;
  double elapsed_ms = 0;
  Quantity best_earnings = 0;
  std::int64_t nodes = 0;
};

/// Receives progress during a solve. Called on the solving thread.
class ProgressSink {
 public:
  ProgressSink() = default;
  ProgressSink(const ProgressSink&) = default;
  ProgressSink& operator=(const ProgressSink&) = default;
  ProgressSink(ProgressSink&&) = default;
  ProgressSink& operator=(ProgressSink&&) = default;
  virtual ~ProgressSink() = default;

  virtual void onProgress(const ProgressEvent& event) = 0;
};

/// What a concrete solver produces; Solver::solve() validates it into a Solution.
struct RawSolution {
  Schedule runs;
  bool proven_optimal = false;
  SolverStats stats;
  std::optional<std::string> error;  ///< set when the solver refuses or fails (e.g. too large)
};

/// Tracks the time limit and cancel flag. Cheap enough to call in inner loops.
class Deadline {
 public:
  explicit Deadline(const SolveOptions& options);

  /// True once the time limit has passed or cancel was requested.
  [[nodiscard]] bool expired() const;
  [[nodiscard]] double elapsedMs() const;

 private:
  std::chrono::steady_clock::time_point start_;
  std::chrono::steady_clock::time_point end_;
  const std::atomic<bool>* cancel_;
};

/// Reports progress to an optional sink, tracking the best earnings seen so far.
/// Solvers use it so that a null sink needs no special handling.
class ProgressReporter {
 public:
  ProgressReporter(std::string solver, ProgressSink* sink, const Deadline& deadline)
      : solver_(std::move(solver)), sink_(sink), deadline_(&deadline) {}

  /// Emits an Improved event if `earnings` beats every earlier call.
  void improved(Quantity earnings, std::int64_t nodes = 0);
  /// Emits a Heartbeat event with the current best.
  void heartbeat(std::int64_t nodes);
  [[nodiscard]] std::optional<Quantity> best() const { return best_; }

 private:
  std::string solver_;
  ProgressSink* sink_;
  const Deadline* deadline_;
  std::optional<Quantity> best_;
};

/// Abstract solver. Uses the non-virtual-interface pattern: subclasses implement
/// solveRaw(), and solve() times it and runs the result through the validator, so a
/// solver's own idea of its earnings is never trusted (PROJECT_PLAN.md rule 9.2).
class Solver {
 public:
  Solver() = default;
  Solver(const Solver&) = delete;
  Solver& operator=(const Solver&) = delete;
  Solver(Solver&&) = delete;
  Solver& operator=(Solver&&) = delete;
  virtual ~Solver() = default;

  /// Registry name, e.g. "greedy".
  [[nodiscard]] virtual std::string name() const = 0;

  /// Solves `instance` and returns a validated Solution. `sink` may be null.
  /// Precondition: checkInstance(instance) is empty.
  Solution solve(const Instance& instance, const SolveOptions& options, ProgressSink* sink);

 protected:
  virtual RawSolution solveRaw(const Instance& instance, const SolveOptions& options,
                               const Deadline& deadline, ProgressReporter& progress) = 0;
};

}  // namespace pf
