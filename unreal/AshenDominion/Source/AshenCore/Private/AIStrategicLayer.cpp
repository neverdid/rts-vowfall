#include "AIPlanningInternal.hpp"

#include "ashen/core/Catalog.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <ranges>
#include <utility>
#include <vector>

namespace ashen::core::ai {
namespace {

[[nodiscard]] const Entity* first_orphaned_site(const PlanningContext& context,
                                                const EntityType type) noexcept {
  for (const auto& site : context.observation.owned_entities()) {
    if (site.type != type || !site.alive() || !site.under_construction) {
      continue;
    }
    const auto staffed = std::ranges::any_of(
        context.observation.owned_entities(), [site_id = site.id](const Entity& worker) {
          return worker.type == EntityType::Worker && worker.alive() &&
                 worker.order.type == OrderType::Build && worker.order.target_entity == site_id;
        });
    if (!staffed) {
      return &site;
    }
  }
  return nullptr;
}

[[nodiscard]] std::size_t building_count(const PlanningContext& context, const EntityType type,
                                         const bool completed_only = false) noexcept {
  return static_cast<std::size_t>(std::ranges::count_if(
      context.observation.owned_entities(), [=](const Entity& entity) {
        return entity.type == type && entity.alive() &&
               (!completed_only || !entity.under_construction);
      }));
}

[[nodiscard]] const CommandCapability* available_builder(const PlanningContext& context,
                                                         const EntityType building) noexcept {
  for (const auto& capability : context.observation.capabilities()) {
    if (capability.type != CommandType::Build || capability.entity_type != building) {
      continue;
    }
    const auto* worker = context.owned(capability.actor);
    if (worker != nullptr && worker->type == EntityType::Worker && worker->alive() &&
        worker->order.type != OrderType::Build) {
      return &capability;
    }
  }
  return nullptr;
}

[[nodiscard]] Vec2 structure_position(const PlanningContext& context,
                                      const EntityType building,
                                      const std::size_t existing_count) noexcept {
  if (context.command_building == nullptr) {
    return {};
  }
  const auto direction = context.command_building->position.x < context.observation.map_size().x / 2
                             ? 1
                             : -1;
  constexpr std::array<Vec2, 8> barracks_offsets = {
      Vec2{190 * kWorldScale, -135 * kWorldScale},
      Vec2{190 * kWorldScale, 135 * kWorldScale},
      Vec2{260 * kWorldScale, -95 * kWorldScale},
      Vec2{260 * kWorldScale, 95 * kWorldScale},
      Vec2{345 * kWorldScale, -155 * kWorldScale},
      Vec2{345 * kWorldScale, 155 * kWorldScale},
      Vec2{420 * kWorldScale, -90 * kWorldScale},
      Vec2{420 * kWorldScale, 90 * kWorldScale},
  };
  constexpr std::array<Vec2, 8> turret_offsets = {
      Vec2{330 * kWorldScale, 175 * kWorldScale},
      Vec2{330 * kWorldScale, -175 * kWorldScale},
      Vec2{275 * kWorldScale, 230 * kWorldScale},
      Vec2{275 * kWorldScale, -230 * kWorldScale},
      Vec2{390 * kWorldScale, 225 * kWorldScale},
      Vec2{390 * kWorldScale, -225 * kWorldScale},
      Vec2{240 * kWorldScale, 285 * kWorldScale},
      Vec2{240 * kWorldScale, -285 * kWorldScale},
  };
  const auto cadence =
      std::max<Tick>(1, context.difficulty.strategic_cadence_ticks);
  const auto attempt =
      static_cast<std::size_t>(context.observation.tick() / cadence);
  if (building == EntityType::Turret) {
    const auto offset =
        turret_offsets[(attempt + existing_count) % turret_offsets.size()];
    return {context.command_building->position.x + direction * offset.x,
            context.command_building->position.y + offset.y};
  }
  const auto offset = barracks_offsets[(attempt + existing_count) % barracks_offsets.size()];
  return {context.command_building->position.x + direction * offset.x,
          context.command_building->position.y + offset.y};
}

[[nodiscard]] std::optional<AIPlannedDecision> assign_gatherers(
    const PlanningContext& context) {
  if (context.workers.empty() || context.command_building == nullptr) {
    return std::nullopt;
  }
  const auto* resource = nearest_usable_resource(context.observation,
                                                 context.command_building->position);
  if (resource == nullptr) {
    return std::nullopt;
  }

  auto gather = command_for(context.observation.player(), CommandType::Gather);
  for (const auto* worker : context.workers) {
    if (worker->order.type == OrderType::Build) {
      continue;
    }
    if (worker->order.type != OrderType::Gather || worker->order.resource != resource->id) {
      gather.entities.push_back(worker->id);
    }
  }
  if (gather.entities.empty()) {
    return std::nullopt;
  }
  gather.resource = resource->id;

  std::vector<ScoredCommand> candidates;
  candidates.push_back(std::move(CandidateBuilder{AIAction::AssignGatherers, std::move(gather)}
                                     .add(AIUtilityReason::Baseline, 1'000)
                                     .add(AIUtilityReason::IdleWorkers,
                                          apply_ai_weight(
                                              static_cast<std::int32_t>(
                                                  context.workers.size()) *
                                                  700,
                                              context.doctrine
                                                  .economy_weight_basis_points))
                                     .position(resource->position))
                           .finish());
  return select_decision(AIDecisionLayer::Strategic,
                         context.difficulty.strategic_cadence_ticks,
                         context,
                         std::move(candidates));
}

void add_construction_candidates(const PlanningContext& context,
                                 std::vector<ScoredCommand>& candidates) {
  const auto barracks_total = building_count(context, EntityType::Barracks);
  const auto barracks_completed = building_count(context, EntityType::Barracks, true);
  const auto turret_total = building_count(context, EntityType::Turret);
  const auto* orphaned_barracks = first_orphaned_site(context, EntityType::Barracks);
  const auto* orphaned_turret = first_orphaned_site(context, EntityType::Turret);

  if (orphaned_barracks != nullptr) {
    if (const auto* builder = available_builder(context, EntityType::Barracks)) {
      auto command = command_for(context.observation.player(), CommandType::Build);
      command.entities = {builder->actor};
      command.target = orphaned_barracks->position;
      command.target_entity = orphaned_barracks->id;
      command.building_type = EntityType::Barracks;
      candidates.push_back(std::move(CandidateBuilder{AIAction::ResumeBarracks, std::move(command)}
                                         .add(AIUtilityReason::Baseline, 2'000)
                                         .add(AIUtilityReason::OrphanedConstruction, 28'000)
                                         .target(orphaned_barracks->id)
                                         .position(orphaned_barracks->position)
                                         .entity_type(EntityType::Barracks))
                               .finish());
    }
  }

  if (orphaned_turret != nullptr) {
    if (const auto* builder = available_builder(context, EntityType::Turret)) {
      auto command = command_for(context.observation.player(), CommandType::Build);
      command.entities = {builder->actor};
      command.target = orphaned_turret->position;
      command.target_entity = orphaned_turret->id;
      command.building_type = EntityType::Turret;
      candidates.push_back(std::move(CandidateBuilder{AIAction::ResumeTurret, std::move(command)}
                                         .add(AIUtilityReason::Baseline, 2'000)
                                         .add(AIUtilityReason::OrphanedConstruction, 24'000)
                                         .target(orphaned_turret->id)
                                         .position(orphaned_turret->position)
                                         .entity_type(EntityType::Turret))
                               .finish());
    }
  }

  const auto supply_headroom = context.observation.self().supply_cap -
                               context.observation.self().supply_used;
  const auto opening_required = barracks_total == 0;
  const auto supply_required = barracks_completed > 0 && supply_headroom <= 4 &&
                               barracks_total < 3;
  const auto capacity_wanted = context.observation.tick() >= 1'600 && context.army.size() >= 8 &&
                               barracks_completed < 2;
  if ((opening_required || supply_required || capacity_wanted) &&
      orphaned_barracks == nullptr && context.command_building != nullptr) {
    if (const auto* builder = available_builder(context, EntityType::Barracks)) {
      auto command = command_for(context.observation.player(), CommandType::Build);
      command.entities = {builder->actor};
      command.target = structure_position(context, EntityType::Barracks, barracks_total);
      command.building_type = EntityType::Barracks;
      auto candidate = CandidateBuilder{AIAction::BuildBarracks, std::move(command)};
      candidate.add(AIUtilityReason::Baseline, 500);
      if (opening_required) {
        candidate.add(AIUtilityReason::RequiredOpening, 20'000);
      }
      if (supply_required) {
        candidate.add(
            AIUtilityReason::SupplyPressure,
            apply_ai_weight(14'000, context.doctrine.economy_weight_basis_points));
      }
      if (capacity_wanted) {
        candidate.add(AIUtilityReason::ProductionCapacity,
                      apply_ai_weight(
                          5'000,
                          context.doctrine.aggression_weight_basis_points));
      }
      candidate.position(structure_position(context, EntityType::Barracks, barracks_total))
          .entity_type(EntityType::Barracks);
      candidates.push_back(std::move(candidate).finish());
    }
  }

  if (turret_total == 0 && orphaned_turret == nullptr && barracks_completed > 0 &&
      context.observation.tick() >= context.doctrine.first_fortification_tick &&
      context.command_building != nullptr) {
    if (const auto* builder = available_builder(context, EntityType::Turret)) {
      auto command = command_for(context.observation.player(), CommandType::Build);
      command.entities = {builder->actor};
      command.target = structure_position(context, EntityType::Turret, turret_total);
      command.building_type = EntityType::Turret;
      auto candidate = CandidateBuilder{AIAction::BuildTurret, std::move(command)};
      candidate.add(
          AIUtilityReason::Baseline,
          apply_ai_weight(
              3'200, context.doctrine.fortification_weight_basis_points));
      if (context.visible_enemy_power > context.friendly_power) {
        candidate.add(
            AIUtilityReason::Outnumbered,
            apply_ai_weight(
                3'000,
                context.doctrine.preservation_weight_basis_points));
      }
      candidate.position(structure_position(context, EntityType::Turret, turret_total))
          .entity_type(EntityType::Turret);
      candidates.push_back(std::move(candidate).finish());
    }
  }
}

void add_research_candidates(const PlanningContext& context,
                             std::vector<ScoredCommand>& candidates) {
  if (const auto* capability = first_capability(context.observation, CommandType::Research,
                                                std::nullopt, ResearchId::TierTwo)) {
    auto command = command_for(context.observation.player(), CommandType::Research);
    command.producer = capability->actor;
    command.research = ResearchId::TierTwo;
    candidates.push_back(std::move(CandidateBuilder{AIAction::ResearchTierTwo, std::move(command)}
                                       .add(AIUtilityReason::Baseline, 2'000)
                                       .add(AIUtilityReason::TechnologyTiming,
                                            apply_ai_weight(
                                                5'500 +
                                                    static_cast<std::int32_t>(
                                                        context.army.size()) *
                                                        120,
                                                context.doctrine
                                                    .technology_weight_basis_points))
                                       .target(capability->actor)
                                       .research(ResearchId::TierTwo))
                             .finish());
  }

  if (const auto doctrine = faction_doctrine(context.observation.self().faction)) {
    if (const auto* capability = first_capability(context.observation, CommandType::Research,
                                                  std::nullopt, doctrine)) {
      auto command = command_for(context.observation.player(), CommandType::Research);
      command.producer = capability->actor;
      command.research = *doctrine;
      candidates.push_back(std::move(CandidateBuilder{AIAction::ResearchDoctrine, std::move(command)}
                                         .add(AIUtilityReason::Baseline, 1'800)
                                         .add(AIUtilityReason::FactionDoctrine,
                                              apply_ai_weight(
                                                  4'800 +
                                                      static_cast<std::int32_t>(
                                                          context.army.size()) *
                                                          100,
                                                  context.doctrine
                                                      .technology_weight_basis_points))
                                         .target(capability->actor)
                                         .research(*doctrine))
                               .finish());
    }
  }
}

void add_production_candidates(const PlanningContext& context,
                               std::vector<ScoredCommand>& candidates) {
  const auto worker_target =
      static_cast<std::size_t>(std::max(0, context.doctrine.worker_target));
  const auto* harvest_target =
      context.command_building == nullptr
          ? nullptr
          : nearest_usable_resource(context.observation,
                                    context.command_building->position);
  const auto effective_worker_target =
      context.army.empty()
          ? std::size_t{1}
          : (harvest_target != nullptr ? worker_target : std::size_t{0});
  if (context.workers.size() < effective_worker_target) {
    if (const auto* capability = first_capability(context.observation, CommandType::Train,
                                                  EntityType::Worker)) {
      auto command = command_for(context.observation.player(), CommandType::Train);
      command.producer = capability->actor;
      command.train_type = EntityType::Worker;
      candidates.push_back(std::move(CandidateBuilder{AIAction::TrainWorker, std::move(command)}
                                         .add(AIUtilityReason::Baseline, 2'000)
                                         .add(AIUtilityReason::EconomyTarget,
                                              apply_ai_weight(
                                                  9'000 +
                                                      static_cast<std::int32_t>(
                                                          effective_worker_target -
                                                          context.workers.size()) *
                                                          1'200,
                                                  context.doctrine
                                                      .economy_weight_basis_points))
                                         .target(capability->actor)
                                         .entity_type(EntityType::Worker))
                               .finish());
    }
  }

  const auto military_recovery = context.army.empty();
  auto armored_enemies = std::int32_t{0};
  auto enemy_structures = std::int32_t{0};
  for (const auto* enemy : context.visible_enemies) {
    const auto definition = entity_definition(context.observation.opponent_faction(), enemy->type);
    armored_enemies += definition.armor == ArmorClass::Armored ? 1 : 0;
    enemy_structures += enemy->kind == EntityKind::Building ? 1 : 0;
  }

  if (const auto* capability = first_capability(context.observation, CommandType::Train,
                                                EntityType::Vanguard)) {
    auto command = command_for(context.observation.player(), CommandType::Train);
    command.producer = capability->actor;
    command.train_type = EntityType::Vanguard;
    auto candidate = CandidateBuilder{AIAction::TrainVanguard, std::move(command)};
    candidate
        .add(AIUtilityReason::Baseline,
             apply_ai_weight(
                 4'400, context.doctrine.vanguard_weight_basis_points))
        .add(AIUtilityReason::PressureStructures,
             apply_ai_weight(
                 enemy_structures * 600,
                 context.doctrine.aggression_weight_basis_points))
        .add(AIUtilityReason::CompositionBalance,
             context.vanguards.size() <= context.skirmishers.size() ? 1'400 : 0)
        .add(AIUtilityReason::CombatRecovery,
             military_recovery
                 ? apply_ai_weight(
                       12'000,
                       context.doctrine.aggression_weight_basis_points)
                 : 0)
        .add(AIUtilityReason::Baseline,
             context.doctrine.temperament == AITemperament::Audacious ? 180
                                                                      : 0)
        .target(capability->actor)
        .entity_type(EntityType::Vanguard);
    candidates.push_back(std::move(candidate).finish());
  }

  if (const auto* capability = first_capability(context.observation, CommandType::Train,
                                                EntityType::Skirmisher)) {
    auto command = command_for(context.observation.player(), CommandType::Train);
    command.producer = capability->actor;
    command.train_type = EntityType::Skirmisher;
    auto candidate = CandidateBuilder{AIAction::TrainSkirmisher, std::move(command)};
    candidate
        .add(AIUtilityReason::Baseline,
             apply_ai_weight(
                 4'400, context.doctrine.skirmisher_weight_basis_points))
        .add(AIUtilityReason::CounterArmored,
             apply_ai_weight(
                 armored_enemies * 800,
                 context.doctrine.aggression_weight_basis_points))
        .add(AIUtilityReason::CompositionBalance,
             context.skirmishers.size() < context.vanguards.size() ? 1'400 : 0)
        .add(AIUtilityReason::CombatRecovery,
             military_recovery
                 ? apply_ai_weight(
                       12'000,
                       context.doctrine.aggression_weight_basis_points)
                 : 0)
        .add(AIUtilityReason::Baseline,
             context.doctrine.temperament == AITemperament::Watchful ? 180
                                                                     : 0)
        .target(capability->actor)
        .entity_type(EntityType::Skirmisher);
    candidates.push_back(std::move(candidate).finish());
  }
}

}  // namespace

std::vector<AIPlannedDecision> evaluate_strategic_layer(const PlanningContext& context) {
  std::vector<AIPlannedDecision> decisions;
  if (const auto gatherers = assign_gatherers(context)) {
    decisions.push_back(*gatherers);
  }

  std::vector<ScoredCommand> candidates;
  add_construction_candidates(context, candidates);
  add_research_candidates(context, candidates);
  add_production_candidates(context, candidates);
  if (const auto macro = select_decision(AIDecisionLayer::Strategic,
                                         context.difficulty.strategic_cadence_ticks,
                                         context,
                                         std::move(candidates))) {
    decisions.push_back(*macro);
  }
  return decisions;
}

}  // namespace ashen::core::ai
