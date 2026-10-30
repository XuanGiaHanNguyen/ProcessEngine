#include "pf/registry.hpp"

#include "pf/solvers/brute_force.hpp"
#include "pf/solvers/greedy.hpp"

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
  return r;
}

}  // namespace

const SolverRegistry& SolverRegistry::builtin() {
  static const SolverRegistry registry = makeBuiltin();
  return registry;
}

}  // namespace pf
