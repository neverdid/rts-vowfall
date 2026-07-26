#include "ashen/core/AIDifficulty.hpp"

#include <array>
#include <cstddef>

namespace ashen::core {
namespace {

inline constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

inline constexpr std::array<AIDifficultyProfile, 4> kProfiles{{
    {
        .difficulty = AIDifficulty::Story,
        .reaction_delay_ticks = 20,
        .strategic_cadence_ticks = 140,
        .tactical_cadence_ticks = 180,
        .tactical_phase_ticks = 45,
        .micro_cadence_ticks = 30,
        .command_latency_ticks = 4,
        .command_precision_radius = world(18, 0).x,
        .planning_horizon_cells = 3,
        .mistake_rate_basis_points = 1'800,
        .minimum_mistake_quality_basis_points = 5'500,
        .mobile_memory_ticks = 600,
        .utility_search_breadth = 2,
    },
    {
        .difficulty = AIDifficulty::Standard,
        .reaction_delay_ticks = 12,
        .strategic_cadence_ticks = 100,
        .tactical_cadence_ticks = 150,
        .tactical_phase_ticks = 30,
        .micro_cadence_ticks = 20,
        .command_latency_ticks = 2,
        .command_precision_radius = world(10, 0).x,
        .planning_horizon_cells = 4,
        .mistake_rate_basis_points = 900,
        .minimum_mistake_quality_basis_points = 7'000,
        .mobile_memory_ticks = 1'200,
        .utility_search_breadth = 4,
    },
    {
        .difficulty = AIDifficulty::Veteran,
        .reaction_delay_ticks = 6,
        .strategic_cadence_ticks = 80,
        .tactical_cadence_ticks = 120,
        .tactical_phase_ticks = 30,
        .micro_cadence_ticks = 14,
        .command_latency_ticks = 1,
        .command_precision_radius = world(4, 0).x,
        .planning_horizon_cells = 5,
        .mistake_rate_basis_points = 300,
        .minimum_mistake_quality_basis_points = 8'500,
        .mobile_memory_ticks = 1'800,
        .utility_search_breadth = 6,
    },
    {
        .difficulty = AIDifficulty::Competitive,
        .reaction_delay_ticks = 4,
        .strategic_cadence_ticks = 80,
        .tactical_cadence_ticks = 120,
        .tactical_phase_ticks = 30,
        .micro_cadence_ticks = 12,
        .command_latency_ticks = 0,
        .command_precision_radius = 0,
        .planning_horizon_cells = 6,
        .mistake_rate_basis_points = 0,
        .minimum_mistake_quality_basis_points = 10'000,
        .mobile_memory_ticks = 2'400,
        .utility_search_breadth = 0,
    },
}};

template <typename Value>
void hash_integral(std::uint64_t& hash, const Value value) noexcept {
  auto bits = static_cast<std::uint64_t>(value);
  for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
    hash ^= bits & 0xffU;
    hash *= kFnvPrime;
    bits >>= 8U;
  }
}

}  // namespace

const AIDifficultyProfile& ai_difficulty_profile(
    const AIDifficulty difficulty) noexcept {
  switch (difficulty) {
    case AIDifficulty::Story:
      return kProfiles[0];
    case AIDifficulty::Standard:
      return kProfiles[1];
    case AIDifficulty::Veteran:
      return kProfiles[2];
    case AIDifficulty::Competitive:
      return kProfiles[3];
  }
  return kProfiles[3];
}

std::uint64_t ai_difficulty_hash(const AIDifficultyProfile& profile) noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, static_cast<std::uint8_t>(profile.difficulty));
  hash_integral(hash, profile.reaction_delay_ticks);
  hash_integral(hash, profile.strategic_cadence_ticks);
  hash_integral(hash, profile.tactical_cadence_ticks);
  hash_integral(hash, profile.tactical_phase_ticks);
  hash_integral(hash, profile.micro_cadence_ticks);
  hash_integral(hash, profile.command_latency_ticks);
  hash_integral(hash, profile.command_precision_radius);
  hash_integral(hash, profile.planning_horizon_cells);
  hash_integral(hash, profile.mistake_rate_basis_points);
  hash_integral(hash, profile.minimum_mistake_quality_basis_points);
  hash_integral(hash, profile.mobile_memory_ticks);
  hash_integral(hash, profile.utility_search_breadth);
  return hash;
}

std::string_view to_string(const AIDifficulty difficulty) noexcept {
  switch (difficulty) {
    case AIDifficulty::Story:
      return "story";
    case AIDifficulty::Standard:
      return "standard";
    case AIDifficulty::Veteran:
      return "veteran";
    case AIDifficulty::Competitive:
      return "competitive";
  }
  return "unknown";
}

}  // namespace ashen::core
