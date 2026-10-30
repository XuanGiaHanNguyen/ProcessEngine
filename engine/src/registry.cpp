#include "pf/registry.hpp"

#include "pf/solvers/beam_search.hpp"
#include "pf/solvers/branch_and_bound.hpp"
#include "pf/solvers/brute_force.hpp"
#include "pf/solvers/dp.hpp"
#include "pf/solvers/greedy.hpp"
#include "pf/solvers/lp_relaxation.hpp"
#include "pf/solvers/simulated_annealing.hpp"

#include <stdexcept>

namespace pf {

void SolverRegistry::add(SolverInfo info, Factory factory) {
  if (find(info.name) != nullptr) {
    throw std::invalid_argument("solver '" + info.name + "' is already registered");
  }
  entries_.push_back({std::move(info), std::move(factory)});
}

std::unique_ptr<Solver> SolverRegistry::create(const std::string& name) const {
  for (const Entry& e : entries_) {
    if (e.info.name == name) return e.factory();
  }
  return nullptr;
}

const SolverInfo* SolverRegistry::find(const std::string& name) const {
  for (const Entry& e : entries_) {
    if (e.info.name == name) return &e.info;
  }
  return nullptr;
}

std::vector<SolverInfo> SolverRegistry::list() const {
  std::vector<SolverInfo> out;
  out.reserve(entries_.size());
  for (const Entry& e : entries_) out.push_back(e.info);
  return out;
}

namespace {

SolverRegistry makeBuiltin() {
  SolverRegistry r;
  r.add({"greedy", "Starts the legal process with the best market-value gain per tick.", false, {}},
        [] { return std::make_unique<GreedySolver>(GreedyScore::MarketValuePerTick); });
  r.add({"greedy_potential",
         "Greedy that values outputs by what they can still be turned into before the horizon.",
         false,
         {}},
        [] { return std::make_unique<GreedySolver>(GreedyScore::PotentialValuePerTick); });
  r.add({"brute_force",
         "Exhaustive search over every schedule. Tiny instances only (ground truth for tests).",
         true,
         {{"max_nodes", "Give up after this many search nodes.",
           static_cast<double>(BruteForceSolver::kDefaultMaxNodes)}}},
        [] { return std::make_unique<BruteForceSolver>(); });
  r.add({"dp",
         "Dynamic programming over memoized event states (tick, inventory, runs in progress).",
         true,
         {{"max_states", "Fail cleanly after storing this many states.",
           static_cast<double>(DPSolver::kDefaultMaxStates)}}},
        [] { return std::make_unique<DPSolver>(); });
  r.add({"branch_and_bound",
         "Depth-first branch and bound with a dual (potential) upper bound, best-first child "
         "order and a greedy incumbent. Anytime.",
         true,
         {{"max_transpositions", "Cap on remembered fully-searched states.",
           static_cast<double>(BranchAndBoundSolver::kDefaultMaxTranspositions)}}},
        [] { return std::make_unique<BranchAndBoundSolver>(); });
  r.add({"beam_search",
         "Beam search over the event decision tree, scoring children by greedy rollouts.",
         false,
         {{"beam_width", "Children kept per level.", BeamSearchSolver::kDefaultWidth}}},
        [] { return std::make_unique<BeamSearchSolver>(); });
  r.add(
      {"simulated_annealing",
       "Simulated annealing with insert/remove/shift/change moves and rule-based repair.",
       false,
       {{"iterations", "Number of annealing steps.", SimulatedAnnealingSolver::kDefaultIterations},
        {"initial_temperature", "Starting temperature (0 = 5% of the greedy earnings).", 0},
        {"final_temperature_ratio", "Final temperature as a fraction of the initial one.",
         SimulatedAnnealingSolver::kDefaultFinalRatio}}},
      [] { return std::make_unique<SimulatedAnnealingSolver>(); });
  r.add({"lp_relaxation",
         "Solves the time-indexed LP relaxation (upper bound in stats.lp_bound) and rounds "
         "its plan into a schedule.",
         false,
         {}},
        [] { return std::make_unique<LPRelaxationSolver>(); });
  return r;
}

}  // namespace

const SolverRegistry& SolverRegistry::builtin() {
  static const SolverRegistry registry = makeBuiltin();
  return registry;
}

}  // namespace pf
