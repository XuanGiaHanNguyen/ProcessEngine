// pf_compare: runs a list of solvers over generated (or JSON) instances, writes one CSV row
// per (instance, solver), and prints a per-solver summary table.
//
//   pf_compare [--solvers a,b,c] [--count N] [--seed S] [--size small|medium|large]
//              [--time-limit MS] [--csv PATH] [--instances FILE_OR_DIR]...
#include "pf/generator.hpp"
#include "pf/json_io.hpp"
#include "pf/registry.hpp"
#include "pf/validator.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct Options {
  std::vector<std::string> solvers;
  int count = 100;
  std::uint64_t seed = 1;
  std::string size = "small";
  double time_limit_ms = 1000;
  std::string csv = "compare_results.csv";
  std::vector<std::string> instance_paths;
};

struct NamedInstance {
  std::string name;
  pf::Instance instance;
};

struct Row {
  std::string instance;
  std::string solver;
  bool valid = false;
  pf::Quantity earnings = 0;
  pf::Quantity profit = 0;
  bool proven_optimal = false;
  double runtime_ms = 0;
  double gap_pct = 0;
  std::string reference;  // "optimal" or "best_known"
  std::string error;
};

void usage() {
  std::cerr << "usage: pf_compare [--solvers a,b,c] [--count N] [--seed S]\n"
               "                  [--size small|medium|large] [--time-limit MS] [--csv PATH]\n"
               "                  [--instances FILE_OR_DIR]...\n"
               "solvers:";
  for (const auto& info : pf::SolverRegistry::builtin().list()) std::cerr << " " << info.name;
  std::cerr << "\n";
}

std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::stringstream in(s);
  for (std::string item; std::getline(in, item, sep);) {
    if (!item.empty()) out.push_back(item);
  }
  return out;
}

std::optional<Options> parseArgs(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    auto value = [&]() -> std::optional<std::string> {
      if (i + 1 >= argc) return std::nullopt;
      return std::string(argv[++i]);
    };
    if (arg == "--help" || arg == "-h") return std::nullopt;
    const std::optional<std::string> v = value();
    if (!v) {
      std::cerr << "missing value for " << arg << "\n";
      return std::nullopt;
    }
    try {
      if (arg == "--solvers") {
        o.solvers = split(*v, ',');
      } else if (arg == "--count") {
        o.count = std::stoi(*v);
      } else if (arg == "--seed") {
        o.seed = std::stoull(*v);
      } else if (arg == "--size") {
        o.size = *v;
      } else if (arg == "--time-limit") {
        o.time_limit_ms = std::stod(*v);
      } else if (arg == "--csv") {
        o.csv = *v;
      } else if (arg == "--instances") {
        o.instance_paths.push_back(*v);
      } else {
        std::cerr << "unknown option " << arg << "\n";
        return std::nullopt;
      }
    } catch (const std::exception&) {
      std::cerr << "bad value for " << arg << ": " << *v << "\n";
      return std::nullopt;
    }
  }
  if (o.solvers.empty()) {
    for (const auto& info : pf::SolverRegistry::builtin().list()) o.solvers.push_back(info.name);
  }
  return o;
}

bool loadInstances(const Options& o, std::vector<NamedInstance>& out) {
  namespace fs = std::filesystem;
  std::vector<fs::path> files;
  for (const std::string& p : o.instance_paths) {
    if (fs::is_directory(p)) {
      for (const auto& e : fs::recursive_directory_iterator(p)) {
        // Directory scans skip Factory-mode levels, which are not scheduling instances.
        if (e.is_regular_file() && e.path().extension() == ".json" &&
            e.path().parent_path().filename() != "factory") {
          files.push_back(e.path());
        }
      }
    } else {
      files.emplace_back(p);
    }
  }
  std::sort(files.begin(), files.end());
  for (const auto& f : files) {
    std::ifstream in(f);
    std::stringstream buffer;
    buffer << in.rdbuf();
    auto parsed = pf::json::parseInstance(std::string_view(buffer.str()));
    if (!parsed.value) {
      std::cerr << f << ": " << parsed.errors.front() << "\n";
      return false;
    }
    out.push_back({f.stem().string(), std::move(*parsed.value)});
  }
  if (!o.instance_paths.empty()) return true;

  for (int i = 0; i < o.count; ++i) {
    const std::uint64_t seed = o.seed + static_cast<std::uint64_t>(i);
    pf::GeneratorParams params;
    if (o.size == "small") {
      params = pf::GeneratorParams::small(seed);
    } else if (o.size == "medium") {
      params = pf::GeneratorParams::medium(seed);
    } else if (o.size == "large") {
      params = pf::GeneratorParams::large(seed);
    } else {
      std::cerr << "unknown size '" << o.size << "'\n";
      return false;
    }
    out.push_back({o.size + "-" + std::to_string(seed), pf::generate(params).instance});
  }
  return true;
}

std::string csvEscape(const std::string& s) {
  if (s.find_first_of(",\"\n") == std::string::npos) return s;
  std::string out = "\"";
  for (char c : s) {
    if (c == '"') out += '"';
    out += c;
  }
  return out + "\"";
}

void printSummary(const std::vector<std::string>& solvers, const std::vector<Row>& rows) {
  struct Agg {
    int n = 0, valid = 0, hits = 0, optimal = 0;
    double profit = 0, gap = 0, max_gap = 0, runtime = 0, max_runtime = 0;
  };
  std::map<std::string, Agg> agg;
  for (const Row& r : rows) {
    Agg& a = agg[r.solver];
    ++a.n;
    a.runtime += r.runtime_ms;
    a.max_runtime = std::max(a.max_runtime, r.runtime_ms);
    if (!r.valid) continue;
    ++a.valid;
    a.profit += static_cast<double>(r.profit);
    a.gap += r.gap_pct;
    a.max_gap = std::max(a.max_gap, r.gap_pct);
    if (r.gap_pct == 0) ++a.hits;
    if (r.proven_optimal) ++a.optimal;
  }
  std::printf("\n%-18s %6s %6s %12s %10s %10s %8s %8s %12s %12s\n", "solver", "runs", "valid",
              "mean profit", "mean gap%", "max gap%", "best", "proven", "mean ms", "max ms");
  std::printf("%s\n", std::string(110, '-').c_str());
  for (const std::string& s : solvers) {
    const Agg& a = agg[s];
    const double v = a.valid > 0 ? a.valid : 1;
    std::printf("%-18s %6d %6d %12.2f %10.2f %10.2f %8d %8d %12.3f %12.3f\n", s.c_str(), a.n,
                a.valid, a.profit / v, a.gap / v, a.max_gap, a.hits, a.optimal,
                a.n > 0 ? a.runtime / a.n : 0, a.max_runtime);
  }
  std::printf(
      "\ngap%% is relative to the proven optimum when some solver proved one, otherwise to the\n"
      "best earnings any solver found. 'best' counts runs with gap 0.\n");
}

}  // namespace

int main(int argc, char** argv) {
  const auto options = parseArgs(argc, argv);
  if (!options) {
    usage();
    return 2;
  }
  const auto& registry = pf::SolverRegistry::builtin();
  for (const std::string& s : options->solvers) {
    if (registry.find(s) == nullptr) {
      std::cerr << "unknown solver '" << s << "'\n";
      usage();
      return 2;
    }
  }
  std::vector<NamedInstance> instances;
  if (!loadInstances(*options, instances)) return 1;

  pf::SolveOptions solve_options;
  solve_options.time_limit_ms = options->time_limit_ms;
  solve_options.seed = options->seed;

  std::vector<Row> rows;
  for (const NamedInstance& ni : instances) {
    const pf::Quantity baseline = pf::validate(ni.instance, {}).earnings;
    std::vector<Row> group;
    std::optional<pf::Quantity> optimum;
    pf::Quantity best = baseline;
    for (const std::string& name : options->solvers) {
      const pf::Solution s = registry.create(name)->solve(ni.instance, solve_options, nullptr);
      Row r{ni.name,          name,         s.valid, s.earnings, s.earnings - baseline,
            s.proven_optimal, s.runtime_ms, 0,       "",         s.error.value_or("")};
      if (s.valid) {
        best = std::max(best, s.earnings);
        if (s.proven_optimal) optimum = s.earnings;
      }
      group.push_back(std::move(r));
    }
    const pf::Quantity reference = optimum.value_or(best);
    for (Row& r : group) {
      r.reference = optimum ? "optimal" : "best_known";
      if (r.valid && reference > 0) {
        r.gap_pct =
            100.0 * static_cast<double>(reference - r.earnings) / static_cast<double>(reference);
      }
      rows.push_back(std::move(r));
    }
  }

  std::ofstream csv(options->csv);
  if (!csv) {
    std::cerr << "cannot write " << options->csv << "\n";
    return 1;
  }
  csv << "instance,solver,valid,earnings,profit,proven_optimal,runtime_ms,gap_pct,reference,"
         "error\n";
  for (const Row& r : rows) {
    csv << csvEscape(r.instance) << ',' << r.solver << ',' << (r.valid ? 1 : 0) << ',' << r.earnings
        << ',' << r.profit << ',' << (r.proven_optimal ? 1 : 0) << ',' << r.runtime_ms << ','
        << r.gap_pct << ',' << r.reference << ',' << csvEscape(r.error) << '\n';
  }
  std::printf("%zu instances x %zu solvers -> %s\n", instances.size(), options->solvers.size(),
              options->csv.c_str());
  printSummary(options->solvers, rows);
  return 0;
}
