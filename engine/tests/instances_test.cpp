// Loads every hand-made instance in instances/ and checks that the validator reproduces the
// hand-computed earnings of each reference solution stored alongside it.
#include "pf/json_io.hpp"
#include "pf/validator.hpp"

#include <gtest/gtest.h>

#include "instance_files.hpp"

namespace pf {
namespace {

namespace fs = std::filesystem;
using test::instanceFiles;
using test::loadJson;

TEST(HandMadeInstances, AtLeastFiveWithAKnownOptimum) {
  const auto files = instanceFiles();
  EXPECT_GE(files.size(), 5U);
  int with_optimum = 0;
  for (const auto& f : files) {
    for (const auto& ref : loadJson(f).value("reference_solutions", nlohmann::json::array())) {
      if (ref.value("optimal", false)) ++with_optimum;
    }
  }
  EXPECT_GE(with_optimum, 1);
}

class HandMadeInstance : public ::testing::TestWithParam<fs::path> {};

TEST_P(HandMadeInstance, ValidatorMatchesHandComputedEarnings) {
  const nlohmann::json doc = loadJson(GetParam());
  const auto parsed = json::parseInstance(doc);
  ASSERT_TRUE(parsed.ok()) << (parsed.errors.empty() ? "" : parsed.errors.front());
  ASSERT_TRUE(doc.contains("comment")) << "hand-made instances must document their answer";

  const auto& refs = doc.at("reference_solutions");
  ASSERT_FALSE(refs.empty());
  for (std::size_t i = 0; i < refs.size(); ++i) {
    const auto schedule = json::parseSchedule(refs[i].at("runs"), *parsed.value);
    ASSERT_TRUE(schedule.ok()) << "reference " << i;
    const ValidationReport report = validate(*parsed.value, *schedule.value);
    ASSERT_TRUE(report.valid) << "reference " << i << ": " << report.violation->message;
    EXPECT_EQ(report.earnings, refs[i].at("earnings").get<Quantity>()) << "reference " << i;
  }
}

INSTANTIATE_TEST_SUITE_P(All, HandMadeInstance, ::testing::ValuesIn(instanceFiles()),
                         [](const auto& param_info) { return param_info.param.stem().string(); });

}  // namespace
}  // namespace pf
