#pragma once

#include "ashen/core/SpatialGrid.hpp"
#include "ashen/core/Types.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace ashen::core {

struct ResolveUpdate {
  EntityId entity{};
  std::int32_t resolve{100};
  ResolveState state{ResolveState::Steady};

  auto operator<=>(const ResolveUpdate&) const = default;
};

struct ResolveSystemOutput {
  std::vector<ResolveUpdate> entities{};
  std::array<std::int32_t, 2> player_resolve{100, 100};

  auto operator<=>(const ResolveSystemOutput&) const = default;
};

[[nodiscard]] ASHENCORE_API ResolveState resolve_state_from_value(
    std::int32_t resolve) noexcept;
[[nodiscard]] ASHENCORE_API ResolveSystemOutput evaluate_resolve(
    std::int32_t ruin_tide, const std::array<PlayerState, 2>& players,
    std::span<const Entity> entities,
    std::span<const ControlPoint> control_points,
    const SpatialGrid& spatial_grid);

}  // namespace ashen::core
