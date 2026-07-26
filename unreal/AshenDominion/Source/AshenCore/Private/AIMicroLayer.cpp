#include "AIPlanningInternal.hpp"

#include "ashen/core/Catalog.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <ranges>
#include <utility>
#include <vector>

namespace ashen::core::ai {
namespace {

[[nodiscard]] std::vector<EntityId> entity_ids(
    const std::vector<const Entity*>& entities) {
  std::vector<EntityId> result;
  result.reserve(entities.size());
  for (const auto* entity : entities) {
    result.push_back(entity->id);
  }
  return result;
}

[[nodiscard]] std::int32_t health_basis(const Entity& entity) noexcept {
  return entity.max_hit_points <= 0
             ? 0
             : std::clamp(entity.hit_points * 10'000 / entity.max_hit_points, 0, 10'000);
}

[[nodiscard]] bool already_attacking(const std::vector<const Entity*>& army,
                                     const EntityId target) noexcept {
  return !army.empty() && std::ranges::all_of(army, [target](const Entity* unit) {
    return unit->order.type == OrderType::Attack && unit->order.target_entity == target;
  });
}

void add_critical_retreat_candidate(const PlanningContext& context,
                                    std::vector<ScoredCommand>& candidates) {
  if (context.command_building == nullptr || context.search_commitment) {
    return;
  }
  std::vector<const Entity*> critical;
  auto lowest_health = 10'000;
  auto lowest_resolve = 100;
  for (const auto* unit : context.army) {
    const auto health = health_basis(*unit);
    if (health > context.doctrine.critical_retreat_health_basis_points &&
        unit->resolve > context.doctrine.critical_retreat_resolve) {
      continue;
    }
    const auto shelter_radius = static_cast<std::int64_t>(context.command_building->radius) + 110'000;
    const auto sheltered = squared_distance(unit->position, context.command_building->position) <=
                           static_cast<std::uint64_t>(shelter_radius * shelter_radius);
    const auto settled = unit->order.type == OrderType::Idle || unit->order.type == OrderType::Hold;
    if ((sheltered && settled) ||
        (unit->stance == UnitStance::Defensive && unit->order.type == OrderType::Move)) {
      continue;
    }
    const auto threatened = std::ranges::any_of(
        context.visible_enemies, [unit](const ObservedEnemy* enemy) {
          constexpr auto danger_radius = std::uint64_t{260'000};
          return squared_distance(unit->position, enemy->position) <= danger_radius * danger_radius;
        });
    const auto committed = unit->order.type == OrderType::Attack ||
                           unit->order.type == OrderType::AttackMove ||
                           unit->order.type == OrderType::Patrol;
    if (threatened || committed) {
      critical.push_back(unit);
      lowest_health = std::min(lowest_health, health);
      lowest_resolve = std::min(lowest_resolve, unit->resolve);
    }
  }
  if (critical.empty()) {
    return;
  }

  auto retreat = command_for(context.observation.player(), CommandType::Retreat);
  retreat.entities = entity_ids(critical);
  retreat.target = context.command_building->position;
  auto candidate = CandidateBuilder{AIAction::Retreat, std::move(retreat)};
  candidate.add(AIUtilityReason::Baseline, 2'000);
  if (lowest_health <=
      context.doctrine.critical_retreat_health_basis_points) {
    candidate.add(
        AIUtilityReason::CriticalHealth,
        apply_ai_weight(
            15'000 +
                (context.doctrine.critical_retreat_health_basis_points -
                 lowest_health),
            context.doctrine.preservation_weight_basis_points));
  }
  if (lowest_resolve <= context.doctrine.critical_retreat_resolve) {
    candidate.add(
        AIUtilityReason::LowResolve,
        apply_ai_weight(
            13'000 +
                (context.doctrine.critical_retreat_resolve -
                 lowest_resolve) *
                    100,
            context.doctrine.preservation_weight_basis_points));
    candidate.add(
        AIUtilityReason::ResolvePreservation,
        apply_ai_weight(
            (100 - lowest_resolve) * 35,
            context.doctrine.preservation_weight_basis_points));
  }
  candidate.position(context.command_building->position);
  candidates.push_back(std::move(candidate).finish());
}

void add_kite_candidates(const PlanningContext& context,
                         std::vector<ScoredCommand>& candidates) {
  for (const auto* skirmisher : context.skirmishers) {
    if (skirmisher->cooldown_ticks == 0) {
      continue;
    }
    const ObservedEnemy* nearest = nullptr;
    auto nearest_distance = std::numeric_limits<std::uint64_t>::max();
    for (const auto* enemy : context.visible_enemies) {
      if (enemy->type != EntityType::Vanguard) {
        continue;
      }
      const auto distance = squared_distance(skirmisher->position, enemy->position);
      const auto kite_radius = static_cast<std::int64_t>(skirmisher->attack_range) * 9 / 10 +
                               enemy->radius;
      if (distance <= static_cast<std::uint64_t>(kite_radius * kite_radius) &&
          (distance < nearest_distance ||
           (distance == nearest_distance && nearest != nullptr &&
            enemy->id.value < nearest->id.value))) {
        nearest = enemy;
        nearest_distance = distance;
      }
    }
    if (nearest == nullptr) {
      continue;
    }

    const auto target = position_away_from(context.observation, skirmisher->position,
                                           nearest->position, 90'000);
    if (skirmisher->order.type == OrderType::Move &&
        squared_distance(skirmisher->order.target, target) <=
            static_cast<std::uint64_t>(30'000) * 30'000U) {
      continue;
    }
    auto kite = command_for(context.observation.player(), CommandType::Move);
    kite.entities = {skirmisher->id};
    kite.target = target;
    const auto distance_world = static_cast<std::int32_t>(
        integer_sqrt(nearest_distance) / static_cast<std::uint64_t>(kWorldScale));
    candidates.push_back(std::move(CandidateBuilder{AIAction::Kite, std::move(kite)}
                                       .add(AIUtilityReason::Baseline, 3'000)
                                       .add(AIUtilityReason::WeaponCoolingDown,
                                            apply_ai_weight(
                                                7'000 +
                                                    static_cast<std::int32_t>(
                                                        skirmisher
                                                            ->cooldown_ticks) *
                                                        80,
                                                context.doctrine
                                                    .preservation_weight_basis_points))
                                       .add(AIUtilityReason::MeleePressure,
                                            apply_ai_weight(
                                                4'000 -
                                                    std::min(
                                                        3'000,
                                                        distance_world * 12),
                                                context.doctrine
                                                    .preservation_weight_basis_points))
                                       .target(nearest->id)
                                       .position(target))
                             .finish());
  }
}

void add_focus_fire_candidates(const PlanningContext& context,
                               std::vector<ScoredCommand>& candidates) {
  if (context.ready_army.empty()) {
    return;
  }
  for (const auto* enemy : context.visible_enemies) {
    if (already_attacking(context.ready_army, enemy->id)) {
      continue;
    }
    const auto definition = entity_definition(context.observation.opponent_faction(), enemy->type);
    const auto missing_health = enemy->max_hit_points <= 0
                                    ? 0
                                    : std::clamp((enemy->max_hit_points - enemy->hit_points) * 10'000 /
                                                     enemy->max_hit_points,
                                                 0, 10'000);
    auto bonus_attackers = std::int32_t{0};
    for (const auto* unit : context.ready_army) {
      if (unit->has_damage_bonus && unit->bonus_against == definition.armor) {
        ++bonus_attackers;
      }
    }

    auto focus = command_for(context.observation.player(), CommandType::Attack);
    focus.entities = entity_ids(context.ready_army);
    focus.target_entity = enemy->id;
    auto candidate = CandidateBuilder{AIAction::FocusFire, std::move(focus)};
    const auto resolve_vulnerability =
        std::max(0, 85 - enemy->resolve);
    const auto dread_opportunity =
        context.friendly_terror <= 0
            ? 0
            : resolve_vulnerability * 90 + definition.ward * 60;
    candidate.add(AIUtilityReason::Baseline, 3'200)
        .add(AIUtilityReason::VulnerableTarget, missing_health / 2)
        .add(AIUtilityReason::HighThreatTarget,
             definition.damage * 100 + definition.cost * 5 +
                 (is_army_unit(enemy->type) ? 2'800 : 0))
        .add(AIUtilityReason::CounterArmored, bonus_attackers * 500)
        .add(AIUtilityReason::DreadExploitation,
             apply_ai_weight(
                 dread_opportunity,
                 context.doctrine.dread_exploitation_weight_basis_points))
        .add(AIUtilityReason::ResolvePreservation,
             apply_ai_weight(
                 definition.terror * 120,
                 context.doctrine
                     .terror_resistance_weight_basis_points))
        .target(enemy->id)
        .position(enemy->position)
        .entity_type(enemy->type);
    candidates.push_back(std::move(candidate).finish());
  }
}

void add_screen_candidate(const PlanningContext& context,
                          std::vector<ScoredCommand>& candidates) {
  if (context.vanguards.empty() || context.skirmishers.empty()) {
    return;
  }
  std::vector<const ObservedEnemy*> melee;
  for (const auto* enemy : context.visible_enemies) {
    if (enemy->type == EntityType::Vanguard) {
      melee.push_back(enemy);
    }
  }
  if (melee.empty()) {
    return;
  }
  const auto ranged_center = centroid(context.skirmishers);
  const auto danger = enemy_centroid(melee);
  const auto distance = integer_sqrt(squared_distance(ranged_center, danger));
  if (distance > 75'000) {
    return;
  }

  auto dx = static_cast<std::int64_t>(danger.x) - ranged_center.x;
  auto dy = static_cast<std::int64_t>(danger.y) - ranged_center.y;
  if (distance == 0) {
    dx = context.observation.player() == PlayerId::One ? 1 : -1;
    dy = 0;
  }
  const auto divisor = static_cast<std::int64_t>(std::max<std::uint64_t>(1, distance));
  const auto target = clamped_position(
      context.observation,
      {ranged_center.x + static_cast<std::int32_t>(dx * 45'000 / divisor),
       ranged_center.y + static_cast<std::int32_t>(dy * 45'000 / divisor)});
  auto screen = command_for(context.observation.player(), CommandType::Move);
  screen.entities = entity_ids(context.vanguards);
  screen.target = target;
  candidates.push_back(std::move(CandidateBuilder{AIAction::ScreenRanged, std::move(screen)}
                                     .add(AIUtilityReason::Baseline, 3'500)
                                     .add(AIUtilityReason::RangedLineThreatened,
                                          apply_ai_weight(
                                              8'000 +
                                                  static_cast<std::int32_t>(
                                                      75'000 - distance) /
                                                      20,
                                              context.doctrine
                                                  .cohesion_weight_basis_points))
                                     .add(AIUtilityReason::FormationDoctrine,
                                          apply_ai_weight(
                                              1'200,
                                              context.doctrine
                                                  .cohesion_weight_basis_points))
                                     .position(target))
                           .finish());
}

void add_formation_recovery_candidate(const PlanningContext& context,
                                      std::vector<ScoredCommand>& candidates) {
  if (!context.visible_enemies.empty() || context.ready_army.size() < 3) {
    return;
  }
  const auto center = centroid(context.ready_army);
  const Entity* straggler = nullptr;
  auto greatest_distance = std::uint64_t{0};
  for (const auto* unit : context.ready_army) {
    if (unit->order.type != OrderType::Idle && unit->order.type != OrderType::Hold) {
      continue;
    }
    const auto distance = squared_distance(unit->position, center);
    if (distance > greatest_distance ||
        (distance == greatest_distance && straggler != nullptr && unit->id.value < straggler->id.value)) {
      straggler = unit;
      greatest_distance = distance;
    }
  }
  const auto recovery_distance = static_cast<std::uint64_t>(
      std::max(1, context.doctrine.formation_recovery_distance));
  if (straggler == nullptr || greatest_distance <= recovery_distance * recovery_distance) {
    return;
  }

  auto rejoin = command_for(context.observation.player(), CommandType::AttackMove);
  rejoin.entities = {straggler->id};
  rejoin.target = center;
  candidates.push_back(std::move(CandidateBuilder{AIAction::RejoinFormation, std::move(rejoin)}
                                     .add(AIUtilityReason::Baseline, 2'000)
                                     .add(AIUtilityReason::FormationSpread,
                                          apply_ai_weight(
                                              5'500,
                                              context.doctrine
                                                  .cohesion_weight_basis_points))
                                     .add(AIUtilityReason::FormationDoctrine,
                                          apply_ai_weight(
                                              1'000,
                                              context.doctrine
                                                  .cohesion_weight_basis_points))
                                     .target(straggler->id)
                                     .position(center))
                           .finish());
}

}  // namespace

std::optional<AIPlannedDecision> evaluate_micro_layer(const PlanningContext& context) {
  const auto decisive_structure_assault =
      context.search_commitment &&
      std::ranges::any_of(context.visible_enemies, [](const ObservedEnemy* enemy) {
        return enemy->type == EntityType::Command;
      });
  if (decisive_structure_assault) {
    return std::nullopt;
  }

  std::vector<ScoredCommand> candidates;
  add_critical_retreat_candidate(context, candidates);
  add_kite_candidates(context, candidates);
  add_screen_candidate(context, candidates);
  add_focus_fire_candidates(context, candidates);
  add_formation_recovery_candidate(context, candidates);
  return select_decision(AIDecisionLayer::Micro,
                         context.difficulty.micro_cadence_ticks, context,
                         std::move(candidates));
}

}  // namespace ashen::core::ai
