#include "AIPlanningInternal.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace ashen::core::ai {
namespace {

struct DestinationChoice {
  Vec2 position{};
  AIInfluenceSample sample{};
  std::int64_t score{std::numeric_limits<std::int64_t>::min()};
};

[[nodiscard]] std::vector<EntityId> entity_ids(const std::vector<const Entity*>& entities) {
  std::vector<EntityId> result;
  result.reserve(entities.size());
  for (const auto* entity : entities) {
    result.push_back(entity->id);
  }
  return result;
}

[[nodiscard]] std::vector<const Entity*> tactical_field_force(const PlanningContext& context) {
  if (context.search_commitment) {
    return context.army;
  }
  if (!context.ready_army.empty()) {
    return context.ready_army;
  }
  return {};
}

[[nodiscard]] std::int32_t average_health_basis(
    const std::vector<const Entity*>& entities) noexcept {
  if (entities.empty()) {
    return 10'000;
  }
  std::int64_t total = 0;
  for (const auto* entity : entities) {
    total += entity->max_hit_points <= 0
                 ? 0
                 : std::clamp(entity->hit_points * 10'000 / entity->max_hit_points, 0, 10'000);
  }
  return static_cast<std::int32_t>(total / static_cast<std::int64_t>(entities.size()));
}

[[nodiscard]] std::int32_t average_resolve(const std::vector<const Entity*>& entities) noexcept {
  if (entities.empty()) {
    return 100;
  }
  std::int64_t total = 0;
  for (const auto* entity : entities) {
    total += entity->resolve;
  }
  return static_cast<std::int32_t>(total / static_cast<std::int64_t>(entities.size()));
}

[[nodiscard]] std::int32_t cell_distance(const AIInfluenceMap& map, const Vec2 left,
                                         const Vec2 right) noexcept {
  return static_cast<std::int32_t>(integer_sqrt(squared_distance(left, right)) /
                                   static_cast<std::uint64_t>(map.cell_size()));
}

[[nodiscard]] std::int32_t danger_value(
    const AIInfluenceCell& cell, const AIDoctrineProfile& doctrine) noexcept {
  const auto value = static_cast<std::int64_t>(cell.observed_enemy_power) * 6 +
                     static_cast<std::int64_t>(cell.static_danger) * 8 +
                     apply_ai_weight(
                         cell.terror_pressure * 2,
                         doctrine.terror_resistance_weight_basis_points);
  return static_cast<std::int32_t>(std::min<std::int64_t>(50'000, value));
}

[[nodiscard]] std::int32_t corridor_danger(const AIInfluenceMap& map, const Vec2 start,
                                           const Vec2 end,
                                           const AIDoctrineProfile& doctrine) noexcept {
  const auto dx = static_cast<std::int64_t>(end.x) - start.x;
  const auto dy = static_cast<std::int64_t>(end.y) - start.y;
  const auto distance = integer_sqrt(static_cast<std::uint64_t>(dx * dx + dy * dy));
  const auto samples =
      std::max<std::uint64_t>(1, distance / static_cast<std::uint64_t>(map.cell_size()));
  std::int64_t total = 0;
  auto peak = std::int32_t{};
  for (std::uint64_t index = 0; index <= samples; ++index) {
    const Vec2 point{
        start.x + static_cast<std::int32_t>(dx * static_cast<std::int64_t>(index) /
                                            static_cast<std::int64_t>(samples)),
        start.y + static_cast<std::int32_t>(dy * static_cast<std::int64_t>(index) /
                                            static_cast<std::int64_t>(samples)),
    };
    const auto danger = danger_value(map.cell_at(point), doctrine);
    total += danger;
    peak = std::max(peak, danger);
  }
  const auto average =
      static_cast<std::int32_t>(total / static_cast<std::int64_t>(samples + 1));
  return static_cast<std::int32_t>(
      std::min<std::int64_t>(50'000, std::max(average, peak * 3 / 4)));
}

[[nodiscard]] bool usable_cell(const PlanningContext& context,
                               const AIInfluenceCell& cell) noexcept {
  return cell.navigable &&
         (context.command_building == nullptr || cell.travel_cost < kAIUnreachableTravelCost);
}

[[nodiscard]] std::int32_t stable_tie_bonus(const PlanningContext& context,
                                            const AIInfluenceSample& sample) noexcept {
  auto value = context.strategy ^ (static_cast<std::uint64_t>(sample.row) << 32U) ^
               static_cast<std::uint32_t>(sample.column);
  value ^= value >> 33U;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33U;
  return static_cast<std::int32_t>(value & 31U);
}

[[nodiscard]] DestinationChoice safe_approach(const PlanningContext& context, const Vec2 origin,
                                              const Vec2 goal) noexcept {
  const auto& map = context.tactical_map();
  const auto goal_sample = map.sample_at(goal);
  DestinationChoice best{goal_sample.center, goal_sample};
  constexpr std::int32_t radius = 2;
  for (auto row = std::max(0, goal_sample.row - radius);
       row <= std::min(map.rows() - 1, goal_sample.row + radius); ++row) {
    for (auto column = std::max(0, goal_sample.column - radius);
         column <= std::min(map.columns() - 1, goal_sample.column + radius); ++column) {
      const auto position = map.cell_center(column, row);
      const auto sample = map.sample_at(position);
      if (!usable_cell(context, sample.cell)) {
        continue;
      }
      const auto goal_steps = cell_distance(map, position, goal);
      const auto travel_steps = cell_distance(map, origin, position);
      const auto danger = danger_value(sample.cell, context.doctrine);
      const auto route_danger =
          corridor_danger(map, origin, position, context.doctrine);
      const auto friendly_support = apply_ai_weight(
          sample.cell.friendly_power * 4,
          context.doctrine.cohesion_weight_basis_points);
      const auto ward_support = apply_ai_weight(
          sample.cell.friendly_ward * 3,
          context.doctrine.ward_affinity_weight_basis_points);
      const auto dread_opportunity =
          context.friendly_terror <= 0
              ? 0
              : apply_ai_weight(
                    sample.cell.resolve_vulnerability * 3,
                    context.doctrine.dread_exploitation_weight_basis_points);
      const auto score = -static_cast<std::int64_t>(std::abs(goal_steps - 1)) * 1'200 -
                         static_cast<std::int64_t>(travel_steps) * 45 -
                         static_cast<std::int64_t>(danger) * 5 -
                         static_cast<std::int64_t>(route_danger) * 2 +
                         friendly_support + ward_support + dread_opportunity -
                         sample.cell.uncertainty +
                         stable_tie_bonus(context, sample);
      if (score > best.score || (score == best.score && position < best.position)) {
        best = {position, sample, score};
      }
    }
  }
  return best;
}

[[nodiscard]] DestinationChoice safe_advance(const PlanningContext& context, const Vec2 origin,
                                             const Vec2 goal, const bool scouting) noexcept {
  const auto& map = context.tactical_map();
  const auto origin_to_goal = cell_distance(map, origin, goal);
  const auto goal_sample = map.sample_at(goal);
  if (origin_to_goal <= 2 && usable_cell(context, goal_sample.cell)) {
    return {goal, goal_sample, std::numeric_limits<std::int64_t>::max()};
  }
  DestinationChoice best{};
  const auto maximum_leg =
      std::max(1, context.difficulty.planning_horizon_cells);
  for (std::int32_t row = 0; row < map.rows(); ++row) {
    for (std::int32_t column = 0; column < map.columns(); ++column) {
      const auto position = map.cell_center(column, row);
      const auto sample = map.sample_at(position);
      if (!usable_cell(context, sample.cell)) {
        continue;
      }
      const auto leg = cell_distance(map, origin, position);
      const auto remaining = cell_distance(map, position, goal);
      const auto progress = origin_to_goal - remaining;
      if (leg == 0 || leg > maximum_leg || progress <= 0) {
        continue;
      }
      const auto danger = danger_value(sample.cell, context.doctrine);
      const auto route_danger =
          corridor_danger(map, origin, position, context.doctrine);
      const auto information =
          scouting
              ? apply_ai_weight(
                    sample.cell.uncertainty * 3,
                    context.doctrine.scouting_weight_basis_points)
              : -sample.cell.uncertainty;
      const auto friendly_support = apply_ai_weight(
          sample.cell.friendly_power * 3,
          context.doctrine.cohesion_weight_basis_points);
      const auto ward_support = apply_ai_weight(
          sample.cell.friendly_ward * 2,
          context.doctrine.ward_affinity_weight_basis_points);
      const auto dread_opportunity =
          context.friendly_terror <= 0
              ? 0
              : apply_ai_weight(
                    sample.cell.resolve_vulnerability * 2,
                    context.doctrine.dread_exploitation_weight_basis_points);
      const auto score =
          static_cast<std::int64_t>(progress) * 3'000 - static_cast<std::int64_t>(leg) * 90 -
          static_cast<std::int64_t>(danger) * 3 - static_cast<std::int64_t>(route_danger) * 2 +
          friendly_support + ward_support + dread_opportunity + information +
          stable_tie_bonus(context, sample);
      if (score > best.score || (score == best.score && position < best.position)) {
        best = {position, sample, score};
      }
    }
  }
  return best.score == std::numeric_limits<std::int64_t>::min()
             ? safe_approach(context, origin, goal)
             : best;
}

[[nodiscard]] DestinationChoice safest_shelter(const PlanningContext& context,
                                               const Vec2 origin) noexcept {
  const auto& map = context.tactical_map();
  const auto shelter =
      context.command_building == nullptr ? origin : context.command_building->position;
  const auto shelter_sample = map.sample_at(shelter);
  DestinationChoice best{shelter_sample.center, shelter_sample};
  constexpr std::int32_t radius = 4;
  for (auto row = std::max(0, shelter_sample.row - radius);
       row <= std::min(map.rows() - 1, shelter_sample.row + radius); ++row) {
    for (auto column = std::max(0, shelter_sample.column - radius);
         column <= std::min(map.columns() - 1, shelter_sample.column + radius); ++column) {
      const auto position = map.cell_center(column, row);
      const auto sample = map.sample_at(position);
      if (!usable_cell(context, sample.cell)) {
        continue;
      }
      const auto danger = danger_value(sample.cell, context.doctrine);
      const auto route_danger =
          corridor_danger(map, origin, position, context.doctrine);
      const auto shelter_distance = cell_distance(map, shelter, position);
      const auto support = apply_ai_weight(
          sample.cell.friendly_power * 10,
          context.doctrine.cohesion_weight_basis_points);
      const auto ward_support = apply_ai_weight(
          sample.cell.friendly_ward * 8,
          context.doctrine.ward_affinity_weight_basis_points);
      const auto score = static_cast<std::int64_t>(support) + ward_support -
                         static_cast<std::int64_t>(danger) * 8 -
                         static_cast<std::int64_t>(route_danger) * 4 -
                         static_cast<std::int64_t>(shelter_distance) * 250 -
                         static_cast<std::int64_t>(sample.cell.uncertainty) * 2 +
                         stable_tie_bonus(context, sample);
      if (score > best.score || (score == best.score && position < best.position)) {
        best = {position, sample, score};
      }
    }
  }
  return best;
}

[[nodiscard]] DestinationChoice reinforcement_staging(const PlanningContext& context,
                                                      const Vec2 origin,
                                                      const Vec2 front) noexcept {
  const auto& map = context.tactical_map();
  const auto front_sample = map.sample_at(front);
  DestinationChoice best{front_sample.center, front_sample};
  constexpr std::int32_t radius = 2;
  for (auto row = std::max(0, front_sample.row - radius);
       row <= std::min(map.rows() - 1, front_sample.row + radius); ++row) {
    for (auto column = std::max(0, front_sample.column - radius);
         column <= std::min(map.columns() - 1, front_sample.column + radius); ++column) {
      const auto position = map.cell_center(column, row);
      const auto sample = map.sample_at(position);
      if (!usable_cell(context, sample.cell)) {
        continue;
      }
      const auto danger = danger_value(sample.cell, context.doctrine);
      const auto route_danger =
          corridor_danger(map, origin, position, context.doctrine);
      const auto front_distance = cell_distance(map, front, position);
      const auto support = apply_ai_weight(
          sample.cell.friendly_power * 9,
          context.doctrine.cohesion_weight_basis_points);
      const auto ward_support = apply_ai_weight(
          sample.cell.friendly_ward * 4,
          context.doctrine.ward_affinity_weight_basis_points);
      const auto score = static_cast<std::int64_t>(support) + ward_support -
                         static_cast<std::int64_t>(danger) * 7 -
                         static_cast<std::int64_t>(route_danger) * 3 -
                         static_cast<std::int64_t>(front_distance) * 300 - sample.cell.uncertainty +
                         stable_tie_bonus(context, sample);
      if (score > best.score || (score == best.score && position < best.position)) {
        best = {position, sample, score};
      }
    }
  }
  return best;
}

[[nodiscard]] Vec2 scouting_goal(const PlanningContext& context, const Vec2 origin) noexcept {
  const auto& map = context.tactical_map();
  DestinationChoice best{};
  for (std::int32_t row = 0; row < map.rows(); ++row) {
    for (std::int32_t column = 0; column < map.columns(); ++column) {
      const auto position = map.cell_center(column, row);
      const auto sample = map.sample_at(position);
      if (!usable_cell(context, sample.cell)) {
        continue;
      }
      const auto distance = cell_distance(map, origin, position);
      if (distance < 2) {
        continue;
      }
      const auto enemy_depth =
          context.observation.player() == PlayerId::One ? column : map.columns() - 1 - column;
      const auto information = apply_ai_weight(
          sample.cell.uncertainty * 8,
          context.doctrine.scouting_weight_basis_points);
      const auto score = static_cast<std::int64_t>(information) +
                         static_cast<std::int64_t>(sample.cell.objective_value) * 2 -
                         static_cast<std::int64_t>(
                             danger_value(sample.cell, context.doctrine)) *
                             8 -
                         static_cast<std::int64_t>(distance) * 70 -
                         static_cast<std::int64_t>(sample.cell.travel_cost) * 2 +
                         static_cast<std::int64_t>(enemy_depth) * 220 +
                         stable_tie_bonus(context, sample);
      if (score > best.score || (score == best.score && position < best.position)) {
        best = {position, sample, score};
      }
    }
  }
  return best.score == std::numeric_limits<std::int64_t>::min() ? origin : best.position;
}

void add_influence_utility(CandidateBuilder& candidate,
                           const PlanningContext& context,
                           const AIInfluenceSample& sample,
                           const bool scouting = false,
                           const bool flanking = false) {
  const auto danger = danger_value(sample.cell, context.doctrine);
  candidate.add(AIUtilityReason::DangerAvoidance, std::clamp(1'800 - danger / 2, -3'000, 1'800));
  candidate.add(
      AIUtilityReason::FriendlySupport,
      std::min(2'000,
               apply_ai_weight(
                   sample.cell.friendly_power * 4,
                   context.doctrine.cohesion_weight_basis_points)));
  candidate.add(
      AIUtilityReason::WardSupport,
      std::min(2'000,
               apply_ai_weight(
                   sample.cell.friendly_ward * 5,
                   context.doctrine.ward_affinity_weight_basis_points)));
  candidate.add(AIUtilityReason::TravelEfficiency,
                sample.cell.travel_cost >= kAIUnreachableTravelCost
                    ? -3'000
                    : std::max(0, 1'200 - sample.cell.travel_cost * 3));
  candidate.add(
      AIUtilityReason::TerrorAvoidance,
      std::clamp(
          apply_ai_weight(
              700 - sample.cell.terror_pressure * 3,
              context.doctrine.terror_resistance_weight_basis_points),
          -2'500, 1'000));
  if (context.friendly_terror > 0) {
    candidate.add(
        AIUtilityReason::DreadExploitation,
        std::min(
            3'000,
            apply_ai_weight(
                sample.cell.resolve_vulnerability * 4,
                context.doctrine.dread_exploitation_weight_basis_points)));
  }
  if (scouting) {
    candidate.add(
        AIUtilityReason::UncertaintyReduction,
        std::min(
            2'500,
            apply_ai_weight(
                sample.cell.uncertainty * 2,
                context.doctrine.scouting_weight_basis_points)));
    candidate.add(
        AIUtilityReason::ScoutingDoctrine,
        apply_ai_weight(
            300, context.doctrine.scouting_weight_basis_points));
  }
  if (flanking) {
    candidate.add(AIUtilityReason::FlankSafety,
                  std::clamp(1'200 - sample.cell.static_danger * 2, -2'000, 1'200));
  }
}

[[nodiscard]] const ObservedEnemy* latest_known_command(const PlanningContext& context) noexcept {
  const ObservedEnemy* result = nullptr;
  for (const auto& enemy : context.observation.known_enemies()) {
    if (enemy.type != EntityType::Command || enemy.hit_points <= 0) {
      continue;
    }
    if (result == nullptr || (enemy.currently_visible && !result->currently_visible) ||
        (enemy.currently_visible == result->currently_visible &&
         enemy.last_observed_tick > result->last_observed_tick) ||
        (enemy.currently_visible == result->currently_visible &&
         enemy.last_observed_tick == result->last_observed_tick &&
         enemy.id.value < result->id.value)) {
      result = &enemy;
    }
  }
  return result;
}

[[nodiscard]] bool knows_enemy_fortification(
    const PlanningContext& context) noexcept {
  return std::ranges::any_of(
      context.observation.known_enemies(), [](const ObservedEnemy& enemy) {
        return enemy.type == EntityType::Turret && enemy.hit_points > 0;
      });
}

[[nodiscard]] std::size_t minimum_base_assault_force(
    const PlanningContext& context) noexcept {
  if (context.observation.tick() < 2'400 ||
      knows_enemy_fortification(context)) {
    return static_cast<std::size_t>(
        std::max(1, context.doctrine.minimum_assault_units));
  }
  return std::size_t{1};
}

void add_retreat_candidate(const PlanningContext& context, std::vector<ScoredCommand>& candidates) {
  if (context.army.empty() || context.command_building == nullptr ||
      context.visible_enemy_power <= 0 || context.attrition_commitment) {
    return;
  }
  std::vector<const Entity*> exposed;
  auto exposed_power = std::int32_t{};
  for (const auto* unit : context.army) {
    const auto shelter_radius =
        static_cast<std::int64_t>(context.command_building->radius) + 110'000;
    const auto sheltered = squared_distance(unit->position, context.command_building->position) <=
                           static_cast<std::uint64_t>(shelter_radius * shelter_radius);
    const auto settled = unit->order.type == OrderType::Idle || unit->order.type == OrderType::Hold;
    const auto retreating =
        unit->stance == UnitStance::Defensive && unit->order.type == OrderType::Move;
    if ((sheltered && settled) || retreating) {
      continue;
    }
    const auto threatened =
        std::ranges::any_of(context.visible_enemies, [unit](const ObservedEnemy* enemy) {
          constexpr auto danger_radius = std::uint64_t{360'000};
          return squared_distance(unit->position, enemy->position) <= danger_radius * danger_radius;
        });
    const auto committed = unit->order.type == OrderType::Attack ||
                           unit->order.type == OrderType::AttackMove ||
                           unit->order.type == OrderType::Patrol;
    if (!threatened && !committed) {
      continue;
    }
    exposed.push_back(unit);
    exposed_power += entity_power(*unit, context.observation.self().faction);
  }
  if (exposed.empty()) {
    return;
  }
  const auto health = average_health_basis(exposed);
  const auto resolve = average_resolve(exposed);
  const auto outnumbered =
      static_cast<std::int64_t>(exposed_power) * 10'000 <
      static_cast<std::int64_t>(context.visible_enemy_power) *
          context.doctrine.engagement_power_ratio_basis_points;
  const auto damaged =
      health < context.doctrine.tactical_retreat_health_basis_points;
  const auto wavering = resolve < context.doctrine.tactical_retreat_resolve;
  if (!outnumbered && !damaged && !wavering) {
    return;
  }

  auto retreat = command_for(context.observation.player(), CommandType::Retreat);
  retreat.entities = entity_ids(exposed);
  const auto destination = safest_shelter(context, centroid(exposed));
  retreat.target = destination.position;
  auto candidate = CandidateBuilder{AIAction::Retreat, std::move(retreat)};
  candidate.add(AIUtilityReason::Baseline, 1'000);
  if (outnumbered) {
    const auto deficit = std::max(0, context.visible_enemy_power - exposed_power);
    candidate.add(
        AIUtilityReason::Outnumbered,
        apply_ai_weight(
            13'000 + std::min(4'000, deficit * 10),
            context.doctrine.preservation_weight_basis_points));
    candidate.add(
        AIUtilityReason::AcceptableLosses,
        apply_ai_weight(
            1'500,
            context.doctrine.preservation_weight_basis_points));
  }
  if (damaged) {
    candidate.add(
        AIUtilityReason::CriticalHealth,
        apply_ai_weight(
            8'000 +
                (context.doctrine.tactical_retreat_health_basis_points -
                 health),
            context.doctrine.preservation_weight_basis_points));
  }
  if (wavering) {
    candidate.add(
        AIUtilityReason::LowResolve,
        apply_ai_weight(
            7'000 +
                (context.doctrine.tactical_retreat_resolve - resolve) * 80,
            context.doctrine.preservation_weight_basis_points));
    candidate.add(
        AIUtilityReason::ResolvePreservation,
        apply_ai_weight(
            (100 - resolve) * 30,
            context.doctrine.preservation_weight_basis_points));
  }
  candidate.position(destination.position).influence(context.tactical_map(), destination.position);
  add_influence_utility(candidate, context, destination.sample);
  candidates.push_back(std::move(candidate).finish());
}

void add_power_candidate(const PlanningContext& context, std::vector<ScoredCommand>& candidates) {
  if (context.ready_army.size() <
          static_cast<std::size_t>(
              std::max(1, context.doctrine.minimum_assault_units)) ||
      context.visible_enemies.empty() ||
      !context.observation.permits(CommandType::ActivatePower)) {
    return;
  }
  const auto resolve_deficit =
      std::max(0, 85 - context.average_army_resolve);
  auto damaged_units = std::int32_t{};
  auto damaged_buildings = std::int32_t{};
  for (const auto& entity : context.observation.owned_entities()) {
    if (!entity.alive() || entity.under_construction ||
        entity.max_hit_points <= 0) {
      continue;
    }
    const auto damaged =
        entity.hit_points * 10'000 / entity.max_hit_points < 8'000;
    damaged_units += damaged && entity.kind == EntityKind::Unit ? 1 : 0;
    damaged_buildings += damaged && entity.kind == EntityKind::Building ? 1 : 0;
  }
  const auto enemy_resolve_deficit =
      std::max(0, 85 - context.visible_enemy_average_resolve);
  const auto terror_advantage =
      std::max(0, context.friendly_terror - context.visible_enemy_ward);
  const auto local_terror =
      context.tactical_map().cell_at(centroid(context.ready_army)).terror_pressure;

  auto opportunity = std::int32_t{};
  switch (context.doctrine.faction) {
    case FactionId::Compact:
      if (resolve_deficit <= 5 && damaged_units == 0) {
        return;
      }
      opportunity =
          4'000 + resolve_deficit * 180 + damaged_units * 700;
      break;
    case FactionId::Ascendancy:
      opportunity = 5'000 + enemy_resolve_deficit * 120 +
                    terror_advantage * 40;
      break;
    case FactionId::Concord:
      if (resolve_deficit <= 5 && damaged_buildings == 0 &&
          local_terror == 0) {
        return;
      }
      opportunity = 4'200 + resolve_deficit * 160 +
                    damaged_buildings * 900 + local_terror * 4;
      break;
  }
  auto power = command_for(context.observation.player(), CommandType::ActivatePower);
  const auto position = centroid(context.ready_army);
  const auto sample = context.tactical_map().sample_at(position);
  auto candidate = CandidateBuilder{AIAction::ActivatePower, std::move(power)};
  candidate
      .add(AIUtilityReason::Baseline,
           apply_ai_weight(
               2'500, context.doctrine.power_weight_basis_points))
      .add(AIUtilityReason::AbilityOpportunity,
           apply_ai_weight(
               opportunity, context.doctrine.power_weight_basis_points));
  if (context.doctrine.faction == FactionId::Ascendancy) {
    candidate.add(
        AIUtilityReason::DreadExploitation,
        apply_ai_weight(
            enemy_resolve_deficit * 90 + terror_advantage * 30,
            context.doctrine.dread_exploitation_weight_basis_points));
  } else {
    candidate.add(
        AIUtilityReason::ResolvePreservation,
        apply_ai_weight(
            resolve_deficit * 100,
            context.doctrine.preservation_weight_basis_points));
  }
  if (context.visible_enemy_power > context.friendly_power) {
    candidate.add(
        AIUtilityReason::Outnumbered,
        apply_ai_weight(
            2'000, context.doctrine.preservation_weight_basis_points));
  }
  candidate.position(position).influence(context.tactical_map(), position);
  add_influence_utility(candidate, context, sample);
  candidates.push_back(std::move(candidate).finish());
}

void add_engagement_candidates(const PlanningContext& context,
                               std::vector<ScoredCommand>& candidates) {
  const auto assault_force = tactical_field_force(context);
  if (assault_force.empty() || context.visible_enemies.empty()) {
    return;
  }
  const auto* known_command = latest_known_command(context);
  const auto minimum_assault_force = minimum_base_assault_force(context);
  if (known_command != nullptr && known_command->currently_visible &&
      assault_force.size() >= minimum_assault_force) {
    const auto army_center = centroid(assault_force);
    const auto approach = safe_approach(context, army_center, known_command->position);
    const auto in_commit_range =
        integer_sqrt(squared_distance(army_center, known_command->position)) <=
        static_cast<std::uint64_t>(kAIInfluenceCellSize * 2);
    auto assault = command_for(context.observation.player(),
                               in_commit_range ? CommandType::Attack : CommandType::AttackMove);
    assault.entities = entity_ids(assault_force);
    if (in_commit_range) {
      assault.target_entity = known_command->id;
    } else {
      assault.target = approach.position;
    }
    const auto destination = in_commit_range ? known_command->position : approach.position;
    auto candidate = CandidateBuilder{AIAction::AssaultCommand, std::move(assault)};
    candidate
        .add(AIUtilityReason::Baseline,
             apply_ai_weight(
                 3'000, context.doctrine.aggression_weight_basis_points))
        .add(AIUtilityReason::EnemyCommandExposed,
             apply_ai_weight(
                 11'000, context.doctrine.aggression_weight_basis_points))
        .target(known_command->id)
        .position(destination)
        .influence(context.tactical_map(), destination);
    add_influence_utility(
        candidate, context,
        in_commit_range ? context.tactical_map().sample_at(destination) : approach.sample, false,
        !in_commit_range);
    candidates.push_back(std::move(candidate).finish());
  }

  if (context.ready_army.empty() && !context.attrition_commitment) {
    return;
  }

  std::vector<const ObservedEnemy*> combat_enemies;
  for (const auto* enemy : context.visible_enemies) {
    if (is_army_unit(enemy->type)) {
      combat_enemies.push_back(enemy);
    }
  }
  auto combat_power = std::int32_t{};
  for (const auto* unit : assault_force) {
    combat_power += entity_power(*unit, context.observation.self().faction);
  }
  const auto acceptable_losses =
      context.visible_enemy_power > 0 &&
      static_cast<std::int64_t>(combat_power) * 10'000 >=
          static_cast<std::int64_t>(context.visible_enemy_power) *
              context.doctrine.engagement_power_ratio_basis_points;
  if (combat_enemies.empty() ||
      (!context.attrition_commitment && !acceptable_losses)) {
    return;
  }
  const auto enemy_center = enemy_centroid(combat_enemies);
  const auto approach = safe_approach(context, centroid(assault_force), enemy_center);
  const auto target = approach.position;
  auto engage = command_for(context.observation.player(), CommandType::AttackMove);
  engage.entities = entity_ids(assault_force);
  engage.target = target;
  const auto advantage = std::max(0, combat_power - context.visible_enemy_power);
  const auto engagement_ratio =
      context.visible_enemy_power <= 0
          ? 20'000
          : static_cast<std::int32_t>(
                std::min<std::int64_t>(
                    20'000,
                    static_cast<std::int64_t>(combat_power) * 10'000 /
                        context.visible_enemy_power));
  const auto acceptable_margin =
      std::max(0, engagement_ratio -
                      context.doctrine.engagement_power_ratio_basis_points);
  const auto enemy_resolve_deficit =
      std::max(0, 85 - context.visible_enemy_average_resolve);
  const auto terror_advantage =
      std::max(0, context.friendly_terror - context.visible_enemy_ward);
  auto candidate = CandidateBuilder{AIAction::EngageForce, std::move(engage)};
  candidate
      .add(AIUtilityReason::Baseline,
           apply_ai_weight(
               3'000, context.doctrine.aggression_weight_basis_points))
      .add(AIUtilityReason::FavorableEngagement,
           apply_ai_weight(
               4'500 + std::min(4'000, advantage * 8),
               context.doctrine.aggression_weight_basis_points))
      .add(AIUtilityReason::AcceptableLosses,
           std::min(2'500, acceptable_margin / 2))
      .add(AIUtilityReason::ResolvePreservation,
           apply_ai_weight(
               std::max(
                   0, context.average_army_resolve -
                          context.doctrine.tactical_retreat_resolve) *
                   35,
               context.doctrine.preservation_weight_basis_points))
      .add(AIUtilityReason::DreadExploitation,
           apply_ai_weight(
               enemy_resolve_deficit * 55 + terror_advantage * 35,
               context.doctrine.dread_exploitation_weight_basis_points))
      .position(target)
      .influence(context.tactical_map(), target);
  add_influence_utility(candidate, context, approach.sample, false, true);
  candidates.push_back(std::move(candidate).finish());
}

void add_reinforcement_candidate(const PlanningContext& context,
                                 std::vector<ScoredCommand>& candidates) {
  std::vector<const Entity*> active;
  std::vector<const Entity*> idle;
  for (const auto* unit : context.ready_army) {
    if (unit->order.type == OrderType::Attack || unit->order.type == OrderType::AttackMove) {
      active.push_back(unit);
    } else if (unit->order.type == OrderType::Idle || unit->order.type == OrderType::Hold) {
      idle.push_back(unit);
    }
  }
  if (active.size() < 2 || idle.empty()) {
    return;
  }
  const auto target = reinforcement_staging(context, centroid(idle), centroid(active));
  auto reinforce = command_for(context.observation.player(), CommandType::AttackMove);
  reinforce.entities = entity_ids(idle);
  reinforce.target = target.position;
  auto candidate = CandidateBuilder{AIAction::ReinforceFront, std::move(reinforce)};
  candidate.add(AIUtilityReason::Baseline, 2'000)
      .add(AIUtilityReason::ReinforcementReady,
           apply_ai_weight(
               4'500 + static_cast<std::int32_t>(idle.size()) * 300,
               context.doctrine.cohesion_weight_basis_points))
      .add(AIUtilityReason::FormationDoctrine,
           apply_ai_weight(
               500, context.doctrine.cohesion_weight_basis_points))
      .position(target.position)
      .influence(context.tactical_map(), target.position);
  add_influence_utility(candidate, context, target.sample);
  candidates.push_back(std::move(candidate).finish());
}

void add_objective_candidates(const PlanningContext& context,
                              std::vector<ScoredCommand>& candidates) {
  const auto visible_combat_enemy =
      std::ranges::any_of(context.visible_enemies,
                          [](const ObservedEnemy* enemy) { return is_army_unit(enemy->type); });
  auto objective_force = context.ready_army;
  const auto economy_exhausted =
      context.command_building != nullptr &&
      nearest_usable_resource(context.observation,
                              context.command_building->position) == nullptr;
  const auto emergency_worker_capture =
      objective_force.empty() && context.army.empty() && economy_exhausted &&
      !context.workers.empty();
  if (emergency_worker_capture) {
    objective_force.push_back(context.workers.front());
  }
  const auto minimum_assault_force =
      static_cast<std::size_t>(
          std::max(1, context.doctrine.minimum_assault_units));
  const auto fortified_recovery =
      knows_enemy_fortification(context) && !objective_force.empty() &&
      objective_force.size() < minimum_assault_force;
  if ((!emergency_worker_capture && !fortified_recovery &&
       objective_force.size() < minimum_assault_force) ||
      visible_combat_enemy) {
    return;
  }
  const auto recovery_utility =
      emergency_worker_capture ? 12'000 : (fortified_recovery ? 6'000 : 0);
  const auto origin = centroid(objective_force);
  for (const auto& objective : context.observation.public_objectives()) {
    if (objective.has_observed_state &&
        objective.last_observed_owner == context.observation.player()) {
      continue;
    }
    const auto destination = safe_advance(context, origin, objective.position, false);
    auto capture = command_for(context.observation.player(), CommandType::AttackMove);
    capture.entities = entity_ids(objective_force);
    capture.target = destination.position;
    const auto distance_world =
        static_cast<std::int32_t>(integer_sqrt(squared_distance(origin, objective.position)) /
                                  static_cast<std::uint64_t>(kWorldScale));
    auto candidate = CandidateBuilder{AIAction::CaptureObjective, std::move(capture)};
    candidate.add(AIUtilityReason::Baseline, 2'000)
        .add(AIUtilityReason::ObjectiveAvailable,
             apply_ai_weight(
                 5'000 - std::min(2'500, distance_world),
                 context.doctrine.objective_weight_basis_points))
        .add(AIUtilityReason::CombatRecovery, recovery_utility)
        .objective(objective.id)
        .position(destination.position)
        .influence(context.tactical_map(), destination.position);
    add_influence_utility(candidate, context, destination.sample);
    candidates.push_back(std::move(candidate).finish());
  }
}

void add_search_candidate(const PlanningContext& context, std::vector<ScoredCommand>& candidates) {
  if (!context.visible_enemies.empty()) {
    return;
  }
  const auto search_force = tactical_field_force(context);
  const auto* known_command = latest_known_command(context);
  const auto minimum_search_force = minimum_base_assault_force(context);
  if (known_command != nullptr && !known_command->currently_visible) {
    if (search_force.size() < minimum_search_force) {
      return;
    }
    const auto destination =
        context.search_commitment
            ? DestinationChoice{known_command->position,
                                context.tactical_map().sample_at(known_command->position),
                                std::numeric_limits<std::int64_t>::max()}
            : safe_advance(context, centroid(search_force), known_command->position, false);
    auto search = command_for(context.observation.player(), CommandType::AttackMove);
    search.entities = entity_ids(search_force);
    search.target = destination.position;
    const auto age = context.observation.tick() - known_command->last_observed_tick;
    auto candidate = CandidateBuilder{AIAction::SearchEnemyCommand, std::move(search)};
    candidate.add(AIUtilityReason::Baseline, 2'000)
        .add(AIUtilityReason::LastKnownCommand,
             apply_ai_weight(
                 6'500 -
                     static_cast<std::int32_t>(
                         std::min<Tick>(2'500, age)),
                 context.doctrine.scouting_weight_basis_points))
        .target(known_command->id)
        .position(destination.position)
        .influence(context.tactical_map(), destination.position);
    add_influence_utility(candidate, context, destination.sample);
    candidates.push_back(std::move(candidate).finish());
    return;
  }

  if (context.observation.tick() >= 2'400 && !search_force.empty() &&
      context.command_building != nullptr) {
    const auto goal = Vec2{context.observation.map_size().x - context.command_building->position.x,
                           context.command_building->position.y};
    const auto destination = context.search_commitment
                                 ? DestinationChoice{goal, context.tactical_map().sample_at(goal),
                                                     std::numeric_limits<std::int64_t>::max()}
                                 : safe_advance(context, centroid(search_force), goal, false);
    auto search = command_for(context.observation.player(), CommandType::AttackMove);
    search.entities = entity_ids(search_force);
    search.target = destination.position;
    auto candidate = CandidateBuilder{AIAction::SearchEnemyCommand, std::move(search)};
    candidate.add(AIUtilityReason::Baseline, 2'000)
        .add(AIUtilityReason::InformationNeed,
             apply_ai_weight(
                 6'200, context.doctrine.scouting_weight_basis_points))
        .position(destination.position)
        .influence(context.tactical_map(), destination.position);
    add_influence_utility(candidate, context, destination.sample);
    candidates.push_back(std::move(candidate).finish());
  }
}

void add_scout_candidate(const PlanningContext& context, std::vector<ScoredCommand>& candidates) {
  const auto field_force = tactical_field_force(context);
  if (!context.visible_enemies.empty() ||
      context.observation.tick() < context.doctrine.earliest_scout_tick ||
      context.command_building == nullptr ||
      (context.observation.tick() >= 2'400 && !field_force.empty())) {
    return;
  }

  const Entity* scout = nullptr;
  const auto ready_skirmisher = std::ranges::find_if(context.ready_army, [](const Entity* entity) {
    return entity->type == EntityType::Skirmisher;
  });
  const auto reserve =
      static_cast<std::size_t>(
          std::max(0, context.doctrine.scout_army_reserve));
  const auto can_spend_army =
      context.doctrine.scouting == AIScoutingDoctrine::PredatoryProbe
          ? context.ready_army.size() >= reserve &&
                !context.ready_army.empty()
          : context.ready_army.size() > reserve;
  if (ready_skirmisher != context.ready_army.end() &&
      (can_spend_army ||
       context.doctrine.scouting == AIScoutingDoctrine::FarSightScreen)) {
    scout = *ready_skirmisher;
  } else if (can_spend_army) {
    scout = context.ready_army.front();
  } else if (context.workers.size() >
             static_cast<std::size_t>(
                 std::max(0, context.doctrine.worker_target))) {
    scout = context.workers.back();
  }
  if (scout == nullptr) {
    return;
  }
  const auto goal = scouting_goal(context, scout->position);
  const auto destination = safe_advance(context, scout->position, goal, true);
  const auto target = destination.position;
  if ((scout->order.type == OrderType::Move || scout->order.type == OrderType::AttackMove) &&
      squared_distance(scout->order.target, target) <=
          static_cast<std::uint64_t>(80'000) * 80'000U) {
    return;
  }

  auto command = command_for(context.observation.player(), CommandType::AttackMove);
  command.entities = {scout->id};
  command.target = target;
  auto candidate = CandidateBuilder{AIAction::Scout, std::move(command)};
  candidate.add(AIUtilityReason::Baseline, 1'500)
      .add(AIUtilityReason::InformationNeed,
           apply_ai_weight(
               3'800, context.doctrine.scouting_weight_basis_points))
      .add(AIUtilityReason::ScoutingDoctrine,
           apply_ai_weight(
               600, context.doctrine.scouting_weight_basis_points))
      .target(scout->id)
      .position(target)
      .influence(context.tactical_map(), target);
  add_influence_utility(candidate, context, destination.sample, true);
  candidates.push_back(std::move(candidate).finish());
}

}  // namespace

std::optional<AIPlannedDecision> evaluate_tactical_layer(const PlanningContext& context) {
  std::vector<ScoredCommand> candidates;
  add_retreat_candidate(context, candidates);
  add_power_candidate(context, candidates);
  add_engagement_candidates(context, candidates);
  add_reinforcement_candidate(context, candidates);
  add_objective_candidates(context, candidates);
  add_search_candidate(context, candidates);
  add_scout_candidate(context, candidates);
  return select_decision(AIDecisionLayer::Tactical,
                         context.difficulty.tactical_cadence_ticks, context,
                         std::move(candidates));
}

}  // namespace ashen::core::ai
