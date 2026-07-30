#include "ashen/core/AIStrategyState.hpp"

#include "ashen/core/Content.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace ashen::core {
namespace {

inline constexpr std::uint64_t kFnvOffset =
    14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

template <typename Value>
void hash_integral(std::uint64_t& hash, const Value value) noexcept {
  auto bits = static_cast<std::uint64_t>(value);
  for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
    hash ^= bits & 0xffU;
    hash *= kFnvPrime;
    bits >>= 8U;
  }
}

[[nodiscard]] AIOpeningPlan opening_for(
    const FactionId faction) noexcept {
  switch (faction) {
    case FactionId::Compact:
      return AIOpeningPlan::RoadLedger;
    case FactionId::Ascendancy:
      return AIOpeningPlan::PreparedAbsolution;
    case FactionId::Concord:
      return AIOpeningPlan::TreatyPosition;
  }
  return AIOpeningPlan::RoadLedger;
}

[[nodiscard]] const AIStrategyContentDefinition* strategy_for(
    const FactionId faction) noexcept {
  const auto& strategies = builtin_content().ai_strategies;
  const auto found = std::ranges::find(
      strategies, faction, &AIStrategyContentDefinition::faction);
  return found == strategies.end() ? nullptr : &*found;
}

[[nodiscard]] AIPreferredRoute route_for_y(
    const Vec2 map_size, const std::int32_t y) noexcept {
  if (y < map_size.y / 3) {
    return AIPreferredRoute::North;
  }
  if (y > map_size.y * 2 / 3) {
    return AIPreferredRoute::South;
  }
  return AIPreferredRoute::Center;
}

[[nodiscard]] std::uint32_t abort_flag(
    const AIAbortCondition value) noexcept {
  return static_cast<std::uint32_t>(value);
}

}  // namespace

AIStrategyState initial_ai_strategy_state(const PlayerId player) noexcept {
  AIStrategyState result{};
  result.preferred_route =
      player == PlayerId::One ? AIPreferredRoute::North
                              : AIPreferredRoute::South;
  return result;
}

AIStrategyState update_ai_strategy_state(
    const AIStrategyState& previous,
    const PlayerObservation& observation) {
  auto result = previous;
  result.opening = opening_for(observation.self().faction);
  if (const auto* definition = strategy_for(observation.self().faction)) {
    result.desired_workers = definition->desired_workers;
    result.desired_vanguards = definition->desired_vanguards;
    result.desired_skirmishers = definition->desired_skirmishers;
  }
  result.timing_window_start = observation.tick();
  result.timing_window_end = observation.tick() + 600;
  result.last_updated_tick = observation.tick();
  result.abort_conditions = 0;

  const auto has_command = std::ranges::any_of(
      observation.owned_entities(), [](const Entity& entity) {
        return entity.alive() && entity.type == EntityType::Command;
      });
  const auto own_army = std::ranges::count_if(
      observation.owned_entities(), [](const Entity& entity) {
        return entity.alive() &&
               (entity.type == EntityType::Vanguard ||
                entity.type == EntityType::Skirmisher);
      });
  const auto visible_enemies = std::ranges::count_if(
      observation.known_enemies(), [](const ObservedEnemy& enemy) {
        return enemy.currently_visible && enemy.hit_points > 0 &&
               enemy.kind == EntityKind::Unit;
      });
  const auto visible_structures = std::ranges::count_if(
      observation.known_enemies(), [](const ObservedEnemy& enemy) {
        return enemy.currently_visible && enemy.hit_points > 0 &&
               enemy.kind == EntityKind::Building;
      });
  const auto known_vanguards = std::ranges::count(
      observation.known_enemies(), EntityType::Vanguard,
      &ObservedEnemy::type);
  const auto known_skirmishers = std::ranges::count(
      observation.known_enemies(), EntityType::Skirmisher,
      &ObservedEnemy::type);

  if (!has_command) {
    result.intention = AIStrategicIntention::RecoverForce;
    result.contingency = AIContingency::Retreat;
    result.evidence = AIStrategyEvidence::CommandLost;
    result.abort_conditions |= abort_flag(AIAbortCondition::CommandLost);
  } else if (observation.self().resolve < 50) {
    result.intention = AIStrategicIntention::RecoverForce;
    result.contingency = AIContingency::Reinforce;
    result.evidence = AIStrategyEvidence::LowResolve;
    result.abort_conditions |= abort_flag(AIAbortCondition::LowResolve);
  } else if (visible_enemies > 0) {
    result.intention = visible_enemies > own_army
                           ? AIStrategicIntention::DefendObjective
                           : AIStrategicIntention::PressureEnemy;
    result.contingency = visible_enemies > own_army
                             ? AIContingency::Retreat
                             : AIContingency::Hold;
    result.evidence = AIStrategyEvidence::VisibleEnemyForce;
    if (visible_enemies > own_army) {
      result.abort_conditions |=
          abort_flag(AIAbortCondition::Outnumbered);
    }
  } else if (observation.self().faction == FactionId::Compact) {
    result.intention = AIStrategicIntention::SecureRoute;
    result.contingency = AIContingency::Reinforce;
    result.evidence = AIStrategyEvidence::InitialDoctrine;
  } else if (observation.self().faction == FactionId::Ascendancy) {
    result.intention = AIStrategicIntention::PressureEnemy;
    result.contingency = AIContingency::Scout;
    result.evidence = AIStrategyEvidence::InitialDoctrine;
  } else {
    result.intention = AIStrategicIntention::DefendObjective;
    result.contingency = AIContingency::Hold;
    result.evidence = AIStrategyEvidence::ObjectiveState;
  }

  if (visible_structures > 0) {
    result.known_opponent_behavior =
        AIKnownOpponentBehavior::Fortified;
    result.evidence = AIStrategyEvidence::RememberedEnemyStructure;
  } else if (known_vanguards > known_skirmishers) {
    result.known_opponent_behavior =
        AIKnownOpponentBehavior::ArmoredPressure;
  } else if (known_skirmishers > known_vanguards) {
    result.known_opponent_behavior =
        AIKnownOpponentBehavior::RangedPressure;
  } else if (!observation.known_enemies().empty()) {
    result.known_opponent_behavior =
        AIKnownOpponentBehavior::Economic;
  } else {
    result.known_opponent_behavior =
        AIKnownOpponentBehavior::Unknown;
  }

  const auto visible_target = std::ranges::find_if(
      observation.known_enemies(), [](const ObservedEnemy& enemy) {
        return enemy.currently_visible && enemy.hit_points > 0;
      });
  if (visible_target != observation.known_enemies().end()) {
    result.preferred_route =
        route_for_y(observation.map_size(), visible_target->position.y);
  } else {
    const auto known_objective = std::ranges::find_if(
        observation.public_objectives(),
        [](const ObservedControlPoint& objective) {
          return objective.has_observed_state;
        });
    if (known_objective != observation.public_objectives().end()) {
      result.preferred_route =
          route_for_y(observation.map_size(), known_objective->position.y);
      result.evidence = AIStrategyEvidence::ObjectiveState;
    } else {
      result.abort_conditions |=
          abort_flag(AIAbortCondition::RouteUnobserved);
    }
  }

  result.confidence_basis_points = std::clamp(
      3'000 + static_cast<std::int32_t>(visible_enemies) * 850 +
          static_cast<std::int32_t>(visible_structures) * 500 +
          observation.self().resolve * 35,
      0, 10'000);
  return result;
}

std::uint64_t ai_strategy_state_hash(
    const AIStrategyState& state) noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, static_cast<std::uint8_t>(state.intention));
  hash_integral(hash, static_cast<std::uint8_t>(state.opening));
  hash_integral(hash, state.desired_workers);
  hash_integral(hash, state.desired_vanguards);
  hash_integral(hash, state.desired_skirmishers);
  hash_integral(hash, state.timing_window_start);
  hash_integral(hash, state.timing_window_end);
  hash_integral(hash, static_cast<std::uint8_t>(state.preferred_route));
  hash_integral(
      hash, static_cast<std::uint8_t>(state.known_opponent_behavior));
  hash_integral(hash, state.confidence_basis_points);
  hash_integral(hash, state.abort_conditions);
  hash_integral(hash, static_cast<std::uint8_t>(state.contingency));
  hash_integral(hash, static_cast<std::uint8_t>(state.evidence));
  hash_integral(hash, state.last_updated_tick);
  return hash;
}

}  // namespace ashen::core
