#pragma once

#include "pf/model.hpp"

#include <cstdint>

namespace pf {

/// Controls for the random instance generator. All ranges are inclusive.
struct GeneratorParams {
  std::uint64_t seed = 1;
  std::int32_t resources = 6;    ///< raised to chain_depth + 1 if smaller
  std::int32_t processes = 6;    ///< raised to chain_depth if smaller
  std::int32_t chain_depth = 3;  ///< number of processes on the guaranteed profitable chain
  std::int32_t workers = 2;
  Tick horizon = 20;  ///< raised to the guaranteed chain's total duration if smaller
  Quantity min_price = 1;
  Quantity max_price = 20;
  Quantity max_qty = 3;  ///< per input/output entry
  Tick max_duration = 4;
  double zero_price_probability = 0.3;  ///< chance an intermediate cannot be sold
  double max_runs_probability = 0.2;    ///< chance a process gets a max_runs cap
  double generator_probability = 0.1;   ///< chance a process has no inputs

  /// Brute-force friendly: 3-4 resources, 2-4 processes, horizon <= 7, 1-2 workers.
  static GeneratorParams small(std::uint64_t seed);
  /// Exact solvers struggle: ~8 resources, ~10 processes, horizon ~30, 3 workers.
  static GeneratorParams medium(std::uint64_t seed);
  /// Heuristics only: ~20 resources, ~30 processes, horizon ~100, 5 workers.
  static GeneratorParams large(std::uint64_t seed);
};

/// A generated instance plus a schedule proving it has a profitable path.
struct GeneratedInstance {
  Instance instance;
  /// Runs the guaranteed chain once. Valid, and its earnings are strictly greater than
  /// selling the start inventory untouched.
  Schedule witness;
};

/// Deterministic for a given params (including seed). The result satisfies checkInstance().
///
/// Resources get a level 0..chain_depth; processes turn lower-level resources into
/// higher-level ones, so the process graph is acyclic and has chains up to chain_depth
/// long. One chain r0 -> r1 -> ... -> r_depth is built explicitly with enough start
/// inventory and a top price high enough to be profitable.
[[nodiscard]] GeneratedInstance generate(const GeneratorParams& params);

}  // namespace pf
