#pragma once

#include "ashen/core/Types.hpp"

#include <cstdint>
#include <vector>

namespace ashen::core {

class SnapshotCodec;

class ASHENCORE_API VisibilityGrid final {
 public:
  void reset(Vec2 map_size, std::int32_t cell_size);
  void begin_update() noexcept;
  void reveal(Vec2 center, std::int32_t radius) noexcept;

  [[nodiscard]] VisibilityState state_at(Vec2 position) const noexcept;
  [[nodiscard]] bool overlaps_visible(Vec2 center, std::int32_t radius) const noexcept;
  [[nodiscard]] Vec2 map_size() const noexcept { return map_size_; }
  [[nodiscard]] std::int32_t cell_size() const noexcept { return cell_size_; }
  [[nodiscard]] std::int32_t columns() const noexcept { return columns_; }
  [[nodiscard]] std::int32_t rows() const noexcept { return rows_; }
  [[nodiscard]] const std::vector<VisibilityState>& cells() const noexcept { return cells_; }

 private:
  friend class SnapshotCodec;

  [[nodiscard]] std::size_t index(std::int32_t column, std::int32_t row) const noexcept;
  [[nodiscard]] bool cell_intersects_circle(std::int32_t column, std::int32_t row, Vec2 center,
                                            std::int32_t radius) const noexcept;

  Vec2 map_size_{};
  std::int32_t cell_size_{1};
  std::int32_t columns_{1};
  std::int32_t rows_{1};
  std::vector<VisibilityState> cells_{VisibilityState::Hidden};
};

}  // namespace ashen::core
