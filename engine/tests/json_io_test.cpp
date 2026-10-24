#include "pf/json_io.hpp"

#include <gtest/gtest.h>

#include "test_util.hpp"

namespace pf {
namespace {

using Json = nlohmann::json;

const char* kSawmill = R"({
  "name": "Sawmill",
  "resources": [
    {"id": "wood", "name": "Wood", "sell_price": 1},
    {"id": "plank", "sell_price": 4}
  ],
  "processes": [
    {"id": "saw", "inputs": {"wood": 2}, "outputs": {"plank": 1}, "duration": 2, "max_runs": 5}
  ],
  "start_inventory": {"wood": 4},
  "workers": 1,
  "horizon": 4,
  "start_money": 3
})";

bool hasError(const std::vector<std::string>& errors, const std::string& needle) {
  for (const auto& e : errors) {
    if (e.find(needle) != std::string::npos) return true;
  }
  return false;
}

std::string joined(const std::vector<std::string>& errors) {
  std::string out;
  for (const auto& e : errors) out += e + "\n";
  return out;
}

TEST(JsonInstance, ParsesValidInstance) {
  const auto r = json::parseInstance(std::string_view(kSawmill));
  ASSERT_TRUE(r.ok()) << joined(r.errors);
  const Instance& i = *r.value;
  EXPECT_EQ(i.name, "Sawmill");
  ASSERT_EQ(i.resources.size(), 2U);
  EXPECT_EQ(i.resources[1].name, "plank");  // name defaults to id
  EXPECT_EQ(i.resources[1].sell_price, 4);
  ASSERT_EQ(i.processes.size(), 1U);
  EXPECT_EQ(i.processes[0].inputs, (std::vector<ResourceAmount>{{0, 2}}));
  EXPECT_EQ(i.processes[0].outputs, (std::vector<ResourceAmount>{{1, 1}}));
  EXPECT_EQ(i.processes[0].max_runs, 5);
  EXPECT_EQ(i.start_inventory, (std::vector<Quantity>{4, 0}));
  EXPECT_EQ(i.workers, 1);
  EXPECT_EQ(i.horizon, 4);
  EXPECT_EQ(i.start_money, 3);
}

TEST(JsonInstance, RoundTrips) {
  const auto r = json::parseInstance(std::string_view(kSawmill));
  ASSERT_TRUE(r.ok());
  const auto again = json::parseInstance(json::toJson(*r.value));
  ASSERT_TRUE(again.ok()) << joined(again.errors);
  EXPECT_EQ(*again.value, *r.value);
}

TEST(JsonInstance, SortsAmountsByResourceIndex) {
  Json j = Json::parse(kSawmill);
  // Object keys are iterated alphabetically ("plank" < "wood"), but indices are wood=0.
  j["processes"][0]["inputs"] = {{"wood", 1}, {"plank", 1}};
  const auto r = json::parseInstance(j);
  ASSERT_TRUE(r.ok()) << joined(r.errors);
  EXPECT_EQ(r.value->processes[0].inputs, (std::vector<ResourceAmount>{{0, 1}, {1, 1}}));
}

TEST(JsonInstance, ReportsSyntaxError) {
  const auto r = json::parseInstance(std::string_view("{\"resources\": ["));
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(hasError(r.errors, "invalid JSON syntax"));
}

TEST(JsonInstance, ReportsMissingFields) {
  const auto r = json::parseInstance(Json::object());
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(hasError(r.errors, "missing required field 'resources'"));
  EXPECT_TRUE(hasError(r.errors, "missing required field 'processes'"));
  EXPECT_TRUE(hasError(r.errors, "missing required field 'workers'"));
  EXPECT_TRUE(hasError(r.errors, "missing required field 'horizon'"));
}

TEST(JsonInstance, ReportsErrorsWithPaths) {
  Json j = Json::parse(kSawmill);
  j["horzion"] = 4;
  j["workers"] = "two";
  j["resources"][1]["sell_price"] = -1;
  j["processes"][0]["inputs"]["iron"] = 3;
  j["processes"][0]["duration"] = 0;
  j["processes"][0]["outputs"]["plank"] = 1.5;
  const auto r = json::parseInstance(j);
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(hasError(r.errors, "horzion: unknown field")) << joined(r.errors);
  EXPECT_TRUE(hasError(r.errors, "workers: expected an integer, got string"));
  EXPECT_TRUE(hasError(r.errors, "resources[1].sell_price: must be in [0,"));
  EXPECT_TRUE(hasError(r.errors, "processes[0].inputs.iron: unknown resource 'iron'"));
  EXPECT_TRUE(hasError(r.errors, "processes[0].duration: must be in [1,"));
  EXPECT_TRUE(hasError(r.errors, "processes[0].outputs.plank: expected an integer, got number"));
}

TEST(JsonInstance, AppliesStructuralChecks) {
  Json j = Json::parse(kSawmill);
  j["resources"].push_back({{"id", "wood"}});
  const auto r = json::parseInstance(j);
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(hasError(r.errors, "duplicate resource id 'wood'"));
}

TEST(JsonInstance, AllowsMetadataFields) {
  Json j = Json::parse(kSawmill);
  j["comment"] = "hand-computed optimum: 8";
  j["description"] = "tiny";
  j["reference_solutions"] = Json::array();
  EXPECT_TRUE(json::parseInstance(j).ok());
}

TEST(JsonInstance, RejectsOutOfRangeIntegers) {
  Json j = Json::parse(kSawmill);
  j["horizon"] = 99999999999LL;
  j["start_money"] = 18446744073709551615ULL;
  const auto r = json::parseInstance(j);
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(hasError(r.errors, "horizon: must be in"));
  EXPECT_TRUE(hasError(r.errors, "start_money: integer out of range"));
}

TEST(JsonSchedule, ParsesArrayAndObjectForms) {
  const Instance inst = *json::parseInstance(std::string_view(kSawmill)).value;
  const Json runs = Json::parse(R"([{"process_id": "saw", "start_tick": 0},
                                    {"process_id": "saw", "start_tick": 2}])");
  const auto a = json::parseSchedule(runs, inst);
  ASSERT_TRUE(a.ok()) << joined(a.errors);
  EXPECT_EQ(*a.value, (Schedule{{0, 0}, {0, 2}}));

  const auto b = json::parseSchedule(Json{{"runs", runs}}, inst);
  ASSERT_TRUE(b.ok());
  EXPECT_EQ(*b.value, *a.value);

  EXPECT_EQ(json::scheduleToJson(*a.value, inst), runs);
}

TEST(JsonSchedule, ReportsUnknownProcessAndBadFields) {
  const Instance inst = *json::parseInstance(std::string_view(kSawmill)).value;
  const Json runs = Json::parse(R"([{"process_id": "mine", "start_tick": 0},
                                    {"process_id": "saw"},
                                    {"process_id": "saw", "start_tick": "x", "extra": 1}])");
  const auto r = json::parseSchedule(runs, inst);
  ASSERT_FALSE(r.ok());
  EXPECT_TRUE(hasError(r.errors, "[0].process_id: unknown process 'mine'")) << joined(r.errors);
  EXPECT_TRUE(hasError(r.errors, "[1]: missing required field 'start_tick'"));
  EXPECT_TRUE(hasError(r.errors, "[2].start_tick: expected an integer"));
  EXPECT_TRUE(hasError(r.errors, "[2].extra: unknown field"));
}

TEST(JsonReport, SerializesViolation) {
  const Instance inst = *json::parseInstance(std::string_view(kSawmill)).value;
  const Json bad = json::toJson(validate(inst, {{0, 3}}), inst);
  EXPECT_FALSE(bad["valid"].get<bool>());
  EXPECT_EQ(bad["violation"]["kind"], "past_horizon");

  const Json good = json::toJson(validate(inst, {{0, 0}}), inst);
  EXPECT_TRUE(good["valid"].get<bool>());
  EXPECT_EQ(good["earnings"], 3 + 2 + 4);
  EXPECT_EQ(good["final_inventory"]["plank"], 1);
  EXPECT_TRUE(good["violation"].is_null());
}

}  // namespace
}  // namespace pf
