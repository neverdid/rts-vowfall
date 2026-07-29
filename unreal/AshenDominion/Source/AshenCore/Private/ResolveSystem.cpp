#include "ashen/core/ResolveSystem.hpp"

#include "ashen/core/Catalog.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace ashen::core {
namespace {

inline constexpr std::int32_t kTerrorRange = 250'000;
inline constexpr std::int32_t kWardRange = 230'000;

[[nodiscard]] std::uint64_t squared_distance(const Vec2 left,
                                             const Vec2 right) noexcept {
  const auto dx = static_cast<std::int64_t>(right.x) - left.x;
  const auto dy = static_cast<std::int64_t>(right.y) - left.y;
  return static_cast<std::uint64_t>(dx * dx + dy * dy);
}

[[nodiscard]] std::uint64_t integer_sqrt(
    const std::uint64_t value) noexcept {
  if (value == 0) {
    return 0;
  }
  auto estimate = value;
  auto next = (estimate + 1) / 2;
  while (next < estimate) {
    estimate = next;
    next = (estimate + value / estimate) / 2;
  }
  return estimate;
}

}  // namespace

ResolveState resolve_state_from_value(const std::int32_t resolve) noexcept {
  if (resolve >= 80) {
    return ResolveState::Steady;
  }
  if (resolve >= 60) {
    return ResolveState::Strained;
  }
  if (resolve >= 40) {
    return ResolveState::Wavering;
  }
  return ResolveState::Broken;
}

ResolveSystemOutput evaluate_resolve(
    const std::int32_t ruin_tide,
    const std::array<PlayerState, 2>& players,
    const std::span<const Entity> entities,
    const std::span<const ControlPoint> control_points,
    const SpatialGrid& spatial_grid) {
  ResolveSystemOutput output{};
  output.entities.reserve(entities.size());
  std::array<std::int32_t, 2> resolve_totals{};
  std::array<std::int32_t, 2> resolve_samples{};
  std::vector<SpatialQueryHit> nearby;

  for (const auto& entity : entities) {
    if (!entity.alive()) {
      continue;
    }
    if (entity.kind == EntityKind::Building) {
      output.entities.push_back(
          {entity.id, 100, ResolveState::Steady});
      continue;
    }

    auto enemy_terror = 0;
    auto friendly_ward = 0;
    spatial_grid.query_radius(entity.position,
                              std::max(kTerrorRange, kWardRange), nearby);
    for (const auto& hit : nearby) {
      if (hit.source_index >= entities.size()) {
        continue;
      }
      const auto& other = entities[hit.source_index];
      if (!other.alive() || other.id != hit.id) {
        continue;
      }
      const auto gap = static_cast<std::int64_t>(
          integer_sqrt(squared_distance(entity.position, other.position)));
      if (other.owner != entity.owner && other.terror > 0 &&
          gap <= kTerrorRange) {
        enemy_terror += static_cast<std::int32_t>(
            other.terror * (kTerrorRange - gap) / kTerrorRange);
      }
      if (other.owner == entity.owner && other.ward > 0 &&
          gap <= kWardRange) {
        friendly_ward += static_cast<std::int32_t>(
            other.ward * (kWardRange - gap) / kWardRange);
      }
    }

    auto relic_ward = 0;
    for (const auto& point : control_points) {
      if (point.owner == entity.owner) {
        const auto reach = static_cast<std::int64_t>(point.radius) + 130'000;
        if (squared_distance(entity.position, point.position) <=
            static_cast<std::uint64_t>(reach * reach)) {
          relic_ward = 8;
          break;
        }
      }
    }

    const auto ambient = ruin_tide * 18 / 100;
    const auto race_drift =
        faction_definition(players[player_index(entity.owner)].faction)
            .resolve_drift;
    const auto dread =
        ambient + enemy_terror - friendly_ward - relic_ward - race_drift;
    const auto resolve = std::clamp(100 - dread, 38, 100);
    output.entities.push_back(
        {entity.id, resolve, resolve_state_from_value(resolve)});
    resolve_totals[player_index(entity.owner)] += resolve;
    ++resolve_samples[player_index(entity.owner)];
  }

  for (const auto player : {PlayerId::One, PlayerId::Two}) {
    const auto index = player_index(player);
    output.player_resolve[index] =
        resolve_samples[index] == 0
            ? 100
            : resolve_totals[index] / resolve_samples[index];
  }
  return output;
}

}  // namespace ashen::core
