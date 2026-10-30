#include "pf/validator.hpp"

#include "pf/limits.hpp"

#include <gtest/gtest.h>

#include "test_util.hpp"

namespace pf {
namespace {

// wood(1) x4 --saw(2 ticks)--> plank(4); kiln capped at 1 run.
Instance workshop() {
  return test::InstanceBuilder()
      .resource("wood", 1, 4)
      .resource("plank", 4)
      .resource("charcoal", 2)
      .process("saw", {{"wood", 2}}, {{"plank", 1}}, 2)
      .process("kiln", {{"wood", 1}}, {{"charcoal", 1}}, 1, /*max_runs=*/1)
      .workers(2)
      .horizon(6)
      .startMoney(10)
      .build();
}

constexpr ProcessIndex kSaw = 0;
constexpr ProcessIndex kKiln = 1;

TEST(Validator, EmptyScheduleSellsStartInventory) {
  const auto r = validate(workshop(), {});
  ASSERT_TRUE(r.valid);
  EXPECT_FALSE(r.violation.has_value());
  EXPECT_EQ(r.earnings, 10 + 4);
  EXPECT_EQ(r.final_inventory, (std::vector<Quantity>{4, 0, 0}));
}

TEST(Validator, ComputesEarningsOfValidSchedule) {
  // wood 4 -> saw@0 (-2) and kiln@0 (-1) leave 1 wood; plank arrives at 2, charcoal at 1.
  const auto r = validate(workshop(), {{kSaw, 0}, {kKiln, 0}});
  ASSERT_TRUE(r.valid);
  EXPECT_EQ(r.final_inventory, (std::vector<Quantity>{1, 1, 1}));
  EXPECT_EQ(r.earnings, 10 + 1 * 1 + 1 * 4 + 1 * 2);
}

TEST(Validator, ScheduleOrderDoesNotMatter) {
  const Instance inst = workshop();
  const auto a = validate(inst, {{kSaw, 2}, {kKiln, 0}});
  const auto b = validate(inst, {{kKiln, 0}, {kSaw, 2}});
  ASSERT_TRUE(a.valid);
  ASSERT_TRUE(b.valid);
  EXPECT_EQ(a.earnings, b.earnings);
  EXPECT_EQ(a.earnings, 10 + 1 + 4 + 2);
}

TEST(Validator, RunMayEndExactlyAtHorizon) {
  const auto r = validate(workshop(), {{kSaw, 4}});
  ASSERT_TRUE(r.valid);
  EXPECT_EQ(r.final_inventory[1], 1);
}

TEST(Validator, OutputsUsableAtTheirArrivalTick) {
  const Instance inst = test::InstanceBuilder()
                            .resource("a", 0, 1)
                            .resource("b", 0)
                            .resource("c", 5)
                            .process("ab", {{"a", 1}}, {{"b", 1}}, 2)
                            .process("bc", {{"b", 1}}, {{"c", 1}}, 1)
                            .workers(1)
                            .horizon(3)
                            .build();
  const auto r = validate(inst, {{0, 0}, {1, 2}});
  ASSERT_TRUE(r.valid) << r.violation->message;
  EXPECT_EQ(r.earnings, 5);

  const auto early = validate(inst, {{0, 0}, {1, 1}});
  ASSERT_FALSE(early.valid);
  EXPECT_EQ(early.violation->kind, RuleViolation::NoFreeWorker);
}

TEST(Validator, RejectsNegativeInventory) {
  const auto r = validate(workshop(), {{kSaw, 0}, {kSaw, 0}, {kKiln, 2}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::InsufficientInventory);
  EXPECT_EQ(r.violation->run_index, 2U);
  EXPECT_EQ(r.violation->tick, 2);
  EXPECT_EQ(r.violation->message, "run #2 at tick 2: not enough wood (need 1, have 0)");
}

TEST(Validator, RejectsWorkerOverflow) {
  const Instance inst = test::InstanceBuilder()
                            .resource("wood", 1, 10)
                            .resource("plank", 4)
                            .process("saw", {{"wood", 2}}, {{"plank", 1}}, 2)
                            .workers(2)
                            .horizon(6)
                            .build();
  const auto r = validate(inst, {{kSaw, 0}, {kSaw, 1}, {kSaw, 1}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::NoFreeWorker);
  EXPECT_EQ(r.violation->run_index, 2U);
  // A slot freed at tick 2 is usable at tick 2.
  EXPECT_TRUE(validate(inst, {{kSaw, 0}, {kSaw, 1}, {kSaw, 2}}).valid);
}

TEST(Validator, RejectsRunPastHorizon) {
  const auto r = validate(workshop(), {{kSaw, 5}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::PastHorizon);
  EXPECT_EQ(r.violation->message,
            "run #0 at tick 5: 'saw' would finish at tick 7, after the horizon 6");
}

TEST(Validator, RejectsStartAfterHorizon) {
  const auto r = validate(workshop(), {{kKiln, 7}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::PastHorizon);
}

TEST(Validator, RejectsNegativeStartTick) {
  const auto r = validate(workshop(), {{kKiln, -1}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::StartInPast);
}

TEST(Validator, RejectsUnknownProcess) {
  const auto r = validate(workshop(), {{kSaw, 0}, {42, 1}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::UnknownProcess);
  EXPECT_EQ(r.violation->run_index, 1U);
}

TEST(Validator, RejectsExceededMaxRuns) {
  const auto r = validate(workshop(), {{kKiln, 0}, {kKiln, 1}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::MaxRunsExceeded);
  EXPECT_EQ(r.violation->run_index, 1U);
}

TEST(Validator, MaxRunsZeroForbidsProcess) {
  Instance inst = workshop();
  inst.processes[kKiln].max_runs = 0;
  EXPECT_EQ(validate(inst, {{kKiln, 0}}).violation->kind, RuleViolation::MaxRunsExceeded);
}

TEST(Validator, ReportsEarliestViolationInTime) {
  // Entry 0 is invalid at tick 5, entry 1 at tick 0: the tick-0 one is reported.
  const auto r = validate(workshop(), {{kSaw, 5}, {42, 0}});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->run_index, 1U);
}

TEST(Validator, ReportsEarningsOverflow) {
  Instance inst = test::InstanceBuilder()
                      .resource("gold", limits::kMaxPrice, limits::kMaxStartQty)
                      .workers(1)
                      .horizon(1)
                      .startMoney(limits::kMaxStartMoney)
                      .build();
  inst.resources.push_back({"silver", "silver", limits::kMaxPrice});
  inst.start_inventory.push_back(std::numeric_limits<Quantity>::max() / 2);
  const auto r = validate(inst, {});
  ASSERT_FALSE(r.valid);
  EXPECT_EQ(r.violation->kind, RuleViolation::Overflow);
}

TEST(Validator, ZeroHorizonOnlyAllowsEmptySchedule) {
  Instance inst = workshop();
  inst.horizon = 0;
  EXPECT_TRUE(validate(inst, {}).valid);
  EXPECT_EQ(validate(inst, {{kKiln, 0}}).violation->kind, RuleViolation::PastHorizon);
}

}  // namespace
}  // namespace pf
