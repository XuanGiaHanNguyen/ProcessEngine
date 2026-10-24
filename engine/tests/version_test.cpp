#include "pf/version.hpp"

#include <gtest/gtest.h>

TEST(Version, IsNonEmpty) { EXPECT_FALSE(pf::version().empty()); }
