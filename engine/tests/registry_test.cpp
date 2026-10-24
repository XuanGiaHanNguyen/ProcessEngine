#include "pf/registry.hpp"

#include <stdexcept>

#include <gtest/gtest.h>

namespace pf {
namespace {

TEST(SolverRegistry, BuiltinListsBaselineSolvers) {
  const auto& registry = SolverRegistry::builtin();
  for (const char* name : {"greedy", "greedy_potential", "brute_force"}) {
    ASSERT_NE(registry.find(name), nullptr) << name;
    const auto solver = registry.create(name);
    ASSERT_NE(solver, nullptr) << name;
    EXPECT_EQ(solver->name(), name);
  }
  EXPECT_TRUE(registry.find("brute_force")->exact);
  EXPECT_FALSE(registry.find("greedy")->exact);
}

TEST(SolverRegistry, UnknownNameGivesNull) {
  EXPECT_EQ(SolverRegistry::builtin().create("nope"), nullptr);
  EXPECT_EQ(SolverRegistry::builtin().find("nope"), nullptr);
}

TEST(SolverRegistry, RejectsDuplicateNames) {
  SolverRegistry r;
  r.add({"x", "", false, {}}, [] { return std::unique_ptr<Solver>(); });
  EXPECT_THROW(r.add({"x", "", false, {}}, [] { return std::unique_ptr<Solver>(); }),
               std::invalid_argument);
  EXPECT_EQ(r.list().size(), 1U);
}

}  // namespace
}  // namespace pf
