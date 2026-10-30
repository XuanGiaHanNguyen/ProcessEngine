#include "pf/random.hpp"

#include "pf/generator.hpp"
#include "pf/json_io.hpp"

#include <gtest/gtest.h>

namespace pf {
namespace {

// The exact values pin the sequence: they must be identical with libstdc++ and libc++
// (checked on macOS/libc++ and GCC/libstdc++).
TEST(Random, SameSequenceOnEveryPlatform) {
  Random r(42);
  std::vector<int> got;
  for (int i = 0; i < 8; ++i) got.push_back(r.uniform(0, 99));
  EXPECT_EQ(got, (std::vector<int>{6, 24, 50, 62, 81, 28, 36, 44}));
}

TEST(Random, RespectsBounds) {
  Random r(7);
  for (int i = 0; i < 10'000; ++i) {
    const int v = r.uniform(-3, 4);
    ASSERT_GE(v, -3);
    ASSERT_LE(v, 4);
    const double u = r.unit();
    ASSERT_GE(u, 0.0);
    ASSERT_LT(u, 1.0);
  }
  EXPECT_EQ(r.uniform(5, 5), 5);
  EXPECT_EQ(r.uniform(5, 2), 5);
}

TEST(Random, GeneratedInstancesArePinned) {
  // FNV-1a of a generated instance's JSON. If this changes, generated instances changed on
  // this platform, and the numbers recorded in docs/solvers.md no longer apply.
  std::uint64_t h = 1469598103934665603ULL;
  for (const char c : json::toJson(generate(GeneratorParams::medium(1)).instance).dump()) {
    h ^= static_cast<unsigned char>(c);
    h *= 1099511628211ULL;
  }
  EXPECT_EQ(h, 5686450548723312551ULL);
}

}  // namespace
}  // namespace pf
