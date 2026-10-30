#include "pf/rules.hpp"

#include <gtest/gtest.h>

#include "test_util.hpp"

namespace pf {
namespace {

Instance chain() {
  return test::InstanceBuilder()
      .resource("ore", 0, 4)
      .resource("iron", 3)
      .process("smelt", {{"ore", 2}}, {{"iron", 1}}, 2)
      .process("refine", {{"iron", 1}}, {{"ore", 3}}, 1, /*max_runs=*/1)
      .workers(2)
      .horizon(5)
      .build();
}

TEST(SimState, StartsAtTickZeroWithStartInventory) {
  const Instance inst = chain();
  const SimState s(inst);
  EXPECT_EQ(s.tick(), 0);
  EXPECT_EQ(s.inventory(), (std::vector<Quantity>{4, 0}));
  EXPECT_EQ(s.freeWorkers(), 2);
  EXPECT_FALSE(s.nextEventTick().has_value());
}

TEST(SimState, StartConsumesInputsImmediately) {
  const Instance inst = chain();
  SimState s(inst);
  ASSERT_FALSE(s.checkStart(0).has_value());
  s.start(0);
  EXPECT_EQ(s.inventory()[0], 2);
  EXPECT_EQ(s.freeWorkers(), 1);
  EXPECT_EQ(s.runCounts()[0], 1);
  EXPECT_EQ(s.nextEventTick(), 2);
}

TEST(SimState, OutputsArriveExactlyAtEndTick) {
  const Instance inst = chain();
  SimState s(inst);
  s.start(0);
  s.advanceTo(1);
  EXPECT_EQ(s.inventory()[1], 0);
  EXPECT_EQ(s.freeWorkers(), 1);
  s.advanceTo(2);
  EXPECT_EQ(s.inventory()[1], 1);
  EXPECT_EQ(s.freeWorkers(), 2);
  EXPECT_TRUE(s.activeRuns().empty());
}

TEST(SimState, ActiveRunsSortedByEnd) {
  const Instance inst = chain();
  SimState s(inst);
  s.start(0);  // ends at 2
  s.advanceTo(2);
  s.start(1);  // refine: ends at 3
  s.start(0);  // smelt: ends at 4
  ASSERT_EQ(s.activeRuns().size(), 2U);
  EXPECT_EQ(s.activeRuns()[0].end, 3);
  EXPECT_EQ(s.activeRuns()[1].end, 4);
  EXPECT_EQ(s.nextEventTick(), 3);
}

TEST(SimState, CheckStartReportsEachRule) {
  const Instance inst = chain();
  SimState s(inst);
  EXPECT_EQ(s.checkStart(9)->kind, RuleViolation::UnknownProcess);

  const auto noIron = s.checkStart(1);
  ASSERT_TRUE(noIron.has_value());
  EXPECT_EQ(noIron->kind, RuleViolation::InsufficientInventory);
  EXPECT_EQ(noIron->message, "not enough iron (need 1, have 0)");

  s.start(0);
  s.start(0);
  EXPECT_EQ(s.checkStart(0)->kind, RuleViolation::NoFreeWorker);
  EXPECT_EQ(s.checkStart(0)->message, "no free worker (2/2 busy)");

  s.advanceTo(4);  // 2 iron
  s.start(1);
  s.advanceTo(5);
  EXPECT_EQ(s.checkStart(1)->kind, RuleViolation::PastHorizon);
  EXPECT_NE(s.checkStart(1)->message.find("after the horizon 5"), std::string::npos);
}

TEST(SimState, MaxRunsChecked) {
  const Instance inst = chain();
  SimState s(inst);
  s.start(0);
  s.start(0);
  s.advanceTo(2);
  s.start(1);
  const auto e = s.checkStart(1);
  ASSERT_TRUE(e.has_value());
  EXPECT_EQ(e->kind, RuleViolation::MaxRunsExceeded);
  EXPECT_EQ(e->message, "'refine' already ran 1 times (max_runs 1)");
}

TEST(SimState, EqualityComparesFullState) {
  const Instance inst = chain();
  SimState a(inst);
  SimState b(inst);
  EXPECT_EQ(a, b);
  a.start(0);
  EXPECT_NE(a, b);
  b.start(0);
  EXPECT_EQ(a, b);
}

TEST(RuleViolation, HasStableNames) {
  EXPECT_STREQ(toString(RuleViolation::InsufficientInventory), "insufficient_inventory");
  EXPECT_STREQ(toString(RuleViolation::NoFreeWorker), "no_free_worker");
  EXPECT_STREQ(toString(RuleViolation::PastHorizon), "past_horizon");
}

}  // namespace
}  // namespace pf
