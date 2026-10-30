#pragma once

#include "pf/solver.hpp"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace pf {

/// Describes a solver-specific parameter accepted through SolveOptions::params.
struct SolverOption {
  std::string name;
  std::string description;
  double default_value = 0;
};

/// Static description of a registered solver (what GET /api/solvers returns).
struct SolverInfo {
  std::string name;
  std::string description;
  bool exact = false;  ///< can prove optimality
  std::vector<SolverOption> options;
};

/// Factory registry: lists solvers and constructs them by name.
///
/// Registration is explicit (see builtin()) rather than via static initializers in each
/// solver's translation unit, because linkers drop unreferenced objects from static
/// libraries and the registrations would silently disappear.
class SolverRegistry {
 public:
  using Factory = std::function<std::unique_ptr<Solver>()>;

  /// Adds a solver. Throws std::invalid_argument if the name is already taken.
  void add(SolverInfo info, Factory factory);

  /// Constructs a new solver instance, or nullptr if `name` is unknown.
  [[nodiscard]] std::unique_ptr<Solver> create(const std::string& name) const;

  [[nodiscard]] const SolverInfo* find(const std::string& name) const;
  /// All solvers in registration order.
  [[nodiscard]] std::vector<SolverInfo> list() const;

  /// The registry with every solver shipped in the engine.
  [[nodiscard]] static const SolverRegistry& builtin();

 private:
  struct Entry {
    SolverInfo info;
    Factory factory;
  };
  std::vector<Entry> entries_;
};

}  // namespace pf
