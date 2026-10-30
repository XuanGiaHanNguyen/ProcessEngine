#include "pf/solvers/simulated_annealing.hpp"

#include "pf/rules.hpp"
#include "pf/solvers/greedy.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace pf {

namespace {

/// Replays `schedule` in start order and drops every run that is illegal when reached.
/// Returns the surviving runs (sorted by start) and their earnings.
Rollout repair(const Instance& inst, Schedule schedule) {
  std::stable_sort(
      schedule.begin(), schedule.end(),
      [](const ScheduledRun& a, const ScheduledRun& b) { return a.start_tick < b.start_tick; });
  Rollout out;
  SimState sim(inst);
  for (const ScheduledRun& run : schedule) {
    if (run.start_tick < sim.tick() || run.start_tick > inst.horizon) continue;
    sim.advanceTo(run.start_tick);
    if (sim.checkStart(run.process)) continue;
    sim.start(run.process);
    out.runs.push_back(run);
  }
  sim.advanceTo(inst.horizon);
  out.earnings = sim.inventoryValue();
  return out;
}

class Annealer {
 public:
  Annealer(const Instance& inst, std::uint64_t seed) : inst_(inst), rng_(seed) {
    for (ProcessIndex p = 0; p < inst.processes.size(); ++p) {
      if (inst.processes[p].duration <= inst.horizon) startable_.push_back(p);
    }
  }

  /// A random neighbor of `current` (before repair).
  Schedule neighbor(const Schedule& current) {
    Schedule next = current;
    const int move = next.empty() ? 0 : uniform(0, 3);
    if (move == 0 && !startable_.empty()) {  // insert
      const ProcessIndex p = startable_[uniform<std::size_t>(0, startable_.size() - 1)];
      next.push_back({p, randomTick(current, inst_.processes[p].duration)});
    } else if (move == 1) {  // remove
      next.erase(next.begin() +
                 static_cast<std::ptrdiff_t>(uniform<std::size_t>(0, next.size() - 1)));
    } else if (move == 2) {  // shift
      ScheduledRun& run = next[uniform<std::size_t>(0, next.size() - 1)];
      run.start_tick = std::clamp(run.start_tick + uniform<Tick>(-3, 3), Tick{0}, inst_.horizon);
    } else if (!startable_.empty()) {  // change process
      ScheduledRun& run = next[uniform<std::size_t>(0, next.size() - 1)];
      run.process = startable_[uniform<std::size_t>(0, startable_.size() - 1)];
    }
    return next;
  }

  bool accept(Quantity candidate, Quantity current, double temperature) {
    if (candidate >= current) return true;
    const double p = std::exp(static_cast<double>(candidate - current) / temperature);
    return std::uniform_real_distribution<double>(0, 1)(rng_) < p;
  }

 private:
  template <typename Int = int>
  Int uniform(Int lo, Int hi) {
    return std::uniform_int_distribution<Int>(lo, hi)(rng_);
  }

  /// Half the time an event tick of the schedule (0 or a completion), else any tick.
  Tick randomTick(const Schedule& schedule, Tick duration) {
    const Tick latest = inst_.horizon - duration;
    if (!schedule.empty() && uniform(0, 1) == 0) {
      const ScheduledRun& run = schedule[uniform<std::size_t>(0, schedule.size() - 1)];
      const Tick end = run.start_tick + inst_.processes[run.process].duration;
      if (end <= latest) return end;
    }
    return uniform<Tick>(0, latest);
  }

  const Instance& inst_;
  std::mt19937_64 rng_;
  std::vector<ProcessIndex> startable_;
};

}  // namespace

RawSolution SimulatedAnnealingSolver::solveRaw(const Instance& instance,
                                               const SolveOptions& options,
                                               const Deadline& deadline,
                                               ProgressReporter& progress) {
  const auto iterations =
      static_cast<std::int64_t>(std::max(0.0, options.param("iterations", kDefaultIterations)));
  const double ratio =
      std::clamp(options.param("final_temperature_ratio", kDefaultFinalRatio), 1e-12, 1.0);

  const PotentialValues potential(instance);
  Rollout current = repair(
      instance,
      greedyRollout(SimState(instance), GreedyScore::PotentialValuePerTick, &potential).runs);
  Rollout best = current;
  progress.improved(best.earnings);

  double t0 = options.param("initial_temperature", 0);
  if (t0 <= 0) t0 = std::max(1.0, 0.05 * std::abs(static_cast<double>(current.earnings)));
  const double alpha =
      iterations > 0 ? std::pow(ratio, 1.0 / static_cast<double>(iterations)) : 1.0;

  Annealer annealer(instance, options.seed);
  double temperature = t0;
  std::int64_t done = 0;
  std::int64_t accepted = 0;
  std::int64_t improvements = 0;
  for (; done < iterations; ++done) {
    if (done % 256 == 0 && deadline.expired()) break;
    Rollout candidate = repair(instance, annealer.neighbor(current.runs));
    if (annealer.accept(candidate.earnings, current.earnings, temperature)) {
      current = std::move(candidate);
      ++accepted;
      if (current.earnings > best.earnings) {
        best = current;
        ++improvements;
        progress.improved(best.earnings, done);
      }
    }
    temperature *= alpha;
  }

  RawSolution result;
  result.runs = std::move(best.runs);
  result.stats["iterations"] = done;
  result.stats["accepted"] = accepted;
  result.stats["improvements"] = improvements;
  return result;
}

}  // namespace pf
