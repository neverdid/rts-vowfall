#include "ashen/core/SpatialGrid.hpp"

#include <algorithm>
#include <cstdint>
#include <ranges>

namespace ashen::core {
namespace {

[[nodiscard]] std::uint64_t squared_distance(const Vec2 left,
                                             const Vec2 right) noexcept {
  const auto dx = static_cast<std::int64_t>(right.x) - left.x;
  const auto dy = static_cast<std::int64_t>(right.y) - left.y;
  return static_cast<std::uint64_t>(dx * dx + dy * dy);
}

}  // namespace

void SpatialGrid::reset(const Vec2 map_size, const std::int32_t cell_size) {
  map_size_ = {std::max(0, map_size.x), std::max(0, map_size.y)};
  cell_size_ = std::max(1, cell_size);
  columns_ = std::max(1, (map_size_.x + cell_size_ - 1) / cell_size_);
  rows_ = std::max(1, (map_size_.y + cell_size_ - 1) / cell_size_);
  cells_.clear();
  cells_.resize(static_cast<std::size_t>(columns_) *
                static_cast<std::size_t>(rows_));
  entry_count_ = 0;
}

void SpatialGrid::rebuild(const std::span<const Entity> entities) {
  for (auto& cell : cells_) {
    cell.clear();
  }
  entry_count_ = 0;
  for (std::size_t source_index = 0; source_index < entities.size();
       ++source_index) {
    const auto& entity = entities[source_index];
    if (!entity.alive()) {
      continue;
    }
    const auto x = std::clamp(entity.position.x, 0, map_size_.x);
    const auto y = std::clamp(entity.position.y, 0, map_size_.y);
    const auto column = std::min(columns_ - 1, x / cell_size_);
    const auto row = std::min(rows_ - 1, y / cell_size_);
    cells_[index(column, row)].push_back(
        {entity.id, source_index, entity.position});
    ++entry_count_;
  }
  for (auto& cell : cells_) {
    std::ranges::sort(cell, {}, [](const SpatialQueryHit& hit) {
      return hit.id.value;
    });
  }
}

void SpatialGrid::query_radius(const Vec2 center, const std::int32_t radius,
                               std::vector<SpatialQueryHit>& output) const {
  output.clear();
  const auto query_radius = std::max(0, radius);
  const auto raw_minimum_x =
      static_cast<std::int64_t>(center.x) - query_radius;
  const auto raw_maximum_x =
      static_cast<std::int64_t>(center.x) + query_radius;
  const auto raw_minimum_y =
      static_cast<std::int64_t>(center.y) - query_radius;
  const auto raw_maximum_y =
      static_cast<std::int64_t>(center.y) + query_radius;
  if (raw_maximum_x < 0 || raw_maximum_y < 0 ||
      raw_minimum_x > map_size_.x || raw_minimum_y > map_size_.y) {
    return;
  }

  const auto minimum_x = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(raw_minimum_x, 0, map_size_.x));
  const auto maximum_x = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(raw_maximum_x, 0, map_size_.x));
  const auto minimum_y = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(raw_minimum_y, 0, map_size_.y));
  const auto maximum_y = static_cast<std::int32_t>(
      std::clamp<std::int64_t>(raw_maximum_y, 0, map_size_.y));
  const auto minimum_column =
      std::min(columns_ - 1, minimum_x / cell_size_);
  const auto maximum_column =
      std::min(columns_ - 1, maximum_x / cell_size_);
  const auto minimum_row = std::min(rows_ - 1, minimum_y / cell_size_);
  const auto maximum_row = std::min(rows_ - 1, maximum_y / cell_size_);
  const auto squared_radius =
      static_cast<std::uint64_t>(query_radius) * query_radius;

  for (auto row = minimum_row; row <= maximum_row; ++row) {
    for (auto column = minimum_column; column <= maximum_column; ++column) {
      for (const auto& hit : cells_[index(column, row)]) {
        if (squared_distance(center, hit.position) <= squared_radius) {
          output.push_back(hit);
        }
      }
    }
  }
  std::ranges::sort(output, {}, [](const SpatialQueryHit& hit) {
    return hit.id.value;
  });
  const auto duplicate =
      std::ranges::unique(output, {}, [](const SpatialQueryHit& hit) {
        return hit.id.value;
      });
  output.erase(duplicate.begin(), duplicate.end());
}

std::vector<SpatialQueryHit> SpatialGrid::query_radius(
    const Vec2 center, const std::int32_t radius) const {
  std::vector<SpatialQueryHit> result;
  query_radius(center, radius, result);
  return result;
}

std::uint64_t SpatialGrid::approximate_memory_bytes() const noexcept {
  auto bytes = static_cast<std::uint64_t>(cells_.capacity()) *
               sizeof(std::vector<SpatialQueryHit>);
  for (const auto& cell : cells_) {
    bytes += static_cast<std::uint64_t>(cell.capacity()) *
             sizeof(SpatialQueryHit);
  }
  return bytes;
}

std::size_t SpatialGrid::index(const std::int32_t column,
                               const std::int32_t row) const noexcept {
  return static_cast<std::size_t>(row) *
             static_cast<std::size_t>(columns_) +
         static_cast<std::size_t>(column);
}

}  // namespace ashen::core
