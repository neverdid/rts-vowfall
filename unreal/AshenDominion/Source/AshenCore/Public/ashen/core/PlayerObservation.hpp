#pragma once

#include "ashen/core/Types.hpp"
#include "ashen/core/VisibilityGrid.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace ashen::core {

inline constexpr Tick kMobileObservationMemoryTicks = 2'400;

class CommanderAI;
class SnapshotCodec;

struct ObservedEnemy {
  EntityId id{};
  PlayerId owner{PlayerId::One};
  FactionId faction{FactionId::Compact};
  EntityType type{EntityType::Worker};
  EntityKind kind{EntityKind::Unit};
  Vec2 position{};
  std::int32_t radius{};
  std::int32_t hit_points{};
  std::int32_t max_hit_points{};
  std::int32_t resolve{};
  bool under_construction{};
  bool currently_visible{};
  Tick last_observed_tick{};

  auto operator<=>(const ObservedEnemy&) const = default;
};

struct ObservedResource {
  ResourceId id{};
  Vec2 position{};
  std::int32_t radius{};
  std::int32_t last_observed_amount{};
  VisibilityState visibility{VisibilityState::Hidden};
  Tick last_observed_tick{};

  auto operator<=>(const ObservedResource&) const = default;
};

struct ObservedControlPoint {
  ControlPointId id{};
  Vec2 position{};
  std::int32_t radius{};
  VisibilityState visibility{VisibilityState::Hidden};
  bool has_observed_state{};
  std::optional<PlayerId> last_observed_owner{};
  std::int32_t last_observed_influence{};
  Tick last_observed_tick{};

  auto operator<=>(const ObservedControlPoint&) const = default;
};

struct CommandCapability {
  CommandType type{CommandType::Move};
  EntityId actor{};
  std::optional<EntityType> entity_type{};
  std::optional<ResearchId> research{};
  UnitIdentityId casualty{};

  auto operator<=>(const CommandCapability&) const = default;
};

// A value snapshot. Callers can inspect it but cannot use it to mutate the simulation.
class ASHENCORE_API PlayerObservation final {
 public:
  [[nodiscard]] Tick tick() const noexcept { return tick_; }
  [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }
  [[nodiscard]] Tick knowledge_tick() const noexcept { return knowledge_tick_; }
  [[nodiscard]] std::uint64_t match_seed() const noexcept { return match_seed_; }
  [[nodiscard]] PlayerId player() const noexcept { return player_; }
  [[nodiscard]] FactionId opponent_faction() const noexcept { return opponent_faction_; }
  [[nodiscard]] MatchStatus status() const noexcept { return status_; }
  [[nodiscard]] const PlayerState& self() const noexcept { return self_; }
  [[nodiscard]] std::int32_t ruin_tide() const noexcept { return ruin_tide_; }
  [[nodiscard]] Vec2 map_size() const noexcept { return map_size_; }
  [[nodiscard]] std::int32_t navigation_cell_size() const noexcept {
    return navigation_cell_size_;
  }
  [[nodiscard]] const std::vector<NavigationObstacle>& navigation_obstacles() const noexcept {
    return navigation_obstacles_;
  }
  [[nodiscard]] const VisibilityGrid& explored_map() const noexcept { return explored_map_; }
  [[nodiscard]] const std::vector<Entity>& owned_entities() const noexcept { return owned_entities_; }
  [[nodiscard]] const std::vector<ObservedEnemy>& known_enemies() const noexcept { return known_enemies_; }
  [[nodiscard]] const std::vector<ObservedResource>& known_resources() const noexcept { return known_resources_; }
  [[nodiscard]] const std::vector<ObservedControlPoint>& public_objectives() const noexcept {
    return public_objectives_;
  }
  [[nodiscard]] const std::vector<CommandCapability>& capabilities() const noexcept { return capabilities_; }
  [[nodiscard]] bool permits(CommandType type, EntityId actor = {},
                             std::optional<EntityType> entity_type = std::nullopt,
                             std::optional<ResearchId> research = std::nullopt,
                             UnitIdentityId casualty = {}) const noexcept;
  [[nodiscard]] std::uint64_t hash() const noexcept;

 private:
  friend class CommanderAI;
  friend class Simulation;
  friend class SnapshotCodec;

  [[nodiscard]] PlayerObservation with_delayed_opponent_knowledge(
      const PlayerObservation* delayed, Tick mobile_memory_ticks) const;

  PlayerObservation(Tick tick, std::uint64_t match_seed, PlayerId player, FactionId opponent_faction,
                    MatchStatus status, PlayerState self, std::int32_t ruin_tide, Vec2 map_size,
                    std::int32_t navigation_cell_size,
                    std::vector<NavigationObstacle> navigation_obstacles,
                    VisibilityGrid explored_map, std::vector<Entity> owned_entities,
                    std::vector<ObservedEnemy> known_enemies,
                    std::vector<ObservedResource> known_resources,
                    std::vector<ObservedControlPoint> public_objectives,
                    std::vector<CommandCapability> capabilities);

  Tick tick_{};
  std::uint64_t revision_{};
  Tick knowledge_tick_{};
  std::uint64_t match_seed_{1};
  PlayerId player_{PlayerId::One};
  FactionId opponent_faction_{FactionId::Compact};
  MatchStatus status_{MatchStatus::Playing};
  PlayerState self_{};
  std::int32_t ruin_tide_{};
  Vec2 map_size_{};
  std::int32_t navigation_cell_size_{1};
  std::vector<NavigationObstacle> navigation_obstacles_{};
  VisibilityGrid explored_map_{};
  std::vector<Entity> owned_entities_{};
  std::vector<ObservedEnemy> known_enemies_{};
  std::vector<ObservedResource> known_resources_{};
  std::vector<ObservedControlPoint> public_objectives_{};
  std::vector<CommandCapability> capabilities_{};
};

}  // namespace ashen::core
