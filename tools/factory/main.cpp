// pf_factory: runs the factory solvers on Factory-mode levels and prints, per level, each
// solver's profit and runtime next to the level's reference designs and the flow LP bound.
//
//   pf_factory [--solvers a,b,c] [--time-limit MS] [--seed S] [--json] LEVEL.json...
//
// --json prints one JSON object per line instead: {level, solver, profit, bound, design}.
#include "pf/factory/json.hpp"
#include "pf/factory/sim.hpp"
#include "pf/factory/solvers.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::string dollars(pf::factory::Cents c) {
  const pf::factory::Cents abs = c < 0 ? -c : c;
  const std::string cents = std::to_string(abs % 100);
  return (c < 0 ? "-$" : "$") + std::to_string(abs / 100) + "." + (cents.size() < 2 ? "0" : "") +
         cents;
}

std::vector<std::string> split(const std::string& s) {
  std::vector<std::string> out;
  std::stringstream in(s);
  for (std::string item; std::getline(in, item, ',');) {
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::vector<std::string> solvers;
  for (const auto& info : pf::factory::factorySolvers()) solvers.push_back(info.name);
  pf::SolveOptions options;
  options.time_limit_ms = 2000;
  std::vector<std::string> files;
  bool as_json = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const bool has_value = i + 1 < argc;
    if (arg == "--solvers" && has_value) {
      solvers = split(argv[++i]);
    } else if (arg == "--time-limit" && has_value) {
      options.time_limit_ms = std::stod(argv[++i]);
    } else if (arg == "--seed" && has_value) {
      options.seed = std::stoull(argv[++i]);
    } else if (arg == "--json") {
      as_json = true;
    } else if (!arg.empty() && arg[0] != '-') {
      files.push_back(arg);
    } else {
      std::cerr << "usage: pf_factory [--solvers a,b,c] [--time-limit MS] [--seed S] [--json] "
                   "LEVEL.json...\n";
      return 2;
    }
  }
  for (const std::string& s : solvers) {
    if (!pf::factory::makeFactorySolver(s)) {
      std::cerr << "unknown factory solver: " << s << "\n";
      return 2;
    }
  }
  if (files.empty()) {
    std::cerr << "no level files given\n";
    return 2;
  }

  for (const std::string& file : files) {
    std::ifstream in(file);
    std::stringstream buffer;
    buffer << in.rdbuf();
    const nlohmann::json doc = nlohmann::json::parse(buffer.str(), nullptr, false);
    auto parsed = pf::factory::json::parseLevel(doc);
    if (!parsed.value) {
      std::cerr << file << ": " << (parsed.errors.empty() ? "not JSON" : parsed.errors.front())
                << "\n";
      return 1;
    }
    const pf::factory::Level& level = *parsed.value;
    const auto bound = pf::factory::flowUpperBound(level, options);
    if (as_json) {
      for (const std::string& s : solvers) {
        const auto sol = pf::factory::makeFactorySolver(s)->solve(level, options);
        std::cout << nlohmann::json{{"level", level.id},
                                    {"solver", s},
                                    {"profit", sol.profit},
                                    {"bound", bound.bound},
                                    {"design", pf::factory::json::designToJson(sol.design, level)}}
                  << "\n";
      }
      continue;
    }
    std::printf("%s (bound %s)\n", level.name.c_str(), dollars(bound.bound).c_str());
    for (const auto& ref : doc.value("reference_designs", nlohmann::json::array())) {
      std::printf("  %-22s %10s\n", ref.value("name", "?").c_str(),
                  dollars(ref.value("profit", 0LL)).c_str());
    }
    for (const std::string& s : solvers) {
      const auto sol = pf::factory::makeFactorySolver(s)->solve(level, options);
      std::printf("  %-22s %10s  %8.0f ms  %zu machines\n", s.c_str(),
                  sol.valid ? dollars(sol.profit).c_str() : "invalid", sol.runtime_ms,
                  sol.design.machines.size());
    }
  }
  return 0;
}
