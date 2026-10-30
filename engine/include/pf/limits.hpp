#pragma once

#include "pf/model.hpp"

#include <cstdint>

namespace pf::limits {

// Bounds enforced by checkInstance. They are chosen so that no inventory count can
// overflow Quantity during a game: at most kMaxWorkers * kMaxHorizon runs, each adding at
// most kMaxRecipeQty of a resource, on top of kMaxStartQty. Only the final sale
// (qty * price summed over resources) can overflow, and that is checked explicitly.

inline constexpr Quantity kMaxRecipeQty = 1'000'000;
inline constexpr Quantity kMaxStartQty = 1'000'000'000'000;
inline constexpr Quantity kMaxPrice = 1'000'000;
inline constexpr Quantity kMaxStartMoney = 1'000'000'000'000'000;
inline constexpr Tick kMaxHorizon = 1'000'000;
inline constexpr std::int32_t kMaxWorkers = 10'000;

}  // namespace pf::limits
