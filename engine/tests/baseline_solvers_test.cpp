#include "pf/json_io.hpp"
#include "pf/registry.hpp"
#include "pf/solvers/brute_force.hpp"
#include "pf/solvers/greedy.hpp"

#include <random>
#include <stdexcept>

#include <gtest/gtest.h>

#include "instance_files.hpp"
#include "test_util.hpp"

namespace pf {
namespace {

Solution run(const std::string& name, const Instance& instance, const SolveOptions& options = {}) {
  const auto solver = SolverRegistry::builtin().create(name);
  return solver->solve(instance, options, nullptr);
}

// --- Solver base class -------------------------------------------------------------------

class FixedSolver final : public Solver {
 public:
  explicit FixedSolver(Schedule runs, bool throws = false)
      : runs_(std::move(runs)), throws_(throws) {}
  [[nodiscard]] std::string name() const override { return "fixed"; }

 protected:
  RawSolution solveRaw(const Instance& /*instance*/, const SolveOptions& /*options*/,
                       const Deadline& /*deadline*/, ProgressReporter& progress) override {
    if (throws_) throw std::runtime_error("boom");
    progress.improved(1'000'000);  // lies about its earnings
    return {runs_, true, {{"answer", 42}}, std::nullopt};
  }

 private:
  Schedule runs_;
  bool throws_;
};

class RecordingSink final : public ProgressSink {
 public:
  void onProgress(const ProgressEvent& e) override { events.push_back(e); }
  std::vector<ProgressEvent> events;
};

Instance sawmill() {
  return test::InstanceBuilder()
      .resource("wood", 1, 4)
      .resource("plank", 4)
      .process("saw", {{"wood", 2}}, {{"plank", 1}}, 2)
      .workers(1)
      .horizon(4)
      .build();
}

TEST(SolverBase, EarningsComeFromTheValidator) {
  FixedSolver solver({{0, 0}});
  RecordingSink sink;
  const Solution s = solver.solve(sawmill(), {}, &sink);
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.earnings, 2 + 4);  // not the 1'000'000 the solver claimed
  EXPECT_TRUE(s.proven_optimal);
  EXPECT_EQ(s.solver_name, "fixed");
  EXPECT_EQ(s.stats.at("answer"), 42);
  EXPECT_GE(s.runtime_ms, 0);
  ASSERT_EQ(sink.events.size(), 1U);
  EXPECT_EQ(sink.events[0].solver, "fixed");
}

TEST(SolverBase, InvalidScheduleBecomesAnError) {
  FixedSolver solver({{0, 3}});
  const Solution s = solver.solve(sawmill(), {}, nullptr);
  EXPECT_FALSE(s.valid);
  EXPECT_FALSE(s.proven_optimal);
  ASSERT_TRUE(s.error.has_value());
  EXPECT_NE(s.error->find("invalid schedule"), std::string::npos);
}

TEST(SolverBase, ExceptionsBecomeAnError) {
  FixedSolver solver({}, /*throws=*/true);
  const Solution s = solver.solve(sawmill(), {}, nullptr);
  EXPECT_FALSE(s.valid);
  EXPECT_EQ(s.error, "solver failed: boom");
}

TEST(Deadline, HonorsCancelFlag) {
  std::atomic<bool> cancel{false};
  SolveOptions options;
  options.cancel = &cancel;
  const Deadline d(options);
  EXPECT_FALSE(d.expired());
  cancel = true;
  EXPECT_TRUE(d.expired());
}

// --- Greedy ------------------------------------------------------------------------------

TEST(Greedy, MarketScorePrefersBestGainPerTick) {
  // slow: +6 over 3 ticks (2/tick); fast: +3 over 1 tick (3/tick). One input, one worker.
  const Instance inst = test::InstanceBuilder()
                            .resource("x", 1, 1)
                            .resource("a", 7)
                            .resource("b", 4)
                            .process("slow", {{"x", 1}}, {{"a", 1}}, 3)
                            .process("fast", {{"x", 1}}, {{"b", 1}}, 1)
                            .workers(1)
                            .horizon(5)
                            .build();
  const Solution s = run("greedy", inst);
  ASSERT_TRUE(s.valid);
  EXPECT_EQ(s.runs, (Schedule{{1, 0}}));
  EXPECT_EQ(s.earnings, 4);
  EXPECT_FALSE(s.proven_optimal);
}

TEST(Greedy, NeverStartsUnprofitableRuns) {
  const Instance inst = test::InstanceBuilder()
                            .resource("gold", 10, 3)
                            .resource("lead", 1)
                            .process("ruin", {{"gold", 1}}, {{"lead", 1}}, 1)
                            .workers(1)
                            .horizon(5)
                            .build();
  EXPECT_TRUE(run("greedy", inst).runs.empty());
  EXPECT_TRUE(run("greedy_potential", inst).runs.empty());
}

TEST(Greedy, PotentialVariantSeesThroughWorthlessIntermediates) {
  // ore -> iron (price 0) -> steel (price 10). Market greedy sees smelt as worthless.
  const Instance inst = test::InstanceBuilder()
                            .resource("ore", 0, 2)
                            .resource("iron", 0)
                            .resource("steel", 10)
                            .process("smelt", {{"ore", 1}}, {{"iron", 1}}, 1)
                            .process("forge", {{"iron", 1}}, {{"steel", 1}}, 1)
                            .workers(1)
                            .horizon(4)
                            .build();
  EXPECT_EQ(run("greedy", inst).earnings, 0);
  EXPECT_EQ(run("greedy_potential", inst).earnings, 20);
}

TEST(PotentialValues, AreTimeAware) {
  const Instance inst = test::InstanceBuilder()
                            .resource("ore", 0)
                            .resource("iron", 0)
                            .resource("steel", 10)
                            .process("smelt", {{"ore", 2}}, {{"iron", 1}}, 2)
                            .process("forge", {{"iron", 1}}, {{"steel", 1}}, 3)
                            .workers(1)
                            .horizon(10)
                            .build();
  const PotentialValues v(inst);
  EXPECT_DOUBLE_EQ(v.value(2, 0), 10);
  EXPECT_DOUBLE_EQ(v.value(1, 2), 0);   // forge needs 3 ticks
  EXPECT_DOUBLE_EQ(v.value(1, 3), 10);  // iron -> steel
  EXPECT_DOUBLE_EQ(v.value(0, 4), 0);   // 2 + 3 ticks needed
  EXPECT_DOUBLE_EQ(v.value(0, 5), 5);   // 2 ore -> 1 iron -> 1 steel
  EXPECT_DOUBLE_EQ(v.value(0, 999), 5);
}

// --- Brute force -------------------------------------------------------------------------

TEST(BruteForce, RefusesLargeInstances) {
  Instance inst = sawmill();
  inst.horizon = BruteForceSolver::kMaxHorizon + 1;
  const Solution s = run("brute_force", inst);
  EXPECT_FALSE(s.valid);
  ASSERT_TRUE(s.error.has_value());
  EXPECT_NE(s.error->find("too large"), std::string::npos);
}

TEST(BruteForce, FailsCleanlyAtNodeLimit) {
  SolveOptions options;
  options.params["max_nodes"] = 3;
  const Solution s = run("brute_force", sawmill(), options);
  EXPECT_FALSE(s.valid);
  ASSERT_TRUE(s.error.has_value());
  EXPECT_NE(s.error->find("node limit"), std::string::npos);
}

TEST(BruteForce, FindsOptimumThatNeedsWaiting) {
  // Starting 'cheap' at tick 0 would use up the input that 'rich' needs once 'key' arrives.
  const Instance inst = test::InstanceBuilder()
                            .resource("in", 1, 1)
                            .resource("key", 0)
                            .resource("low", 2)
                            .resource("high", 20)
                            .process("make_key", {}, {{"key", 1}}, 2, /*max_runs=*/1)
                            .process("cheap", {{"in", 1}}, {{"low", 1}}, 1)
                            .process("rich", {{"in", 1}, {"key", 1}}, {{"high", 1}}, 1)
                            .workers(2)
                            .horizon(4)
                            .build();
  const Solution s = run("brute_force", inst);
  ASSERT_TRUE(s.valid);
  EXPECT_TRUE(s.proven_optimal);
  EXPECT_EQ(s.earnings, 20);
  EXPECT_GT(s.stats.at("nodes"), 0);
}

// --- Hand-made instances -----------------------------------------------------------------

class BaselineOnHandMade : public ::testing::TestWithParam<std::filesystem::path> {};

TEST_P(BaselineOnHandMade, AllSolversValidAndBruteForceIsOptimal) {
  const auto doc = test::loadJson(GetParam());
  const Instance inst = *json::parseInstance(doc).value;

  std::optional<Quantity> known_optimum;
  for (const auto& ref : doc.at("reference_solutions")) {
    if (ref.value("optimal", false)) known_optimum = ref.at("earnings").get<Quantity>();
  }

  for (const char* name : {"greedy", "greedy_potential"}) {
    const Solution s = run(name, inst);
    ASSERT_TRUE(s.valid) << name << ": " << s.error.value_or("");
    if (known_optimum) {
      EXPECT_LE(s.earnings, *known_optimum) << name;
    }
  }

  SolveOptions options;
  options.time_limit_ms = 2000;
  const Solution bf = run("brute_force", inst, options);
  if (!bf.valid || !bf.proven_optimal) {
    // Only acceptable on instances too big for exhaustive search (no hand-computed optimum).
    EXPECT_FALSE(known_optimum.has_value()) << bf.error.value_or("timed out");
    return;
  }
  if (known_optimum) {
    EXPECT_EQ(bf.earnings, *known_optimum);
  }
  for (const char* name : {"greedy", "greedy_potential"}) {
    EXPECT_GE(bf.earnings, run(name, inst).earnings) << name;
  }
}

INSTANTIATE_TEST_SUITE_P(All, BaselineOnHandMade, ::testing::ValuesIn(test::instanceFiles()),
                         [](const auto& param_info) { return param_info.param.stem().string(); });

// --- Tiny random instances ---------------------------------------------------------------

// A deliberately simple tiny-instance maker; milestone 3 replaces it with the real generator.
Instance tinyRandom(std::mt19937_64& rng) {
  auto pick = [&](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };
  test::InstanceBuilder b;
  const int resources = pick(2, 4);
  for (int r = 0; r < resources; ++r) {
    b.resource("r" + std::to_string(r), pick(0, 6), pick(0, 4));
  }
  const int processes = pick(1, 4);
  for (int p = 0; p < processes; ++p) {
    const int in = pick(0, resources - 1);
    int out = pick(0, resources - 1);
    if (out == in) out = (out + 1) % resources;
    std::vector<std::pair<std::string, Quantity>> inputs;
    if (pick(0, 4) != 0) inputs.emplace_back("r" + std::to_string(in), pick(1, 3));
    std::optional<std::int32_t> max_runs;
    if (pick(0, 3) == 0) max_runs = pick(0, 3);
    b.process("p" + std::to_string(p), inputs, {{"r" + std::to_string(out), pick(1, 3)}},
              pick(1, 3), max_runs);
  }
  return b.workers(pick(1, 2)).horizon(pick(1, 6)).build();
}

TEST(BaselineOnTinyRandom, BruteForceDominatesGreedy) {
  std::mt19937_64 rng(12345);
  for (int i = 0; i < 300; ++i) {
    const Instance inst = tinyRandom(rng);
    ASSERT_TRUE(checkInstance(inst).empty());
    const Solution bf = run("brute_force", inst);
    ASSERT_TRUE(bf.valid) << "instance " << i << ": " << bf.error.value_or("");
    ASSERT_TRUE(bf.proven_optimal);
    for (const char* name : {"greedy", "greedy_potential"}) {
      const Solution g = run(name, inst);
      ASSERT_TRUE(g.valid) << name << " instance " << i;
      EXPECT_GE(bf.earnings, g.earnings) << name << " instance " << i;
    }
  }
}

TEST(BaselineOnTinyRandom, Deterministic) {
  std::mt19937_64 rng(7);
  for (int i = 0; i < 50; ++i) {
    const Instance inst = tinyRandom(rng);
    for (const char* name : {"greedy", "greedy_potential", "brute_force"}) {
      EXPECT_EQ(run(name, inst).runs, run(name, inst).runs) << name;
    }
  }
}

}  // namespace
}  // namespace pf
