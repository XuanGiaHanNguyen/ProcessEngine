#pragma once

// Internal: a small dense primal simplex for  max c.x  s.t.  A x <= b,  x >= 0,  b >= 0.
// With b >= 0 the slack basis is feasible, so no phase 1 is needed.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace pf::detail {

struct LinearProgram {
  std::size_t rows = 0;
  std::size_t cols = 0;
  std::vector<double> a;  ///< rows x cols, row-major
  std::vector<double> b;  ///< rows, all >= 0
  std::vector<double> c;  ///< cols

  double& at(std::size_t i, std::size_t j) { return a[i * cols + j]; }
  [[nodiscard]] double at(std::size_t i, std::size_t j) const { return a[i * cols + j]; }
};

struct SimplexResult {
  enum class Status : std::uint8_t { Optimal, IterationLimit, Unbounded };
  Status status = Status::IterationLimit;
  double objective = 0;
  std::vector<double> x;  ///< primal values
  std::vector<double> y;  ///< dual values of the rows (>= 0)
  std::int64_t iterations = 0;
};

/// Dantzig's rule, switching to Bland's rule after a run of degenerate pivots (anti-cycling).
/// `should_stop` is polled every few pivots.
template <typename StopFn>
SimplexResult solveSimplex(const LinearProgram& lp, std::int64_t max_iterations,
                           StopFn&& should_stop) {
  constexpr double kEps = 1e-9;
  const std::size_t m = lp.rows;
  const std::size_t n = lp.cols;
  const std::size_t width = n + m + 1;  // structural, slack, rhs
  // Row i < m: constraint i. Row m: reduced costs r_j = c_j - z_j (optimal when all <= 0),
  // with -objective in the rhs column.
  std::vector<double> t((m + 1) * width, 0.0);
  auto cell = [&](std::size_t i, std::size_t j) -> double& { return t[i * width + j]; };
  for (std::size_t i = 0; i < m; ++i) {
    for (std::size_t j = 0; j < n; ++j) cell(i, j) = lp.at(i, j);
    cell(i, n + i) = 1;
    cell(i, width - 1) = lp.b[i];
  }
  for (std::size_t j = 0; j < n; ++j) cell(m, j) = lp.c[j];
  std::vector<std::size_t> basis(m);
  for (std::size_t i = 0; i < m; ++i) basis[i] = n + i;

  SimplexResult result;
  int degenerate_run = 0;
  while (true) {
    if (result.iterations >= max_iterations || (result.iterations % 16 == 0 && should_stop())) {
      result.status = SimplexResult::Status::IterationLimit;
      break;
    }
    const bool bland = degenerate_run > 50;
    std::size_t enter = width;
    double best = kEps;
    for (std::size_t j = 0; j + 1 < width; ++j) {
      if (cell(m, j) > best) {
        enter = j;
        if (bland) break;
        best = cell(m, j);
      }
    }
    if (enter == width) {
      result.status = SimplexResult::Status::Optimal;
      break;
    }
    std::size_t leave = m;
    double ratio = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < m; ++i) {
      const double coef = cell(i, enter);
      if (coef <= kEps) continue;
      const double r = cell(i, width - 1) / coef;
      if (r < ratio - kEps ||
          (bland && leave < m && std::abs(r - ratio) <= kEps && basis[i] < basis[leave])) {
        ratio = r;
        leave = i;
      }
    }
    if (leave == m) {
      result.status = SimplexResult::Status::Unbounded;
      break;
    }
    degenerate_run = ratio <= kEps ? degenerate_run + 1 : 0;

    const double pivot = cell(leave, enter);
    for (std::size_t j = 0; j < width; ++j) cell(leave, j) /= pivot;
    for (std::size_t i = 0; i <= m; ++i) {
      if (i == leave) continue;
      const double factor = cell(i, enter);
      if (std::abs(factor) <= 1e-15) continue;
      double* row = &t[i * width];
      const double* prow = &t[leave * width];
      for (std::size_t j = 0; j < width; ++j) row[j] -= factor * prow[j];
    }
    basis[leave] = enter;
    ++result.iterations;
  }

  result.x.assign(n, 0.0);
  for (std::size_t i = 0; i < m; ++i) {
    if (basis[i] < n) result.x[basis[i]] = cell(i, width - 1);
  }
  result.y.assign(m, 0.0);
  for (std::size_t i = 0; i < m; ++i) result.y[i] = std::max(0.0, -cell(m, n + i));
  result.objective = -cell(m, width - 1);
  return result;
}

}  // namespace pf::detail
