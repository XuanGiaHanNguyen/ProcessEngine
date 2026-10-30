#include "pf/generator.hpp"

#include "pf/json_io.hpp"
#include "pf/validator.hpp"

#include <gtest/gtest.h>

namespace pf {
namespace {

TEST(Generator, ProducesWellFormedInstancesWithProfitableWitness) {
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    for (const GeneratorParams& params :
         {GeneratorParams::small(seed), GeneratorParams::medium(seed),
          GeneratorParams::large(seed)}) {
      const GeneratedInstance g = generate(params);
      const auto errors = checkInstance(g.instance);
      ASSERT_TRUE(errors.empty()) << "seed " << seed << ": " << errors.front();
      const ValidationReport witness = validate(g.instance, g.witness);
      ASSERT_TRUE(witness.valid) << "seed " << seed << ": " << witness.violation->message;
      EXPECT_GT(witness.earnings, validate(g.instance, {}).earnings) << "seed " << seed;
    }
  }
}

TEST(Generator, IsDeterministicPerSeed) {
  for (std::uint64_t seed = 0; seed < 50; ++seed) {
    EXPECT_EQ(generate(GeneratorParams::medium(seed)).instance,
              generate(GeneratorParams::medium(seed)).instance);
  }
  EXPECT_NE(generate(GeneratorParams::medium(1)).instance,
            generate(GeneratorParams::medium(2)).instance);
}

TEST(Generator, HonorsSizeControls) {
  GeneratorParams p;
  p.seed = 3;
  p.resources = 9;
  p.processes = 12;
  p.chain_depth = 4;
  p.workers = 3;
  p.horizon = 40;
  const Instance i = generate(p).instance;
  EXPECT_EQ(i.resources.size(), 9U);
  EXPECT_EQ(i.processes.size(), 12U);
  EXPECT_EQ(i.workers, 3);
  EXPECT_EQ(i.horizon, 40);
}

TEST(Generator, RaisesHorizonAndSizesToFitChain) {
  GeneratorParams p;
  p.chain_depth = 5;
  p.resources = 2;
  p.processes = 1;
  p.horizon = 1;
  p.max_duration = 3;
  const GeneratedInstance g = generate(p);
  EXPECT_EQ(g.instance.resources.size(), 6U);
  EXPECT_EQ(g.instance.processes.size(), 5U);
  EXPECT_TRUE(validate(g.instance, g.witness).valid);
}

TEST(Generator, ParamsFromJson) {
  const auto r = json::parseGeneratorParams({{"size", "small"}, {"seed", 9}, {"workers", 3}});
  ASSERT_TRUE(r.ok());
  EXPECT_EQ(r.value->seed, 9U);
  EXPECT_EQ(r.value->workers, 3);
  EXPECT_FALSE(json::parseGeneratorParams({{"size", "huge"}}).ok());
  EXPECT_FALSE(json::parseGeneratorParams({{"workers", 0}}).ok());
  EXPECT_FALSE(json::parseGeneratorParams({{"min_price", 5}, {"max_price", 2}}).ok());
}

}  // namespace
}  // namespace pf
