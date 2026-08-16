#pragma once

#include "ashen/core/Content.hpp"
#include "ashen/core/SpatialGrid.hpp"
#include "ashen/core/Types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ashen::core {

struct SupplyNodeState {
  EntityId entity{};
  EntityId source{};
  EntityId predecessor{};
  std::uint32_t hops{};
  std::int32_t demand{};
  bool connected{};

  auto operator<=>(const SupplyNodeState&) const = default;
};

struct SupplyTransition {
  EntityId entity{};
  bool connected{};

  auto operator<=>(const SupplyTransition&) const = default;
};

class ASHENCORE_API SupplySystem final {
 public:
  void reset() noexcept;
  [[nodiscard]] std::vector<SupplyTransition> evaluate(
      std::span<const Entity> entities, const SpatialGrid& spatial_grid,
      const ContentRegistry& content);
  void rebuild(std::span<const Entity> entities,
               const SpatialGrid& spatial_grid,
               const ContentRegistry& content);

  [[nodiscard]] bool can_connect_construction_site(
      EntityId site, PlayerId owner, FactionId faction, EntityType type,
      Vec2 position, std::span<const Entity> entities,
      const SpatialGrid& spatial_grid,
      const ContentRegistry& content) const;

  [[nodiscard]] bool derivation_matches(
      std::span<const Entity> entities, const SpatialGrid& spatial_grid,
      const ContentRegistry& content) const;
  [[nodiscard]] bool has_node(EntityId entity) const noexcept;
  [[nodiscard]] bool connected(EntityId entity) const noexcept;
  [[nodiscard]] const SupplyNodeState* find(EntityId entity) const noexcept;
  [[nodiscard]] EntityId retreat_anchor(
      PlayerId owner, Vec2 position, std::span<const Entity> entities,
      const ContentRegistry& content) const noexcept;
  [[nodiscard]] EntityId recovery_anchor(
      PlayerId owner, Vec2 position, std::span<const Entity> entities,
      const ContentRegistry& content) const noexcept;
  [[nodiscard]] std::span<const SupplyNodeState> states() const noexcept {
    return states_;
  }
  [[nodiscard]] std::uint64_t state_hash() const noexcept;

 private:
  [[nodiscard]] EntityId connected_transmitter_anchor(
      PlayerId owner, Vec2 position, std::span<const Entity> entities,
      const ContentRegistry& content) const noexcept;
  [[nodiscard]] static std::vector<SupplyNodeState> solve(
      std::span<const Entity> entities, const SpatialGrid& spatial_grid,
      const ContentRegistry& content);

  std::vector<SupplyNodeState> states_{};
};

}  // namespace ashen::core
