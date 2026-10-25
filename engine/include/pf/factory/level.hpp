#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

/// Factory mode (docs/factory-spec.md): a level describes suppliers, machine types, a market
/// and slots; a design builds machines in slots, sets supplier orders and lays belts.
namespace pf::factory {

/// Money in integer cents.
using Cents = std::int64_t;
/// Index into one of a level's vectors.
using Index = std::size_t;

/// Display category of a resource. It has no effect on the rules.
enum class ResourceKind : std::uint8_t { Raw, Part, Product };

/// Something that moves on belts.
struct Resource {
  std::string id;
  std::string name;
  ResourceKind kind = ResourceKind::Raw;

  bool operator==(const Resource&) const = default;
};

/// A quantity of one resource in a recipe.
struct Amount {
  Index resource = 0;
  int qty = 0;

  bool operator==(const Amount&) const = default;
};

/// Delivers `order` units of one raw resource every hour, at `unit_cost` each.
struct Supplier {
  std::string id;
  Index resource = 0;
  int max_rate = 0;
  Cents unit_cost = 0;

  bool operator==(const Supplier&) const = default;
};

/// One batch consumes `inputs` at its start and adds `outputs` `hours` hours later.
struct Recipe {
  std::string id;
  std::string name;
  std::vector<Amount> inputs;   ///< sorted by resource, no duplicates
  std::vector<Amount> outputs;  ///< sorted by resource, no duplicates
  int hours = 1;

  bool operator==(const Recipe&) const = default;
};

/// A machine that can be built in a slot for `cost`, running one of its recipes.
struct MachineType {
  std::string id;
  std::string name;
  Cents cost = 0;
  std::vector<Recipe> recipes;

  bool operator==(const MachineType&) const = default;
};

/// The market buys `resource`: the first `demand` units each hour at `price`, the rest at
/// `overflow_price`.
struct Listing {
  Index resource = 0;
  Cents price = 0;
  int demand = 0;
  Cents overflow_price = 0;

  bool operator==(const Listing&) const = default;
};

/// A place where a design may build one machine.
struct Slot {
  std::string id;

  bool operator==(const Slot&) const = default;
};

/// A complete level (layout hints and objectives live only in the JSON).
struct Level {
  std::string id;
  std::string name;
  std::string description;
  int hours = 24;
  int belt_capacity = 10;
  int buffer_batches = 2;
  std::optional<Cents> budget;
  std::vector<Resource> resources;
  std::vector<Supplier> suppliers;
  std::vector<MachineType> machine_types;
  std::vector<Listing> listings;
  std::vector<Slot> slots;

  [[nodiscard]] std::optional<Index> findResource(std::string_view key) const;
  [[nodiscard]] std::optional<Index> findSupplier(std::string_view key) const;
  [[nodiscard]] std::optional<Index> findMachineType(std::string_view key) const;
  [[nodiscard]] std::optional<Index> findSlot(std::string_view key) const;
  /// The market listing for a resource, if the market buys it.
  [[nodiscard]] const Listing* listingFor(Index resource) const;

  bool operator==(const Level&) const = default;
};

/// Kind of node a belt connects.
enum class NodeKind : std::uint8_t { Supplier, Slot, Market };

/// A belt endpoint: supplier index, slot index, or the market (index unused).
struct NodeRef {
  NodeKind kind = NodeKind::Market;
  Index index = 0;

  bool operator==(const NodeRef&) const = default;
};

/// A machine of `type` built in `slot`, running recipe `recipe` of that type.
struct PlacedMachine {
  Index slot = 0;
  Index type = 0;
  Index recipe = 0;

  bool operator==(const PlacedMachine&) const = default;
};

/// Carries `resource` from `from` to `to`; `weight` sets its share when a node splits.
struct Belt {
  NodeRef from;
  NodeRef to;
  Index resource = 0;
  int weight = 1;

  bool operator==(const Belt&) const = default;
};

/// The player's decisions. `orders` has one entry per supplier.
struct Design {
  std::vector<int> orders;
  std::vector<PlacedMachine> machines;
  std::vector<Belt> belts;

  bool operator==(const Design&) const = default;
};

/// A design error or warning with a stable machine code (see the spec's table).
struct DesignIssue {
  std::string code;
  std::string message;

  bool operator==(const DesignIssue&) const = default;
};

/// Limits from the spec.
namespace limits {
inline constexpr int kMaxHours = 168;
inline constexpr std::size_t kMaxResources = 32;
inline constexpr std::size_t kMaxSuppliers = 8;
inline constexpr std::size_t kMaxMachineTypes = 12;
inline constexpr std::size_t kMaxRecipes = 4;
inline constexpr std::size_t kMaxSlots = 24;
inline constexpr std::size_t kMaxBelts = 256;
inline constexpr int kMaxRecipeQty = 1000;
inline constexpr int kMaxRecipeHours = 24;
inline constexpr int kMaxBeltCapacity = 1000;
inline constexpr int kMaxBufferBatches = 10;
inline constexpr int kMaxRate = 1000;
inline constexpr Cents kMaxMoney = 100'000'000;
inline constexpr int kMaxDemand = 1000;
inline constexpr int kMaxWeight = 10;
}  // namespace limits

/// Checks a level's structure and limits. Empty means well formed; everything else in this
/// namespace assumes a well-formed level.
[[nodiscard]] std::vector<std::string> checkLevel(const Level& level);

/// Every rule a design breaks (codes as in the spec). Empty means the design is valid.
[[nodiscard]] std::vector<DesignIssue> checkDesign(const Level& level, const Design& design);

/// Suspicious but legal parts of a valid design: unfed inputs, outputs with nowhere to go,
/// paid-for supply with no belt.
[[nodiscard]] std::vector<DesignIssue> designWarnings(const Level& level, const Design& design);

/// Sum of the build costs of the design's machines.
[[nodiscard]] Cents buildCost(const Level& level, const Design& design);

/// The recipe a placed machine runs.
[[nodiscard]] const Recipe& recipeOf(const Level& level, const PlacedMachine& machine);

/// The id used for a node in JSON and messages ("market" for the market).
[[nodiscard]] std::string nodeId(const Level& level, const NodeRef& node);

/// A design with no machines, no belts and all orders 0.
[[nodiscard]] Design emptyDesign(const Level& level);

}  // namespace pf::factory
