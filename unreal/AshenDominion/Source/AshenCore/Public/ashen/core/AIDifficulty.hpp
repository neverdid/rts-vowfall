#pragma once

#include "ashen/core/Types.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace ashen::core {

struct AIDifficultyProfile {
  AIDifficulty difficulty{AIDifficulty::Competitive};
  Tick reaction_delay_ticks{4};
  Tick strategic_cadence_ticks{80};
  Tick tactical_cadence_ticks{120};
  Tick tactical_phase_ticks{30};
  Tick micro_cadence_ticks{12};
  Tick command_latency_ticks{};
  std::int32_t command_precision_radius{};
  std::int32_t planning_horizon_cells{6};
  std::int32_t mistake_rate_basis_points{};
  std::int32_t minimum_mistake_quality_basis_points{10'000};
  Tick mobile_memory_ticks{2'400};
  std::size_t utility_search_breadth{};

  auto operator<=>(const AIDifficultyProfile&) const = default;
};

[[nodiscard]] ASHENCORE_API const AIDifficultyProfile& ai_difficulty_profile(
    AIDifficulty difficulty) noexcept;
[[nodiscard]] ASHENCORE_API std::uint64_t ai_difficulty_hash(
    const AIDifficultyProfile& profile) noexcept;
[[nodiscard]] ASHENCORE_API std::string_view to_string(AIDifficulty difficulty) noexcept;

}  // namespace ashen::core
