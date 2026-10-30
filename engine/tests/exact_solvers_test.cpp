#include "pf/bounds.hpp"
#include "pf/game_session.hpp"
#include "pf/generator.hpp"
#include "pf/json_io.hpp"
#include "pf/registry.hpp"

#include <random>

#include <gtest/gtest.h>

#include "instance_files.hpp"
#include "test_util.hpp"

namespace pf {
namespace {

Solution run(const std::string& name, const Instance& instance, const SolveOptions& options = {}) {
  return SolverRegistry::builtin().create(name)->solve(instance, options, nullptr);
}

Instance sawmill() {
  return test::InstanceBuilder()
      .resource("wood", 1, 4)
      .resource("plank", 4)
      .process("saw", {{"wood", 2}}, {{"plank", 1}}, 2)
      .workers(1)
      .horizon(4)
      .build();
}

class ExactOnHandMade : public ::testing::TestWithParam<std::filesystem::path> {};

TEST_P(ExactOnHandMade, MatchesKnownOptimum) {
  const auto doc = test::loadJson(GetParam());
  const Instance inst = *json::parseInstance(doc).value;
  std::optional<Quantity> optimum;
  for (const auto& ref : doc.at("reference_solutions")) {
    if (ref.value("optimal", false)) optimum = ref.at("earnings").get<Quantity>();
  }
  ASSERT_TRUE(optimum.has_value());
  for (const char* name : {"dp", "branch_and_bound"}) {
    const Solution s = run(name, inst);
    ASSERT_TRUE(s.valid) << name << ": " << s.error.value_or("");
    EXPECT_TRUE(s.proven_optimal) << name;
    EXPECT_EQ(s.earnings, *optimum) << name;
  }
}

INSTANTIATE_TEST_SUITE_P(All, ExactOnHandMade, ::testing::ValuesIn(test::instanceFiles()),
                         [](const auto& param_info) { return param_info.param.stem().string(); });

TEST(DP, FailsCleanlyAtStateLimit) {
  SolveOptions options;
  options.params["max_states"] = 1;
  const Solution s = run("dp", sawmill(), options);
  EXPECT_FALSE(s.valid);
  EXPECT_FALSE(s.proven_optimal);
  ASSERT_TRUE(s.error.has_value());
  EXPECT_NE(s.error->find("state limit"), std::string::npos);
  EXPECT_EQ(s.stats.at("states"), 1);
}

TEST(DP, FailsCleanlyWhenCancelled) {
  const Instance inst = generate(GeneratorParams::large(1)).instance;
  std::atomic<bool> cancel{true};
  SolveOptions options;
  options.cancel = &cancel;
  const Solution s = run("dp", inst, options);
  EXPECT_FALSE(s.valid);
  ASSERT_TRUE(s.error.has_value());
}

TEST(DP, ReportsStates) {
  const Solution s = run("dp", sawmill());
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.earnings, 8);
  EXPECT_GT(s.stats.at("states"), 0);
}

TEST(BranchAndBound, ReportsNodeAndPruneCounters) {
  const Instance inst = generate(GeneratorParams::medium(3)).instance;
  SolveOptions options;
  options.time_limit_ms = 3000;
  const Solution s = run("branch_and_bound", inst, options);
  ASSERT_TRUE(s.valid);
  EXPECT_GT(s.stats.at("nodes"), 0);
  EXPECT_GT(s.stats.at("pruned"), 0);
  EXPECT_GE(s.stats.at("root_bound"), s.earnings);
  EXPECT_TRUE(s.stats.contains("transposition_hits"));
}

TEST(BranchAndBound, IsAnytimeWhenCancelled) {
  const Instance inst = generate(GeneratorParams::large(2)).instance;
  std::atomic<bool> cancel{true};
  SolveOptions options;
  options.cancel = &cancel;
  const Solution s = run("branch_and_bound", inst, options);
  ASSERT_TRUE(s.valid) << s.error.value_or("");
  EXPECT_FALSE(s.proven_optimal);
  EXPECT_GE(s.earnings, run("greedy_potential", inst).earnings);
}

TEST(BranchAndBound, NeverWorseThanGreedy) {
  for (std::uint64_t seed = 0; seed < 20; ++seed) {
    const Instance inst = generate(GeneratorParams::medium(seed)).instance;
    SolveOptions options;
    options.time_limit_ms = 200;
    const Solution bnb = run("branch_and_bound", inst, options);
    ASSERT_TRUE(bnb.valid);
    EXPECT_GE(bnb.earnings, run("greedy", inst).earnings) << seed;
    EXPECT_GE(bnb.earnings, run("greedy_potential", inst).earnings) << seed;
  }
}

TEST(BranchAndBound, FallsBackWhenBoundTablesTooLarge) {
  Instance inst = sawmill();
  inst.horizon = 1'000'000;
  for (int i = 0; i < 30; ++i) {
    inst.resources.push_back({"x" + std::to_string(i), "x", 1});
    inst.start_inventory.push_back(0);
  }
  const Solution s = run("branch_and_bound", inst);
  ASSERT_TRUE(s.valid) << s.error.value_or("");
  EXPECT_FALSE(s.proven_optimal);
  EXPECT_EQ(s.stats.at("bound_unavailable"), 1);
}

// --- Dual bound --------------------------------------------------------------------------

TEST(DualBound, IsTightOnSawmill) {
  const Instance inst = sawmill();
  const DualBound bound(inst);
  EXPECT_EQ(bound.upperBound(SimState(inst)), 8);
}

TEST(DualBound, PaysForInputlessProcessesWithLambda) {
  const Instance inst = test::InstanceBuilder()
                            .resource("coin", 3)
                            .process("mint", {}, {{"coin", 1}}, 2)
                            .workers(2)
                            .horizon(6)
                            .build();
  const DualBound bound(inst);
  EXPECT_NEAR(bound.lambdas().front(), 1.5, 1e-6);  // 3 per 2 ticks
  // 2 workers x 3 runs x 3 = 18, and the bound is exact here.
  EXPECT_EQ(bound.upperBound(SimState(inst)), 18);
  EXPECT_EQ(run("dp", inst).earnings, 18);
}

TEST(DualBound, IsAdmissibleAtRootOnSmallInstances) {
  for (int i = 0; i < 1000; ++i) {
    const Instance inst = generate(GeneratorParams::small(static_cast<std::uint64_t>(i))).instance;
    const Solution best = run("dp", inst);
    ASSERT_TRUE(best.proven_optimal);
    EXPECT_GE(DualBound(inst).upperBound(SimState(inst)), best.earnings) << "instance " << i;
  }
}

TEST(DualBound, BoundsEveryStateAlongRandomGames) {
  std::mt19937_64 rng(77);
  for (int i = 0; i < 300; ++i) {
    const Instance inst = generate(GeneratorParams::medium(static_cast<std::uint64_t>(i))).instance;
    const DualBound bound(inst);
    GameSession session(inst);
    std::vector<Quantity> bounds;
    while (!session.finished()) {
      bounds.push_back(bound.upperBound(session.sim()));
      auto legal = session.legalActions();
      if (legal.empty()) break;
      ASSERT_FALSE(session.apply(
          legal[std::uniform_int_distribution<std::size_t>(0, legal.size() - 1)(rng)]));
    }
    const Quantity final_earnings = session.finish().earnings;
    for (const Quantity b : bounds) ASSERT_GE(b, final_earnings) << "instance " << i;
  }
}

}  // namespace
}  // namespace pf
