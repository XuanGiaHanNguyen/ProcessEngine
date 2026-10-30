#pragma once

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

#include <nlohmann/json.hpp>

namespace pf::test {

inline nlohmann::json loadJson(const std::filesystem::path& path) {
  std::ifstream in(path);
  std::stringstream buffer;
  buffer << in.rdbuf();
  return nlohmann::json::parse(buffer.str());
}

/// Every Factory-mode level file (format "processengine-factory/1") under instances/, sorted.
inline std::vector<std::filesystem::path> factoryLevelFiles() {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(PF_INSTANCES_DIR)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json" &&
        loadJson(entry.path()).value("format", "") == "processengine-factory/1") {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

/// Every scheduling instance (*.json under instances/ that is not a factory level), sorted.
inline std::vector<std::filesystem::path> instanceFiles() {
  std::vector<std::filesystem::path> files;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(PF_INSTANCES_DIR)) {
    if (entry.is_regular_file() && entry.path().extension() == ".json" &&
        !loadJson(entry.path()).contains("format")) {
      files.push_back(entry.path());
    }
  }
  std::sort(files.begin(), files.end());
  return files;
}

}  // namespace pf::test
