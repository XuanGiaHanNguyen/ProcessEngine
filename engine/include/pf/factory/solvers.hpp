#pragma once

#include "pf/factory/level.hpp"
#include "pf/solver.hpp"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pf::factory {

/// The validated result of a factory solve. `profit` always comes from simulate().
struct FactorySolution {
  std::string solver_name;
  Design design;
  Cents profit = 0;
  double runtime_ms = 0;
  SolverStats stats;
  bool valid = false;
  std::optional<std::string> error;
};

/// What a concrete factory solver returns; FactorySolver::solve() re-simulates it.
struct RawFactorySolution {
  Design design;
  SolverStats stats;
  std::optional<std::string> error;
};

/// A design optimizer. Like pf::Solver, solve() is non-virtual: it times solveRaw(), checks
/// the design and scores it with simulate(), so a solver cannot misreport its profit.
class FactorySolver {
 public:
  FactorySolver() = default;
  FactorySolver(const FactorySolver&) = delete;
  FactorySolver& operator=(const FactorySolver&) = delete;
  FactorySolver(FactorySolver&&) = delete;
  FactorySolver& operator=(FactorySolver&&) = delete;
  virtual ~FactorySolver() = default;

  [[nodiscard]] virtual std::string name() const = 0;
  /// Solves within options.time_limit_ms (and options.cancel). Progress events carry the
  /// best profit in cents as best_earnings.
  [[nodiscard]] FactorySolution solve(const Level& level, const SolveOptions& options,
                                      ProgressSink* sink = nullptr) const;

 protected:
  [[nodiscard]] virtual RawFactorySolution solveRaw(const Level& level, const SolveOptions& options,
                                                    ProgressSink* sink) const = 0;
};

/// Name and one-line description of a built-in factory solver.
struct FactorySolverInfo {
  std::string name;
  std::string description;
};

/// The built-in factory solvers: greedy, local_search, flow_lp.
[[nodiscard]] std::vector<FactorySolverInfo> factorySolvers();
/// Constructs a built-in factory solver, or nullptr for an unknown name.
[[nodiscard]] std::unique_ptr<FactorySolver> makeFactorySolver(std::string_view name);

/// An upper bound on the profit of every valid design: the optimum of a linear relaxation
/// over totals (batches per recipe, machines per recipe, order rates, sales at full and
/// overflow price) that ignores timing, belts and integrality.
struct FlowBound {
  Cents bound = 0;
  bool solved = false;  ///< false if the LP hit its iteration or time limit
};
[[nodiscard]] FlowBound flowUpperBound(const Level& level, const SolveOptions& options);

/// A machine to build: (machine type, recipe of that type).
using MachineChoice = std::pair<Index, Index>;

/// Builds a design from a list of machines and orders: machines go into slots upstream
/// first, and every producer of a resource is belted to every consumer of it (weight 1),
/// including the market. Requires machines.size() <= level.slots.size().
[[nodiscard]] Design autoWiredDesign(const Level& level, std::vector<MachineChoice> machines,
                                     const std::vector<int>& orders);

}  // namespace pf::factory
