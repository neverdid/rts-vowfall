#pragma once

#include "ashen/core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace ashen::core {

struct SpatialQueryHit {
  EntityId id{};
  std::size_t source_index{};
  Vec2 position{};

  auto operator<=>(const SpatialQueryHit&) const = default;
};

// Derived query acceleration only. Canonical simulation iteration remains the ordered
// entity vector. Rebuild and query must use the same immutable entity span.
class ASHENCORE_API SpatialGrid final {
 public:
  void reset(Vec2 map_size, std::int32_t cell_size);
  void rebuild(std::span<const Entity> entities);
  void query_radius(Vec2 center, std::int32_t radius,
                    std::vector<SpatialQueryHit>& output) const;

  [[nodiscard]] std::vector<SpatialQueryHit> query_radius(
      Vec2 center, std::int32_t radius) const;
  [[nodiscard]] Vec2 map_size() const noexcept { return map_size_; }
  [[nodiscard]] std::int32_t cell_size() const noexcept { return cell_size_; }
  [[nodiscard]] std::int32_t columns() const noexcept { return columns_; }
  [[nodiscard]] std::int32_t rows() const noexcept { return rows_; }
  [[nodiscard]] std::size_t entry_count() const noexcept { return entry_count_; }
  [[nodiscard]] std::uint64_t approximate_memory_bytes() const noexcept;

 private:
  [[nodiscard]] std::size_t index(std::int32_t column,
                                  std::int32_t row) const noexcept;

  Vec2 map_size_{};
  std::int32_t cell_size_{1};
  std::int32_t columns_{1};
  std::int32_t rows_{1};
  std::size_t entry_count_{};
  std::vector<std::vector<SpatialQueryHit>> cells_{1};
};

}  // namespace ashen::core
