// Property tests over generated instances, run for every solver in the registry, so new
// solvers are covered automatically (PROJECT_PLAN.md section 8).
#include "pf/game_session.hpp"
#include "pf/generator.hpp"
#include "pf/registry.hpp"
#include "pf/validator.hpp"

#include <gtest/gtest.h>

namespace pf {
namespace {

constexpr int kSmallInstances = 1000;

SolveOptions fastOptions() {
  SolveOptions options;
  options.time_limit_ms = 2000;
  options.seed = 42;
  return options;
}

TEST(Property, AllSolversValidAndExactSolversMatchBruteForce) {
  const auto& registry = SolverRegistry::builtin();
  const auto solvers = registry.list();
  for (int i = 0; i < kSmallInstances; ++i) {
    const Instance inst = generate(GeneratorParams::small(static_cast<std::uint64_t>(i))).instance;
    SolveOptions generous = fastOptions();
    generous.time_limit_ms = 60'000;  // ground truth must finish, even under sanitizers
    const Solution truth = registry.create("brute_force")->solve(inst, generous, nullptr);
    ASSERT_TRUE(truth.valid && truth.proven_optimal)
        << "instance " << i << ": " << truth.error.value_or("not proven");

    for (const SolverInfo& info : solvers) {
      if (info.name == "brute_force") continue;
      const Solution s = registry.create(info.name)->solve(inst, fastOptions(), nullptr);
      ASSERT_TRUE(s.valid) << info.name << " instance " << i << ": " << s.error.value_or("");
      // Independent re-check, in case Solver::solve() itself were broken.
      ASSERT_EQ(validate(inst, s.runs).earnings, s.earnings) << info.name;
      ASSERT_LE(s.earnings, truth.earnings) << info.name << " beat the optimum on " << i;
      if (s.proven_optimal) {
        ASSERT_EQ(s.earnings, truth.earnings) << info.name << " wrongly proved optimal on " << i;
      }
      if (info.exact) {
        ASSERT_TRUE(s.proven_optimal) << info.name << " instance " << i;
      }
    }
  }
}

TEST(Property, SolversAreDeterministicForFixedSeed) {
  const auto& registry = SolverRegistry::builtin();
  for (int i = 0; i < 100; ++i) {
    const Instance inst =
        generate(GeneratorParams::small(5000 + static_cast<std::uint64_t>(i))).instance;
    for (const SolverInfo& info : registry.list()) {
      const Solution a = registry.create(info.name)->solve(inst, fastOptions(), nullptr);
      const Solution b = registry.create(info.name)->solve(inst, fastOptions(), nullptr);
      ASSERT_EQ(a.runs, b.runs) << info.name << " instance " << i;
    }
  }
}

TEST(Property, HeuristicsValidOnMediumInstances) {
  const auto& registry = SolverRegistry::builtin();
  for (int i = 0; i < 30; ++i) {
    const Instance inst = generate(GeneratorParams::medium(static_cast<std::uint64_t>(i))).instance;
    for (const SolverInfo& info : registry.list()) {
      if (info.exact) continue;
      const Solution s = registry.create(info.name)->solve(inst, fastOptions(), nullptr);
      ASSERT_TRUE(s.valid) << info.name << " instance " << i << ": " << s.error.value_or("");
    }
  }
}

}  // namespace
}  // namespace pf
