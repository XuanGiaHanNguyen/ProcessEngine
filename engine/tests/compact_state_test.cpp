// The exact solvers use a compact copy of the rules (src/solvers/compact_state.hpp). This
// test drives it and SimState with the same random actions and requires identical results.
#include "../src/solvers/compact_state.hpp"

#include "pf/generator.hpp"

#include <array>
#include <random>

#include <gtest/gtest.h>

namespace pf {
namespace {

void expectSame(const detail::CompactRules& rules, const std::int64_t* c, const SimState& s) {
  ASSERT_EQ(detail::CompactRules::tick(c), s.tick());
  for (std::size_t r = 0; r < s.inventory().size(); ++r) ASSERT_EQ(c[1 + r], s.inventory()[r]);
  ASSERT_EQ(rules.activeCount(c), s.activeRuns().size());
  ASSERT_EQ(rules.nextEvent(c), s.nextEventTick());
  for (ProcessIndex p = 0; p < s.instance().processes.size(); ++p) {
    ASSERT_EQ(rules.canStart(c, p), s.canStart(p)) << "process " << p;
  }
  SimState done = s;
  done.advanceTo(s.instance().horizon);
  ASSERT_EQ(rules.finalValue(c), done.inventoryValue());
  // Same canonical encoding as building it from the SimState.
  std::vector<std::int64_t> rebuilt(rules.width());
  rules.fromSim(s, rebuilt.data());
  rules.setFrom(rebuilt.data(), rules.from(c));
  ASSERT_TRUE(std::equal(rebuilt.begin(), rebuilt.end(), c));
}

TEST(CompactState, MatchesSimStateOnRandomPlay) {
  std::mt19937_64 rng(11);
  for (std::uint64_t seed = 0; seed < 300; ++seed) {
    const Instance inst =
        generate(seed % 2 == 0 ? GeneratorParams::small(seed) : GeneratorParams::medium(seed))
            .instance;
    const detail::CompactRules rules(inst);
    std::vector<std::int64_t> c(rules.width());
    rules.initial(c.data());
    SimState s(inst);
    expectSame(rules, c.data(), s);
    while (true) {
      std::vector<ProcessIndex> legal;
      for (ProcessIndex p = 0; p < inst.processes.size(); ++p) {
        if (s.canStart(p)) legal.push_back(p);
      }
      const auto next = s.nextEventTick();
      if (legal.empty() && !next) break;
      if (!legal.empty() && (!next || std::uniform_int_distribution<int>(0, 2)(rng) != 0)) {
        const ProcessIndex p =
            legal[std::uniform_int_distribution<std::size_t>(0, legal.size() - 1)(rng)];
        s.start(p);
        rules.start(c.data(), p);
      } else {
        s.advanceTo(*next);
        rules.advanceTo(c.data(), *next);
      }
      expectSame(rules, c.data(), s);
    }
  }
}

TEST(FlatStateMap, FindsWhatWasInsertedAcrossGrowth) {
  detail::FlatStateMap<int> map(3);
  for (std::int64_t i = 0; i < 5000; ++i) {
    const std::array<std::int64_t, 3> key = {i, i * 7, -i};
    ASSERT_EQ(map.find(key.data(), static_cast<std::uint64_t>(i % 97)), nullptr);
    map.insert(key.data(), static_cast<std::uint64_t>(i % 97), static_cast<int>(i));
  }
  EXPECT_EQ(map.size(), 5000U);
  for (std::int64_t i = 0; i < 5000; ++i) {
    const std::array<std::int64_t, 3> key = {i, i * 7, -i};
    const int* v = map.find(key.data(), static_cast<std::uint64_t>(i % 97));
    ASSERT_NE(v, nullptr);
    EXPECT_EQ(*v, i);
  }
}

}  // namespace
}  // namespace pf
