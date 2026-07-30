#pragma once

#include "ashen/core/PlayerObservation.hpp"
#include "ashen/core/Types.hpp"

#include <compare>
#include <cstdint>

namespace ashen::core {

enum class AIStrategicIntention : std::uint8_t {
  StabilizeEconomy,
  SecureRoute,
  PressureEnemy,
  DefendObjective,
  RecoverForce,
};

enum class AIOpeningPlan : std::uint8_t {
  RoadLedger,
  PreparedAbsolution,
  TreatyPosition,
};

enum class AIPreferredRoute : std::uint8_t {
  Unknown,
  North,
  Center,
  South,
};

enum class AIKnownOpponentBehavior : std::uint8_t {
  Unknown,
  Economic,
  ArmoredPressure,
  RangedPressure,
  Fortified,
};

enum class AIContingency : std::uint8_t {
  Scout,
  Reinforce,
  Hold,
  Retreat,
};

enum class AIStrategyEvidence : std::uint8_t {
  InitialDoctrine,
  VisibleEnemyForce,
  RememberedEnemyStructure,
  LowResolve,
  CommandLost,
  ObjectiveState,
};

enum class AIAbortCondition : std::uint32_t {
  None = 0,
  CommandLost = 1U << 0U,
  LowResolve = 1U << 1U,
  Outnumbered = 1U << 2U,
  RouteUnobserved = 1U << 3U,
};

struct AIStrategyState {
  AIStrategicIntention intention{
      AIStrategicIntention::StabilizeEconomy};
  AIOpeningPlan opening{AIOpeningPlan::RoadLedger};
  std::int32_t desired_workers{};
  std::int32_t desired_vanguards{};
  std::int32_t desired_skirmishers{};
  Tick timing_window_start{};
  Tick timing_window_end{};
  AIPreferredRoute preferred_route{AIPreferredRoute::Unknown};
  AIKnownOpponentBehavior known_opponent_behavior{
      AIKnownOpponentBehavior::Unknown};
  std::int32_t confidence_basis_points{};
  std::uint32_t abort_conditions{};
  AIContingency contingency{AIContingency::Scout};
  AIStrategyEvidence evidence{AIStrategyEvidence::InitialDoctrine};
  Tick last_updated_tick{};

  auto operator<=>(const AIStrategyState&) const = default;
};

[[nodiscard]] ASHENCORE_API AIStrategyState initial_ai_strategy_state(
    PlayerId player) noexcept;
[[nodiscard]] ASHENCORE_API AIStrategyState update_ai_strategy_state(
    const AIStrategyState& previous,
    const PlayerObservation& observation);
[[nodiscard]] ASHENCORE_API std::uint64_t ai_strategy_state_hash(
    const AIStrategyState& state) noexcept;

}  // namespace ashen::core
