// Google Benchmark suites for the hot paths: the exact solvers on fixed instances, plus the
// validator and the rule core. Numbers are recorded in docs/solvers.md ("Performance").
//
//   cmake -B build-bench -DPF_BUILD_BENCHMARKS=ON -DPF_BUILD_TESTS=OFF
//   cmake --build build-bench -j && build-bench/engine/bench/pf_bench
#include "pf/bounds.hpp"
#include "pf/generator.hpp"
#include "pf/json_io.hpp"
#include "pf/registry.hpp"
#include "pf/validator.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>

#include <benchmark/benchmark.h>

namespace {

pf::Instance loadFactory() {
  std::ifstream in(std::string(PF_INSTANCES_DIR) + "/medium/factory.json");
  std::stringstream buffer;
  buffer << in.rdbuf();
  auto parsed = pf::json::parseInstance(std::string_view(buffer.str()));
  if (!parsed.value) throw std::runtime_error("cannot load factory.json");
  return *parsed.value;
}

pf::Instance instanceFor(std::int64_t id) {
  if (id == 0) return loadFactory();
  return pf::generate(pf::GeneratorParams::medium(static_cast<std::uint64_t>(id))).instance;
}

void solve(benchmark::State& state, const char* solver) {
  const pf::Instance inst = instanceFor(state.range(0));
  pf::SolveOptions options;
  options.time_limit_ms = 60'000;
  pf::Solution last;
  for (auto _ : state) {
    last = pf::SolverRegistry::builtin().create(solver)->solve(inst, options, nullptr);
    benchmark::DoNotOptimize(last.earnings);
  }
  if (!last.proven_optimal) state.SkipWithError("did not prove optimality");
  state.counters["earnings"] = static_cast<double>(last.earnings);
  for (const char* key : {"nodes", "states", "pruned"}) {
    if (last.stats.contains(key)) state.counters[key] = static_cast<double>(last.stats.at(key));
  }
}

void BM_DP(benchmark::State& state) { solve(state, "dp"); }
void BM_BranchAndBound(benchmark::State& state) { solve(state, "branch_and_bound"); }

// Instance ids: 0 = instances/medium/factory.json, n > 0 = GeneratorParams::medium(n).
BENCHMARK(BM_DP)->Arg(0)->Arg(1)->Arg(3)->Unit(benchmark::kMillisecond);
BENCHMARK(BM_BranchAndBound)->Arg(0)->Arg(5)->Arg(8)->Arg(10)->Unit(benchmark::kMillisecond);

void BM_Validate(benchmark::State& state) {
  const pf::Instance inst = pf::generate(pf::GeneratorParams::large(1)).instance;
  const pf::Solution s =
      pf::SolverRegistry::builtin().create("greedy_potential")->solve(inst, {}, nullptr);
  for (auto _ : state) benchmark::DoNotOptimize(pf::validate(inst, s.runs).earnings);
  state.counters["runs"] = static_cast<double>(s.runs.size());
}
BENCHMARK(BM_Validate)->Unit(benchmark::kMicrosecond);

void BM_DualBound(benchmark::State& state) {
  const pf::Instance inst = pf::generate(pf::GeneratorParams::medium(1)).instance;
  const pf::DualBound bound(inst);
  const pf::SimState root(inst);
  for (auto _ : state) benchmark::DoNotOptimize(bound.upperBound(root));
}
BENCHMARK(BM_DualBound);

}  // namespace

BENCHMARK_MAIN();
