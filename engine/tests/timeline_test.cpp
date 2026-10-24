#include "pf/timeline.hpp"

#include "pf/generator.hpp"
#include "pf/registry.hpp"
#include "pf/validator.hpp"

#include <gtest/gtest.h>

#include "test_util.hpp"

namespace pf {
namespace {

TEST(Timeline, LaysOutRunsOnSlotsAndTracksInventory) {
  const Instance inst = test::InstanceBuilder()
                            .resource("ore", 0, 6)
                            .resource("iron", 3)
                            .resource("steel", 10)
                            .process("smelt", {{"ore", 2}}, {{"iron", 1}}, 2)
                            .process("forge", {{"iron", 2}}, {{"steel", 1}}, 3)
                            .workers(2)
                            .horizon(6)
                            .build();
  const auto t = buildTimeline(inst, {{0, 0}, {0, 0}, {1, 2}, {0, 2}});
  ASSERT_TRUE(t.has_value());
  EXPECT_EQ(t->earnings, 13);
  ASSERT_EQ(t->runs.size(), 4U);
  EXPECT_EQ(t->runs[0].slot, 0);
  EXPECT_EQ(t->runs[1].slot, 1);
  EXPECT_EQ(t->runs[2].slot, 0);  // both slots free again at tick 2
  EXPECT_EQ(t->runs[3].slot, 1);
  std::vector<Tick> ticks;
  for (const auto& p : t->points) ticks.push_back(p.tick);
  EXPECT_EQ(ticks, (std::vector<Tick>{0, 2, 4, 5, 6}));
  EXPECT_EQ(t->points[0].inventory, (std::vector<Quantity>{2, 0, 0}));  // after both starts
  EXPECT_EQ(t->points[1].inventory, (std::vector<Quantity>{0, 0, 0}));  // 2 iron in, 2 out
  EXPECT_EQ(t->points.back().inventory, (std::vector<Quantity>{0, 1, 1}));
  EXPECT_EQ(t->points.back().value, 13);
  EXPECT_EQ(t->points[1].busy_workers, 2);
}

TEST(Timeline, RejectsInvalidSchedules) {
  const Instance inst = test::InstanceBuilder()
                            .resource("a", 1, 1)
                            .process("p", {{"a", 1}}, {}, 1)
                            .workers(1)
                            .horizon(3)
                            .build();
  EXPECT_FALSE(buildTimeline(inst, {{0, 0}, {0, 1}}).has_value());
}

TEST(Timeline, SlotsNeverOverlapOnGeneratedSchedules) {
  for (std::uint64_t seed = 0; seed < 50; ++seed) {
    const Instance inst = generate(GeneratorParams::medium(seed)).instance;
    const Solution s = SolverRegistry::builtin().create("beam_search")->solve(inst, {}, nullptr);
    const auto t = buildTimeline(inst, s.runs);
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(t->earnings, s.earnings);
    for (std::size_t i = 0; i < t->runs.size(); ++i) {
      ASSERT_GE(t->runs[i].slot, 0);
      ASSERT_LT(t->runs[i].slot, inst.workers);
      for (std::size_t j = i + 1; j < t->runs.size(); ++j) {
        const auto& a = t->runs[i];
        const auto& b = t->runs[j];
        if (a.slot == b.slot) {
          ASSERT_TRUE(a.end <= b.start || b.end <= a.start);
        }
      }
    }
  }
}

}  // namespace
}  // namespace pf
