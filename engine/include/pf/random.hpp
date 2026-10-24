#pragma once

#include <cstdint>
#include <limits>
#include <random>

namespace pf {

/// A seeded random source that gives the same sequence on every platform.
///
/// std::mt19937_64 is fully specified by the standard, but std::uniform_int_distribution and
/// the other distributions are not: libstdc++ and libc++ turn the same engine output into
/// different numbers. Everything seeded in the engine (instance generator, simulated
/// annealing) goes through this class so a seed means the same thing on Linux and macOS.
class Random {
 public:
  explicit Random(std::uint64_t seed) : engine_(seed) {}

  /// Uniform integer in [lo, hi] (inclusive; returns lo if hi < lo). Unbiased: rejection
  /// sampling on the engine's 64-bit output.
  template <typename Int>
  Int uniform(Int lo, Int hi) {
    if (hi <= lo) return lo;
    const auto span =
        static_cast<std::uint64_t>(static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo)) +
        1;
    if (span == 0) return static_cast<Int>(engine_());  // the full 64-bit range
    const std::uint64_t limit = std::numeric_limits<std::uint64_t>::max() -
                                (std::numeric_limits<std::uint64_t>::max() % span);
    std::uint64_t x = engine_();
    while (x >= limit) x = engine_();
    return static_cast<Int>(static_cast<std::int64_t>(lo) + static_cast<std::int64_t>(x % span));
  }

  /// Uniform double in [0, 1), from the top 53 bits.
  double unit() { return static_cast<double>(engine_() >> 11U) * 0x1.0p-53; }

  /// True with probability p.
  bool chance(double p) { return unit() < p; }

 private:
  std::mt19937_64 engine_;
};

}  // namespace pf
