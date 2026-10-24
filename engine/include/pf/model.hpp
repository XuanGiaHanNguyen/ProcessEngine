#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace pf {

/// Discrete time, in ticks. The game runs over ticks 0..horizon.
using Tick = std::int32_t;
/// Resource quantities and money.
using Quantity = std::int64_t;
/// Index into Instance::resources.
using ResourceIndex = std::size_t;
/// Index into Instance::processes.
using ProcessIndex = std::size_t;

/// A tradeable good. sell_price == 0 means it cannot be sold.
struct Resource {
  std::string id;
  std::string name;
  Quantity sell_price = 0;

  bool operator==(const Resource&) const = default;
};

/// A quantity of one resource, used for process inputs and outputs.
struct ResourceAmount {
  ResourceIndex resource = 0;
  Quantity qty = 0;

  bool operator==(const ResourceAmount&) const = default;
};

/// A recipe: consumes inputs at start, produces outputs `duration` ticks later.
struct Process {
  std::string id;
  std::string name;
  std::vector<ResourceAmount> inputs;   ///< sorted by resource, no duplicates
  std::vector<ResourceAmount> outputs;  ///< sorted by resource, no duplicates
  Tick duration = 1;
  std::optional<std::int32_t> max_runs;  ///< cap on total runs; nullopt = unlimited

  bool operator==(const Process&) const = default;
};

/// A complete problem: resources, processes, start state, and limits.
struct Instance {
  std::string name;
  std::vector<Resource> resources;
  std::vector<Process> processes;
  std::vector<Quantity> start_inventory;  ///< indexed by ResourceIndex, size == resources.size()
  std::int32_t workers = 1;               ///< max runs in progress at once (W)
  Tick horizon = 0;                       ///< T
  Quantity start_money = 0;

  /// Looks up a resource by id. Linear scan; instances are small.
  [[nodiscard]] std::optional<ResourceIndex> findResource(std::string_view id) const;
  /// Looks up a process by id. Linear scan; instances are small.
  [[nodiscard]] std::optional<ProcessIndex> findProcess(std::string_view id) const;

  bool operator==(const Instance&) const = default;
};

/// One run of a process starting at `start_tick`.
struct ScheduledRun {
  ProcessIndex process = 0;
  Tick start_tick = 0;

  bool operator==(const ScheduledRun&) const = default;
};

/// A full plan: one entry per run, in any order.
using Schedule = std::vector<ScheduledRun>;

/// Checks the structural invariants of an instance (ids unique, references in range,
/// durations >= 1, quantities and prices non-negative, workers >= 1, horizon >= 0, ...).
/// Returns human-readable error messages; empty means the instance is well formed.
/// Solvers, the validator, and GameSession all assume a well-formed instance.
[[nodiscard]] std::vector<std::string> checkInstance(const Instance& instance);

/// Value of an inventory if sold at the instance's prices, plus start money.
[[nodiscard]] Quantity inventoryValue(const Instance& instance,
                                      const std::vector<Quantity>& inventory);

}  // namespace pf
