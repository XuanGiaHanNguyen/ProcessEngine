#include "pf/model.hpp"

#include "pf/limits.hpp"

#include <stdexcept>

#include <gtest/gtest.h>

#include "test_util.hpp"

namespace pf {
namespace {

Instance validInstance() {
  return test::InstanceBuilder()
      .resource("wood", 1, 4)
      .resource("plank", 4)
      .process("saw", {{"wood", 2}}, {{"plank", 1}}, 2)
      .workers(1)
      .horizon(4)
      .build();
}

bool hasErrorContaining(const std::vector<std::string>& errors, const std::string& needle) {
  for (const auto& e : errors) {
    if (e.find(needle) != std::string::npos) return true;
  }
  return false;
}

TEST(CheckInstance, AcceptsValidInstance) { EXPECT_TRUE(checkInstance(validInstance()).empty()); }

TEST(CheckInstance, FindsLookups) {
  const Instance i = validInstance();
  EXPECT_EQ(i.findResource("plank"), 1U);
  EXPECT_EQ(i.findProcess("saw"), 0U);
  EXPECT_FALSE(i.findResource("gold").has_value());
  EXPECT_FALSE(i.findProcess("mine").has_value());
}

TEST(CheckInstance, RejectsDuplicateIds) {
  Instance i = validInstance();
  i.resources.push_back({"wood", "Wood again", 1});
  i.start_inventory.push_back(0);
  i.processes.push_back(i.processes[0]);
  const auto errors = checkInstance(i);
  EXPECT_TRUE(hasErrorContaining(errors, "duplicate resource id 'wood'"));
  EXPECT_TRUE(hasErrorContaining(errors, "duplicate process id 'saw'"));
}

TEST(CheckInstance, RejectsBadNumbers) {
  Instance i = validInstance();
  i.workers = 0;
  i.horizon = -1;
  i.start_money = -5;
  i.processes[0].duration = 0;
  i.processes[0].max_runs = -1;
  i.resources[0].sell_price = -1;
  i.start_inventory[0] = -3;
  const auto errors = checkInstance(i);
  EXPECT_TRUE(hasErrorContaining(errors, "workers"));
  EXPECT_TRUE(hasErrorContaining(errors, "horizon"));
  EXPECT_TRUE(hasErrorContaining(errors, "start_money"));
  EXPECT_TRUE(hasErrorContaining(errors, "duration"));
  EXPECT_TRUE(hasErrorContaining(errors, "max_runs"));
  EXPECT_TRUE(hasErrorContaining(errors, "sell_price"));
  EXPECT_TRUE(hasErrorContaining(errors, "start_inventory of 'wood'"));
}

TEST(CheckInstance, RejectsBadAmounts) {
  Instance i = validInstance();
  i.processes[0].inputs = {{1, 1}, {0, 1}};  // unsorted
  i.processes[0].outputs = {{7, 1}};         // out of range
  const auto errors = checkInstance(i);
  EXPECT_TRUE(hasErrorContaining(errors, "sorted"));
  EXPECT_TRUE(hasErrorContaining(errors, "out of range"));

  i = validInstance();
  i.processes[0].inputs[0].qty = 0;
  EXPECT_TRUE(hasErrorContaining(checkInstance(i), "quantity of 'wood'"));
}

TEST(CheckInstance, RejectsInventorySizeMismatch) {
  Instance i = validInstance();
  i.start_inventory.pop_back();
  EXPECT_TRUE(hasErrorContaining(checkInstance(i), "one entry per resource"));
}

TEST(InventoryValue, SumsPricesPlusStartMoney) {
  Instance i = validInstance();
  i.start_money = 7;
  EXPECT_EQ(inventoryValue(i, {3, 2}), 7 + 3 * 1 + 2 * 4);
}

TEST(InventoryValue, ThrowsOnOverflow) {
  Instance i = validInstance();
  i.resources[0].sell_price = limits::kMaxPrice;
  EXPECT_THROW((void)inventoryValue(i, {std::numeric_limits<Quantity>::max() / 2, 0}),
               std::overflow_error);
}

}  // namespace
}  // namespace pf
