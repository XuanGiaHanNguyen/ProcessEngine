#include "pf/factory/json.hpp"
#include "pf/factory/sim.hpp"
#include "pf/factory/solvers.hpp"

#include <atomic>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "instance_files.hpp"

namespace pf::factory {
namespace {

using Json = nlohmann::json;

struct LoadedLevel {
  std::string file;
  Level level;
  std::vector<Cents> reference_profits;
};

std::vector<LoadedLevel> loadLevels() {
  std::vector<LoadedLevel> out;
  for (const auto& f : test::factoryLevelFiles()) {
    const Json doc = test::loadJson(f);
    LoadedLevel l{f.filename().string(), *json::parseLevel(doc).value, {}};
    for (const Json& ref : doc["reference_designs"])
      l.reference_profits.push_back(ref["profit"].get<Cents>());
    out.push_back(std::move(l));
  }
  return out;
}

SolveOptions quick(double ms = 300) {
  SolveOptions o;
  o.time_limit_ms = ms;
  return o;
}

TEST(FactorySolvers, RegistryListsAndBuildsEverySolver) {
  const auto infos = factorySolvers();
  ASSERT_EQ(infos.size(), 3U);
  for (const auto& info : infos) {
    auto s = makeFactorySolver(info.name);
    ASSERT_NE(s, nullptr) << info.name;
    EXPECT_EQ(s->name(), info.name);
  }
  EXPECT_EQ(makeFactorySolver("nope"), nullptr);
}

TEST(FactorySolvers, FlowBoundIsAboveEveryReferenceDesign) {
  for (const auto& l : loadLevels()) {
    const FlowBound b = flowUpperBound(l.level, quick(2000));
    EXPECT_TRUE(b.solved) << l.file;
    for (const Cents p : l.reference_profits) EXPECT_LE(p, b.bound) << l.file;
  }
}

TEST(FactorySolvers, EverySolverReturnsAValidDesignUnderTheBound) {
  for (const auto& l : loadLevels()) {
    const Cents bound = flowUpperBound(l.level, quick(2000)).bound;
    for (const auto& info : factorySolvers()) {
      SCOPED_TRACE(l.file + " / " + info.name);
      const FactorySolution s = makeFactorySolver(info.name)->solve(l.level, quick());
      ASSERT_TRUE(s.valid) << s.error.value_or("");
      EXPECT_TRUE(checkDesign(l.level, s.design).empty());
      EXPECT_EQ(s.profit, simulate(l.level, s.design, false).profit);
      EXPECT_GE(s.profit, 0);  // building nothing earns 0, and every solver can do that
      EXPECT_LE(s.profit, bound);
    }
  }
}

TEST(FactorySolvers, SolversBeatTheInitialDesigns) {
  for (const auto& l : loadLevels()) {
    const Cents initial = l.reference_profits.front();
    const FactorySolution s = makeFactorySolver("local_search")->solve(l.level, quick(800));
    EXPECT_GT(s.profit, initial) << l.file;
  }
}

TEST(FactorySolvers, GreedyIsDeterministic) {
  const auto levels = loadLevels();
  const Level& level = levels[2].level;
  const auto a = makeFactorySolver("greedy")->solve(level, quick(5000));
  const auto b = makeFactorySolver("greedy")->solve(level, quick(5000));
  EXPECT_EQ(a.design, b.design);
  EXPECT_EQ(a.profit, b.profit);
}

TEST(FactorySolvers, CancelStopsLocalSearch) {
  const auto levels = loadLevels();
  std::atomic<bool> cancel{true};
  SolveOptions o = quick(60'000);
  o.cancel = &cancel;
  const auto s = makeFactorySolver("local_search")->solve(levels[3].level, o);
  EXPECT_TRUE(s.valid);
  EXPECT_LT(s.runtime_ms, 5000);
}

TEST(FactorySolvers, AutoWiredDesignsAreValid) {
  for (const auto& l : loadLevels()) {
    std::vector<MachineChoice> machines;
    for (Index t = 0; t < l.level.machine_types.size() && machines.size() < l.level.slots.size();
         ++t) {
      machines.emplace_back(t, 0);
    }
    Design d = autoWiredDesign(l.level, machines, std::vector<int>(l.level.suppliers.size(), 1));
    if (l.level.budget) continue;  // a machine of every type may exceed the budget
    EXPECT_TRUE(checkDesign(l.level, d).empty()) << l.file;
  }
}

}  // namespace
}  // namespace pf::factory
