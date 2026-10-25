#include "pf/factory/json.hpp"
#include "pf/factory/level.hpp"
#include "pf/factory/sim.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "instance_files.hpp"

namespace pf::factory {
namespace {

using Json = nlohmann::json;

/// A one-machine level: supplier "a" -> machine "m" (1 a -> 1 b, `hours`) -> market.
Json tinyLevel(int hours = 6) {
  Json j = Json::parse(R"({
    "format": "processforge-factory/1", "id": "tiny", "hours": 6, "belt_capacity": 10,
    "buffer_batches": 2,
    "resources": [{"id": "a", "kind": "raw"}, {"id": "b", "kind": "product"},
                  {"id": "c", "kind": "part"}],
    "suppliers": [{"id": "sa", "resource": "a", "max_rate": 20, "unit_cost": 10}],
    "machine_types": [
      {"id": "m", "cost": 100, "recipes": [
        {"id": "ab", "inputs": {"a": 1}, "outputs": {"b": 1}, "hours": 1},
        {"id": "abc", "inputs": {"a": 1}, "outputs": {"b": 1, "c": 1}, "hours": 1}]}],
    "market": {"listings": [
      {"resource": "a", "price": 100, "demand": 2, "overflow_price": 10},
      {"resource": "b", "price": 50, "demand": 10, "overflow_price": 50}]},
    "slots": [{"id": "s1"}, {"id": "s2"}]
  })");
  j["hours"] = hours;
  return j;
}

Level level(const Json& j) {
  auto parsed = json::parseLevel(j);
  EXPECT_TRUE(parsed.ok()) << (parsed.errors.empty() ? "" : parsed.errors.front());
  return *parsed.value;
}

Design design(const Level& l, const Json& j) {
  auto parsed = json::parseDesign(j, l);
  EXPECT_TRUE(parsed.design) << (parsed.errors.empty() ? "" : parsed.errors.front().message);
  return *parsed.design;
}

std::vector<std::string> codes(const Level& l, const Json& j) {
  std::vector<std::string> out;
  for (const DesignIssue& i : json::parseDesign(j, l).errors) out.push_back(i.code);
  return out;
}

bool has(const std::vector<std::string>& v, const std::string& s) {
  return std::find(v.begin(), v.end(), s) != v.end();
}

Json passThrough() {
  return Json::parse(R"({
  "orders": {"sa": 1},
  "machines": [{"slot": "s1", "type": "m", "recipe": "ab"}],
  "belts": [{"from": "sa", "to": "s1", "resource": "a"},
            {"from": "s1", "to": "market", "resource": "b"}]
})");
}

TEST(FactoryLevels, EveryLevelParsesAndReferenceProfitsMatch) {
  const auto files = test::factoryLevelFiles();
  ASSERT_EQ(files.size(), 5U);
  for (const auto& f : files) {
    SCOPED_TRACE(f.string());
    const Json doc = test::loadJson(f);
    auto parsed = json::parseLevel(doc);
    ASSERT_TRUE(parsed.ok()) << parsed.errors.front();
    int initial = 0;
    for (const Json& ref : doc["reference_designs"]) {
      SCOPED_TRACE(ref["name"].get<std::string>());
      if (ref.value("initial", false)) ++initial;
      const Design d = design(*parsed.value, ref["design"]);
      EXPECT_EQ(simulate(*parsed.value, d, false).profit, ref["profit"].get<Cents>());
    }
    EXPECT_EQ(initial, 1);
  }
}

TEST(FactoryLevels, FirstStitchInitialMatchesTheHandCalculation) {
  const Json doc = test::loadJson(test::factoryLevelFiles().front());
  const Level l = level(doc);
  const Design d = design(l, doc["reference_designs"][0]["design"]);
  const SimResult r = simulate(l, d);
  EXPECT_EQ(r.sales, 16200);
  EXPECT_EQ(r.purchases, 10080);
  EXPECT_EQ(r.build, 4500);
  EXPECT_EQ(r.profit, 1620);
  const Index animal = *l.findResource("stuffed-animal");
  EXPECT_EQ(r.sold[animal], 18);
  for (int h = 0; h < 24; ++h) {
    EXPECT_EQ(r.hours[static_cast<Index>(h)].sold[animal], (h >= 6 && h % 2 == 0) ? 2 : 0) << h;
  }
  EXPECT_EQ(r.hours.back().stock, (std::vector<std::int64_t>{80, 38}));
  EXPECT_EQ(r.hours.back().profit, r.profit);
}

TEST(FactorySim, PipelineTakesAnHourPerBeltAndPerBatch) {
  const Level l = level(tinyLevel());
  const SimResult r = simulate(l, design(l, passThrough()));
  const Index b = *l.findResource("b");
  // Delivered and dispatched in hour 0, starts in 1, completes in 2, sold in 3. Hours 3..5 sell.
  for (int h = 0; h < 6; ++h)
    EXPECT_EQ(r.hours[static_cast<Index>(h)].sold[b], h >= 3 ? 1 : 0) << h;
  EXPECT_EQ(r.sales, 3 * 50);
  EXPECT_EQ(r.purchases, 6 * 10);
  EXPECT_EQ(r.profit, 150 - 60 - 100);
  EXPECT_EQ(r.hours[1].machines[0].status, MachineStatus::Busy);
  EXPECT_EQ(r.hours[1].machines[0].progress, 1);
  EXPECT_EQ(r.hours[0].machines[0].status, MachineStatus::Starved);
  EXPECT_EQ(r.hours[0].machines[0].missing, std::vector<Index>{*l.findResource("a")});
}

TEST(FactorySim, MultiHourBatchReportsProgress) {
  Json j = tinyLevel();
  j["machine_types"][0]["recipes"][0]["hours"] = 3;
  const Level l = level(j);
  const SimResult r = simulate(l, design(l, passThrough()));
  EXPECT_EQ(r.hours[2].machines[0].progress, 2);
  EXPECT_EQ(r.hours[2].machines[0].total, 3);
  EXPECT_EQ(r.hours[4].machines[0].batches_done, 1);
}

TEST(FactorySim, AnOutputWithNowhereToGoBlocksAfterKBatches) {
  const Level l = level(tinyLevel(12));
  Json d = passThrough();
  d["machines"][0]["recipe"] = "abc";  // also makes c, which nothing takes
  const SimResult r = simulate(l, design(l, d));
  EXPECT_EQ(r.sold[*l.findResource("b")], 2);
  EXPECT_EQ(r.hours.back().machines[0].status, MachineStatus::Blocked);
  EXPECT_EQ(r.hours.back().machines[0].batches_done, 2);
  const auto warnings = designWarnings(l, design(l, d));
  ASSERT_EQ(warnings.size(), 1U);
  EXPECT_EQ(warnings[0].code, "output_blocked");
}

TEST(FactorySim, BeltCapacityAndDemandTiers) {
  Json j = tinyLevel();
  j["belt_capacity"] = 3;
  const Level l = level(j);
  const Design d = design(l, Json::parse(R"({"orders": {"sa": 5},
      "belts": [{"from": "sa", "to": "market", "resource": "a"}]})"));
  const SimResult r = simulate(l, d);
  EXPECT_EQ(r.hours[0].belt_moved[0], 3);
  EXPECT_EQ(r.hours[0].stock[0], 2);
  EXPECT_EQ(r.hours[5].stock[0], 12);
  // 3 units arrive each hour from hour 1: 2 at 100, 1 at the overflow price 10.
  EXPECT_EQ(r.hours[1].revenue, 210);
  EXPECT_EQ(r.sales, 5 * 210);
}

TEST(FactorySim, WeightedRoundRobinSplitsByWeight) {
  Json j = tinyLevel(2);
  j["buffer_batches"] = 10;
  const Level l = level(j);
  const Design d = design(l, Json::parse(R"({"orders": {"sa": 6},
      "machines": [{"slot": "s1", "type": "m", "recipe": "ab"},
                   {"slot": "s2", "type": "m", "recipe": "ab"}],
      "belts": [{"from": "sa", "to": "s1", "resource": "a", "weight": 2},
                {"from": "sa", "to": "s2", "resource": "a", "weight": 1}]})"));
  const SimResult r = simulate(l, d);
  EXPECT_EQ(r.hours[0].belt_moved, (std::vector<int>{4, 2}));
}

TEST(FactorySim, FullInputBufferStopsDispatch) {
  const Level l = level(tinyLevel(3));
  Json d = passThrough();
  d["orders"]["sa"] = 5;
  const SimResult r = simulate(l, design(l, d));
  EXPECT_EQ(r.hours[0].belt_moved[0], 2);  // input buffer holds 2 batches of 1 a
  EXPECT_EQ(r.hours[0].stock[0], 3);
}

TEST(FactorySim, SimulateRejectsInvalidDesigns) {
  const Level l = level(tinyLevel());
  Design d = emptyDesign(l);
  d.orders[0] = 99;
  EXPECT_THROW((void)simulate(l, d), std::invalid_argument);
}

TEST(FactoryDesign, ErrorCodes) {
  Json j = tinyLevel();
  j["budget"] = 150;
  const Level l = level(j);
  EXPECT_TRUE(has(codes(l, Json::parse(R"({"orders": {"nope": 1}})")), "unknown_supplier"));
  EXPECT_TRUE(has(codes(l, Json::parse(R"({"orders": {"sa": 21}})")), "order_out_of_range"));
  EXPECT_TRUE(
      has(codes(l, Json::parse(R"({"machines": [{"slot": "zz", "type": "m", "recipe": "ab"}]})")),
          "unknown_slot"));
  EXPECT_TRUE(
      has(codes(l, Json::parse(R"({"machines": [{"slot": "s1", "type": "x", "recipe": "ab"}]})")),
          "unknown_machine_type"));
  EXPECT_TRUE(
      has(codes(l, Json::parse(R"({"machines": [{"slot": "s1", "type": "m", "recipe": "x"}]})")),
          "unknown_recipe"));
  EXPECT_TRUE(has(codes(l, Json::parse(R"({"machines": [{"slot": "s1", "type": "m", "recipe": "ab"},
                                                         {"slot": "s1", "type": "m", "recipe": "ab"}]})")),
                  "slot_used_twice"));
  EXPECT_TRUE(has(codes(l, Json::parse(R"({"machines": [{"slot": "s1", "type": "m", "recipe": "ab"},
                                                         {"slot": "s2", "type": "m", "recipe": "ab"}]})")),
                  "over_budget"));
  const std::string m1 = R"("machines": [{"slot": "s1", "type": "m", "recipe": "ab"}])";
  auto belts = [&](const std::string& b) {
    return Json::parse("{" + m1 + R"(, "belts": [)" + b + "]}");
  };
  EXPECT_TRUE(
      has(codes(l, belts(R"({"from": "sa", "to": "s2", "resource": "a"})")), "unknown_node"));
  EXPECT_TRUE(has(codes(l, belts(R"({"from": "sa", "to": "s1", "resource": "b"})")),
                  "belt_wrong_resource"));
  EXPECT_TRUE(has(codes(l, belts(R"({"from": "s1", "to": "market", "resource": "c"})")),
                  "belt_wrong_resource"));
  EXPECT_TRUE(has(codes(l, belts(R"({"from": "s1", "to": "s1", "resource": "b"})")), "self_belt"));
  EXPECT_TRUE(
      has(codes(l, belts(R"({"from": "s1", "to": "s1", "resource": "b"})")), "belt_not_accepted"));
  EXPECT_TRUE(has(codes(l, belts(R"({"from": "sa", "to": "s1", "resource": "a", "weight": 11})")),
                  "weight_out_of_range"));
  EXPECT_TRUE(has(codes(l, belts(R"({"from": "sa", "to": "s1", "resource": "a"},
                                    {"from": "sa", "to": "s1", "resource": "a"})")),
                  "duplicate_belt"));
}

TEST(FactoryDesign, RoundTripsThroughJson) {
  const Level l = level(tinyLevel());
  const Design d = design(l, passThrough());
  EXPECT_EQ(design(l, json::designToJson(d, l)), d);
}

TEST(FactoryDesign, WarnsAboutPaidSupplyWithNoBelt) {
  const Level l = level(tinyLevel());
  const Design d = design(l, Json::parse(R"({"orders": {"sa": 3}})"));
  const auto w = designWarnings(l, d);
  ASSERT_EQ(w.size(), 1U);
  EXPECT_EQ(w[0].code, "unused_supply");
}

TEST(FactoryLevel, RejectsBadLevels) {
  Json j = tinyLevel();
  j["slots"][1]["id"] = "market";
  EXPECT_FALSE(json::parseLevel(j).ok());
  j = tinyLevel();
  j["format"] = "something-else";
  EXPECT_FALSE(json::parseLevel(j).ok());
  j = tinyLevel();
  j["machine_types"][0]["recipes"][0]["hours"] = 0;
  EXPECT_FALSE(json::parseLevel(j).ok());
  j = tinyLevel();
  j["surprise"] = 1;
  EXPECT_FALSE(json::parseLevel(j).ok());
}

TEST(FactorySim, ReportIsConsistent) {
  for (const auto& f : test::factoryLevelFiles()) {
    const Json doc = test::loadJson(f);
    const Level l = level(doc);
    for (const Json& ref : doc["reference_designs"]) {
      const Design d = design(l, ref["design"]);
      const SimResult r = simulate(l, d);
      Cents revenue = 0;
      Cents purchases = 0;
      for (const HourReport& h : r.hours) {
        revenue += h.revenue;
        purchases += h.purchases;
      }
      EXPECT_EQ(revenue, r.sales);
      EXPECT_EQ(purchases, r.purchases);
      EXPECT_EQ(r.hours.back().profit, r.profit);
      EXPECT_EQ(simulate(l, d, false).profit, r.profit);
    }
  }
}

}  // namespace
}  // namespace pf::factory
