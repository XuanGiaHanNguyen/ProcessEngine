#include "pf/factory/hint.hpp"
#include "pf/factory/json.hpp"
#include "pf/factory/sim.hpp"

#include <gtest/gtest.h>

#include "instance_files.hpp"

namespace pf::factory {
namespace {

using Json = nlohmann::json;

struct Loaded {
  Level level;
  Design initial;
};

Loaded load(std::size_t file) {
  const Json doc = test::loadJson(test::factoryLevelFiles().at(file));
  Level level = *json::parseLevel(doc).value;
  Design initial = *json::parseDesign(doc["reference_designs"][0]["design"], level).design;
  return {std::move(level), std::move(initial)};
}

TEST(FactoryHint, ImprovesTheInitialDesignWithAValidScoredChange) {
  for (std::size_t f = 0; f < test::factoryLevelFiles().size(); ++f) {
    const auto [level, initial] = load(f);
    const auto hint = suggestChange(level, initial);
    ASSERT_TRUE(hint.has_value()) << level.id;
    EXPECT_TRUE(checkDesign(level, hint->design).empty());
    EXPECT_EQ(hint->profit, simulate(level, hint->design).profit);
    EXPECT_EQ(hint->gain, hint->profit - simulate(level, initial).profit);
    EXPECT_GT(hint->gain, 0);
    EXPECT_FALSE(hint->description.empty());
  }
}

TEST(FactoryHint, FollowingHintsOnlyEverRaisesProfit) {
  const auto [level, initial] = load(0);
  Design d = initial;
  Cents profit = simulate(level, d).profit;
  for (int i = 0; i < 6; ++i) {
    const auto hint = suggestChange(level, d);
    if (!hint) break;
    EXPECT_GT(hint->profit, profit);
    d = hint->design;
    profit = hint->profit;
  }
  EXPECT_GT(profit, simulate(level, initial).profit);
}

TEST(FactoryHint, ReturnsNothingWhenOutOfTime) {
  const auto [level, initial] = load(0);
  EXPECT_FALSE(suggestChange(level, initial, -1).has_value());
}

}  // namespace
}  // namespace pf::factory
