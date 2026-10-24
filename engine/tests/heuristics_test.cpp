#include "pf/generator.hpp"
#include "pf/json_io.hpp"
#include "pf/registry.hpp"
#include "pf/solvers/lp_relaxation.hpp"

#include <gtest/gtest.h>

#include "../src/solvers/simplex.hpp"
#include "instance_files.hpp"
#include "test_util.hpp"

namespace pf {
namespace {

Solution run(const std::string& name, const Instance& instance, const SolveOptions& options = {}) {
  return SolverRegistry::builtin().create(name)->solve(instance, options, nullptr);
}

std::vector<Instance> testInstances() {
  std::vector<Instance> out;
  for (const auto& f : test::instanceFiles()) {
    out.push_back(*json::parseInstance(test::loadJson(f)).value);
  }
  for (std::uint64_t s = 0; s < 200; ++s)
    out.push_back(generate(GeneratorParams::small(s)).instance);
  for (std::uint64_t s = 0; s < 15; ++s)
    out.push_back(generate(GeneratorParams::medium(s)).instance);
  return out;
}

// --- Simplex -----------------------------------------------------------------------------

TEST(Simplex, SolvesTextbookLp) {
  // max 3x + 2y  s.t.  x + y <= 4,  x + 3y <= 6  ->  x = 4, y = 0, objective 12, y* = (3, 0).
  detail::LinearProgram lp;
  lp.rows = 2;
  lp.cols = 2;
  lp.a = {1, 1, 1, 3};
  lp.b = {4, 6};
  lp.c = {3, 2};
  const auto r = detail::solveSimplex(lp, 100, [] { return false; });
  ASSERT_EQ(r.status, detail::SimplexResult::Status::Optimal);
  EXPECT_NEAR(r.objective, 12, 1e-9);
  EXPECT_NEAR(r.x[0], 4, 1e-9);
  EXPECT_NEAR(r.x[1], 0, 1e-9);
  EXPECT_NEAR(r.y[0], 3, 1e-9);
  EXPECT_NEAR(r.y[1], 0, 1e-9);
}

TEST(Simplex, DetectsUnboundedness) {
  detail::LinearProgram lp;
  lp.rows = 1;
  lp.cols = 2;
  lp.a = {1, -1};
  lp.b = {1};
  lp.c = {0, 1};
  EXPECT_EQ(detail::solveSimplex(lp, 100, [] { return false; }).status,
            detail::SimplexResult::Status::Unbounded);
}

// --- LP relaxation bound -----------------------------------------------------------------

TEST(LpRelaxation, BoundIsAtLeastEverySolversEarnings) {
  SolveOptions options;
  options.time_limit_ms = 300;
  options.params["iterations"] = 2000;
  const auto solvers = SolverRegistry::builtin().list();
  int index = 0;
  for (const Instance& inst : testInstances()) {
    const Solution lp = run("lp_relaxation", inst, options);
    ASSERT_TRUE(lp.valid);
    const Quantity bound = lp.stats.at("lp_bound");
    for (const SolverInfo& info : solvers) {
      if (info.name == "brute_force" && inst.horizon > 8) continue;
      const Solution s = run(info.name, inst, options);
      if (!s.valid) continue;  // e.g. dp out of states on a medium instance
      EXPECT_GE(bound, s.earnings)
          << info.name << " on instance " << index << " (" << inst.name << ")";
    }
    ++index;
  }
}

TEST(LpRelaxation, IsTightOnSawmillAndProvesOptimality) {
  const Instance inst = test::InstanceBuilder()
                            .resource("wood", 1, 4)
                            .resource("plank", 4)
                            .process("saw", {{"wood", 2}}, {{"plank", 1}}, 2)
                            .workers(1)
                            .horizon(4)
                            .build();
  const Solution s = run("lp_relaxation", inst);
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.stats.at("lp_bound"), 8);
  EXPECT_EQ(s.stats.at("lp_solved"), 1);
  EXPECT_EQ(s.earnings, 8);
  EXPECT_TRUE(s.proven_optimal);
}

TEST(LpRelaxation, SkipsHugeLpsButKeepsAValidBound) {
  const Instance inst = generate(GeneratorParams::large(4)).instance;
  const Solution s = run("lp_relaxation", inst);
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.stats.at("lp_too_large"), 1);
  EXPECT_GE(s.stats.at("lp_bound"), s.earnings);
}

// --- Heuristics --------------------------------------------------------------------------

TEST(Heuristics, ValidWithinTimeLimitOnMediumInstances) {
  SolveOptions options;
  options.time_limit_ms = 300;
  for (std::uint64_t seed = 0; seed < 10; ++seed) {
    const Instance inst = generate(GeneratorParams::medium(100 + seed)).instance;
    for (const char* name : {"beam_search", "simulated_annealing", "lp_relaxation"}) {
      const Solution s = run(name, inst, options);
      ASSERT_TRUE(s.valid) << name << ": " << s.error.value_or("");
      EXPECT_LE(s.runtime_ms, options.time_limit_ms + 150) << name;
    }
  }
}

TEST(Heuristics, ReportGapAgainstExactOnSmallInstances) {
  // Acceptance (M5): report each heuristic's optimality gap versus the exact solvers.
  SolveOptions options;
  options.params["iterations"] = 2000;
  for (const char* name :
       {"greedy", "greedy_potential", "beam_search", "simulated_annealing", "lp_relaxation"}) {
    double total_gap = 0;
    int optimal = 0;
    constexpr int kCount = 200;
    for (std::uint64_t seed = 0; seed < kCount; ++seed) {
      const Instance inst = generate(GeneratorParams::small(seed)).instance;
      const Solution exact = run("branch_and_bound", inst);
      ASSERT_TRUE(exact.proven_optimal);
      const Solution s = run(name, inst, options);
      ASSERT_LE(s.earnings, exact.earnings) << name;
      if (s.earnings == exact.earnings) ++optimal;
      if (exact.earnings > 0) {
        total_gap += 100.0 * static_cast<double>(exact.earnings - s.earnings) /
                     static_cast<double>(exact.earnings);
      }
    }
    const double mean_gap = total_gap / kCount;
    RecordProperty(std::string(name) + "_mean_gap_pct", std::to_string(mean_gap));
    std::printf("  %-20s mean gap %6.2f%%  optimal %3d/%d\n", name, mean_gap, optimal, kCount);
    EXPECT_LT(mean_gap, 15.0) << name;
  }
}

TEST(SimulatedAnnealing, IsSeededAndNeverWorseThanItsStart) {
  for (std::uint64_t i = 0; i < 10; ++i) {
    const Instance inst = generate(GeneratorParams::medium(i)).instance;
    SolveOptions a;
    a.seed = 1;
    a.params["iterations"] = 3000;
    a.time_limit_ms = 60'000;
    const Solution s1 = run("simulated_annealing", inst, a);
    const Solution s2 = run("simulated_annealing", inst, a);
    EXPECT_EQ(s1.runs, s2.runs);
    EXPECT_EQ(s1.stats.at("iterations"), 3000);
    EXPECT_GE(s1.earnings, run("greedy_potential", inst).earnings);
  }
}

TEST(BeamSearch, WidthIsConfigurable) {
  const Instance inst = generate(GeneratorParams::medium(7)).instance;
  SolveOptions narrow;
  narrow.params["beam_width"] = 1;
  SolveOptions wide;
  wide.params["beam_width"] = 32;
  const Solution n = run("beam_search", inst, narrow);
  const Solution w = run("beam_search", inst, wide);
  ASSERT_TRUE(n.valid && w.valid);
  EXPECT_GT(w.stats.at("nodes"), n.stats.at("nodes"));
  EXPECT_GE(n.earnings, run("greedy_potential", inst).earnings);
}

}  // namespace
}  // namespace pf
