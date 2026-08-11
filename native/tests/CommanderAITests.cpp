#include "ashen/core/Catalog.hpp"
#include "ashen/core/CommanderAI.hpp"
#include "ashen/core/Simulation.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <ranges>
#include <string_view>

namespace {

using namespace ashen::core;

int failures = 0;

#define CHECK(condition)                                                                            \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition "\n";               \
      ++failures;                                                                                   \
    }                                                                                               \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name << '\n';
}

[[nodiscard]] Simulation sandbox() {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.starting_ore = {1'000, 1'000};
  config.map_size = world(1'200, 800);
  config.navigation_obstacles.clear();
  return Simulation{config};
}

[[nodiscard]] SimulationConfig open_config() {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.map_size = world(1'600, 800);
  config.navigation_obstacles.clear();
  return config;
}

void hold(Simulation& simulation, const PlayerId player,
          const std::initializer_list<EntityId> entities) {
  CHECK(simulation.execute_now(
              Command{.player = player, .type = CommandType::Hold, .entities = entities})
            .ok);
}

[[nodiscard]] const AIPlannedDecision* decision_for(const CommanderPlan& plan,
                                                     const AIDecisionLayer layer) noexcept {
  const auto found = std::ranges::find(plan.decisions, layer, &AIPlannedDecision::layer);
  return found == plan.decisions.end() ? nullptr : &*found;
}

[[nodiscard]] bool has_action(const CommanderPlan& plan, const AIAction action) noexcept {
  return std::ranges::any_of(plan.decisions, [action](const AIPlannedDecision& decision) {
    return decision.selected_action == action;
  });
}

[[nodiscard]] bool valid_score_trace(const AIPlannedDecision& decision) noexcept {
  if (decision.candidates.empty() || decision.selected_candidate >= decision.candidates.size()) {
    return false;
  }
  const auto& difficulty = ai_difficulty_profile(decision.difficulty);
  if (decision.difficulty != AIDifficulty::Competitive ||
      decision.difficulty_hash != ai_difficulty_hash(difficulty) ||
      decision.strategy_state_hash == 0 ||
      decision.evaluated_candidates != decision.candidates.size() ||
      decision.selected_quality_basis_points != 10'000 ||
      decision.mistake_applied ||
      decision.command_precision_offset != Vec2{} ||
      decision.command_latency_ticks != difficulty.command_latency_ticks) {
    return false;
  }
  for (const auto& candidate : decision.candidates) {
    std::int64_t total = 0;
    for (const auto& component : candidate.components) {
      total += component.score;
    }
    if (candidate.components.empty() || total != candidate.total_score) {
      return false;
    }
    if (decision.layer == AIDecisionLayer::Tactical &&
        (candidate.influence_map_hash == 0 ||
         !candidate.influence_sample.has_value())) {
      return false;
    }
  }

  const auto& selected = decision.candidates[decision.selected_candidate];
  if (selected.action != decision.selected_action ||
      std::ranges::any_of(decision.candidates, [&](const AICandidateScore& candidate) {
        return candidate.total_score > selected.total_score;
      })) {
    return false;
  }

  auto expected_reason = AIUtilityReason::Baseline;
  auto best_component = std::numeric_limits<std::int32_t>::min();
  for (const auto& component : selected.components) {
    if (component.score > best_component ||
        (component.score == best_component && component.reason < expected_reason)) {
      expected_reason = component.reason;
      best_component = component.score;
    }
  }
  return decision.winning_reason == expected_reason;
}

void layer_cadences_are_distinct_and_human_bounded() {
  CHECK(ai_decision_cadence(AIDecisionLayer::Strategic) == 80);
  CHECK(ai_decision_cadence(AIDecisionLayer::Tactical) == 120);
  CHECK(ai_decision_cadence(AIDecisionLayer::Micro) == 12);
  CHECK(ai_decision_due(AIDecisionLayer::Strategic, 1));
  CHECK(ai_decision_due(AIDecisionLayer::Strategic, 160));
  CHECK(ai_decision_due(AIDecisionLayer::Tactical, 30));
  CHECK(ai_decision_due(AIDecisionLayer::Tactical, 150));
  CHECK(ai_decision_due(AIDecisionLayer::Micro, 12));
  CHECK(!ai_decision_due(AIDecisionLayer::Strategic, 12));
  CHECK(!ai_decision_due(AIDecisionLayer::Tactical, 12));
  CHECK(!ai_decision_due(AIDecisionLayer::Micro, 30));
}

void strategic_layer_scores_the_opening_and_worker_allocation() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(180, 400)));
  for (const auto y : {350, 400, 450}) {
    static_cast<void>(
        simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(250, y)));
  }
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(1'020, 400)));
  static_cast<void>(simulation.add_resource(world(390, 400), 1'200));
  simulation.step();

  const auto plan = CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  CHECK(plan.decisions.size() == 2);
  CHECK(has_action(plan, AIAction::AssignGatherers));
  CHECK(has_action(plan, AIAction::BuildBarracks));
  CHECK(std::ranges::all_of(plan.decisions, valid_score_trace));
  const auto build = std::ranges::find_if(plan.decisions, [](const AIPlannedDecision& decision) {
    return decision.selected_action == AIAction::BuildBarracks;
  });
  CHECK(build != plan.decisions.end());
  CHECK(build != plan.decisions.end() && build->winning_reason == AIUtilityReason::RequiredOpening);
  CHECK(build != plan.decisions.end() && build->command.type == CommandType::Build);
  if (build != plan.decisions.end()) {
    CHECK(simulation.execute_now(build->command).ok);
    const auto site = std::ranges::find_if(
        simulation.entities(), [](const Entity& entity) {
          return entity.type == EntityType::Barracks &&
                 entity.owner == PlayerId::One && entity.under_construction;
        });
    CHECK(site != simulation.entities().end());
    CHECK(site != simulation.entities().end() &&
          simulation.is_supply_connected(site->id));
  }
}

void strategic_layer_rebuilds_combat_before_an_exhausted_economy() {
  for (const auto faction :
       {FactionId::Compact, FactionId::Ascendancy, FactionId::Concord}) {
    auto config = open_config();
    config.player_one_faction = faction;
    config.starting_ore = {1'000, 0};
    Simulation simulation{config};
    static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                              world(180, 400)));
    static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Barracks,
                                              world(340, 400)));
    const auto worker = simulation.spawn_entity(
        PlayerId::One, EntityType::Worker, world(260, 400));
    static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                              world(1'420, 400)));
    const auto resource = simulation.add_resource(world(430, 400), 1'200);
    CHECK(simulation
              .execute_now(Command{.player = PlayerId::One,
                                   .type = CommandType::Gather,
                                   .entities = {worker},
                                   .resource = resource})
              .ok);
    simulation.step();

    const auto plan =
        CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
    const auto* strategic = decision_for(plan, AIDecisionLayer::Strategic);
    CHECK(strategic != nullptr);
    CHECK(strategic != nullptr &&
          strategic->selected_action == AIAction::TrainVanguard);
    CHECK(strategic != nullptr &&
          strategic->winning_reason == AIUtilityReason::CombatRecovery);
    CHECK(strategic != nullptr &&
          std::ranges::none_of(
              strategic->candidates, [](const AICandidateScore& candidate) {
                return candidate.action == AIAction::TrainWorker;
              }));
    CHECK(strategic != nullptr && valid_score_trace(*strategic));
  }
}

void strategic_layer_uses_one_worker_to_escape_total_attrition() {
  for (const auto faction :
       {FactionId::Compact, FactionId::Ascendancy, FactionId::Concord}) {
    auto config = open_config();
    config.player_one_faction = faction;
    config.starting_ore = {
        entity_definition(faction, EntityType::Worker).cost, 0};
    Simulation simulation{config};
    static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                              world(180, 400)));
    static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Barracks,
                                              world(340, 400)));
    static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                              world(1'420, 400)));
    simulation.step();

    const auto plan =
        CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
    const auto* strategic = decision_for(plan, AIDecisionLayer::Strategic);
    CHECK(strategic != nullptr);
    CHECK(strategic != nullptr &&
          strategic->selected_action == AIAction::TrainWorker);
    CHECK(strategic != nullptr && valid_score_trace(*strategic));
  }
}

void tactical_layer_sends_the_recovery_worker_to_map_control() {
  for (const auto faction :
       {FactionId::Compact, FactionId::Ascendancy, FactionId::Concord}) {
    auto config = open_config();
    config.player_one_faction = faction;
    config.starting_ore = {0, 0};
    Simulation simulation{config};
    static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                              world(180, 400)));
    const auto worker = simulation.spawn_entity(
        PlayerId::One, EntityType::Worker, world(260, 400));
    static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                              world(1'420, 400)));
    static_cast<void>(simulation.add_control_point(world(700, 400)));
    hold(simulation, PlayerId::One, {worker});
    simulation.run(kTacticalDecisionPhase);

    const auto plan =
        CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
    const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
    CHECK(tactical != nullptr);
    CHECK(tactical != nullptr &&
          tactical->selected_action == AIAction::CaptureObjective);
    CHECK(tactical != nullptr &&
          tactical->command.entities == std::vector<EntityId>{worker});
    CHECK(tactical != nullptr &&
          tactical->winning_reason == AIUtilityReason::CombatRecovery);
    CHECK(tactical != nullptr && valid_score_trace(*tactical));
  }
}

void tactical_layer_stages_reinforcements_before_a_fortified_assault() {
  for (const auto faction :
       {FactionId::Compact, FactionId::Ascendancy, FactionId::Concord}) {
    auto config = open_config();
    config.player_one_faction = faction;
    config.starting_ore = {0, 0};
    Simulation simulation{config};
    static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                              world(160, 400)));
    const auto recovery_unit = simulation.spawn_entity(
        PlayerId::One, EntityType::Vanguard, world(360, 400));
    static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                              world(1'420, 400)));
    static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Turret,
                                              world(550, 400)));
    static_cast<void>(simulation.add_control_point(world(760, 650)));
    hold(simulation, PlayerId::One, {recovery_unit});
    simulation.run(kTacticalDecisionPhase);

    const auto plan =
        CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
    const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
    CHECK(tactical != nullptr);
    CHECK(tactical != nullptr &&
          tactical->selected_action == AIAction::CaptureObjective);
    CHECK(tactical != nullptr &&
          tactical->command.entities == std::vector<EntityId>{recovery_unit});
    CHECK(tactical != nullptr &&
          std::ranges::any_of(
              tactical->candidates[tactical->selected_candidate].components,
              [](const AIUtilityComponent& component) {
                return component.reason == AIUtilityReason::CombatRecovery &&
                       component.score > 0;
              }));
    CHECK(tactical != nullptr && valid_score_trace(*tactical));
  }
}

void tactical_layer_retreats_from_visible_superior_force() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(180, 400)));
  const auto defender =
      simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(360, 400));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(1'020, 400)));
  const auto enemy_one =
      simulation.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(540, 340));
  const auto enemy_two =
      simulation.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(540, 400));
  const auto enemy_three =
      simulation.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(540, 460));
  hold(simulation, PlayerId::One, {defender});
  hold(simulation, PlayerId::Two, {enemy_one, enemy_two, enemy_three});
  simulation.run(30);

  const auto plan = CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
  CHECK(tactical != nullptr);
  CHECK(tactical != nullptr && tactical->selected_action == AIAction::Retreat);
  CHECK(tactical != nullptr && tactical->winning_reason == AIUtilityReason::Outnumbered);
  CHECK(tactical != nullptr && tactical->command.type == CommandType::Retreat);
  CHECK(tactical != nullptr && tactical->command.target != Vec2{});
  CHECK(tactical != nullptr && valid_score_trace(*tactical));
}

void tactical_layer_flanks_away_from_a_defended_side() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(160, 400)));
  const auto first = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(360, 370));
  const auto second = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(360, 430));
  const auto ranged = simulation.spawn_entity(
      PlayerId::One, EntityType::Skirmisher, world(390, 400));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'080, 400)));
  const auto contact = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(590, 400));
  const auto turret = simulation.spawn_entity(PlayerId::Two, EntityType::Turret,
                                              world(570, 245));
  hold(simulation, PlayerId::One, {first, second, ranged});
  hold(simulation, PlayerId::Two, {contact});
  simulation.run(30);

  const auto observation = simulation.observe(PlayerId::One);
  CHECK(std::ranges::any_of(
      observation.known_enemies(), [turret](const ObservedEnemy& enemy) {
        return enemy.id == turret && enemy.currently_visible;
      }));
  const AIInfluenceMap influence{observation};
  const auto plan = CommanderAI{PlayerId::One}.plan(observation);
  const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
  CHECK(tactical != nullptr);
  CHECK(tactical != nullptr &&
        tactical->selected_action == AIAction::EngageForce);
  CHECK(tactical != nullptr &&
        tactical->command.type == CommandType::AttackMove);
  CHECK(tactical != nullptr && tactical->command.target.y > world(400, 0).x);
  CHECK(tactical != nullptr &&
        influence.cell_at(tactical->command.target).static_danger <
            influence.cell_at(world(570, 300)).static_danger);
  CHECK(tactical != nullptr && valid_score_trace(*tactical));
}

void tactical_layer_avoids_remembered_static_danger_on_objective_approach() {
  auto config = open_config();
  config.starting_ore = {0, 0};
  Simulation simulation{config};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(160, 400)));
  const auto first = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(620, 360));
  const auto second = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(620, 400));
  const auto third = simulation.spawn_entity(
      PlayerId::One, EntityType::Skirmisher, world(620, 440));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'520, 400)));
  const auto turret = simulation.spawn_entity(PlayerId::Two, EntityType::Turret,
                                              world(850, 400));
  static_cast<void>(simulation.add_control_point(world(1'300, 400)));
  simulation.step();
  CHECK(std::ranges::any_of(simulation.observe(PlayerId::One).known_enemies(),
                            [turret](const ObservedEnemy& enemy) {
                              return enemy.id == turret &&
                                     enemy.currently_visible;
                            }));
  CHECK(simulation
            .execute_now(Command{.player = PlayerId::One,
                                 .type = CommandType::Move,
                                 .entities = {first, second, third},
                                 .target = world(360, 400)})
            .ok);
  while (simulation.tick() < 150) {
    simulation.step();
  }

  const auto observation = simulation.observe(PlayerId::One);
  const auto remembered = std::ranges::find(observation.known_enemies(), turret,
                                            &ObservedEnemy::id);
  CHECK(remembered != observation.known_enemies().end());
  CHECK(remembered != observation.known_enemies().end() &&
        !remembered->currently_visible);
  const AIInfluenceMap influence{observation};
  const auto plan = CommanderAI{PlayerId::One}.plan(observation);
  const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
  CHECK(tactical != nullptr);
  CHECK(tactical != nullptr &&
        tactical->selected_action == AIAction::CaptureObjective);
  CHECK(tactical != nullptr &&
        influence.cell_at(tactical->command.target).static_danger <
            influence.cell_at(world(850, 400)).static_danger);
  CHECK(tactical != nullptr &&
        std::abs(tactical->command.target.y - world(400, 0).x) >=
            kAIInfluenceCellSize / 2);
  CHECK(tactical != nullptr && valid_score_trace(*tactical));
}

void tactical_layer_stages_reinforcements_on_the_safer_side() {
  auto config = open_config();
  config.starting_ore = {0, 0};
  Simulation simulation{config};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(160, 400)));
  const auto active_one = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(330, 370));
  const auto active_two = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(330, 430));
  const auto reinforcement = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(250, 400));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'520, 400)));
  const auto turret = simulation.spawn_entity(PlayerId::Two, EntityType::Turret,
                                              world(540, 245));
  hold(simulation, PlayerId::One, {active_one, active_two, reinforcement});
  simulation.run(30);
  CHECK(simulation
            .execute_now(Command{.player = PlayerId::One,
                                 .type = CommandType::AttackMove,
                                 .entities = {active_one, active_two},
                                 .target = world(700, 400)})
            .ok);

  const auto observation = simulation.observe(PlayerId::One);
  CHECK(std::ranges::any_of(
      observation.known_enemies(), [turret](const ObservedEnemy& enemy) {
        return enemy.id == turret && enemy.currently_visible;
      }));
  const auto plan = CommanderAI{PlayerId::One}.plan(observation);
  const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
  CHECK(tactical != nullptr);
  CHECK(tactical != nullptr &&
        tactical->selected_action == AIAction::ReinforceFront);
  CHECK(tactical != nullptr &&
        tactical->command.entities == std::vector<EntityId>{reinforcement});
  CHECK(tactical != nullptr && tactical->command.target.y > world(400, 0).x);
  CHECK(tactical != nullptr && valid_score_trace(*tactical));
}

void retreat_command_honors_the_influence_selected_shelter() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(180, 400)));
  const auto defender = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(380, 400));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'020, 400)));
  const auto first = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(540, 350));
  const auto second = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(540, 400));
  const auto third = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(540, 450));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Turret,
                                            world(330, 235)));
  hold(simulation, PlayerId::One, {defender});
  hold(simulation, PlayerId::Two, {first, second, third});
  simulation.run(30);

  const auto plan =
      CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
  CHECK(tactical != nullptr && tactical->selected_action == AIAction::Retreat);
  CHECK(tactical != nullptr && tactical->command.target != Vec2{});
  if (tactical == nullptr) {
    return;
  }
  const auto destination = tactical->command.target;
  CHECK(simulation.execute_now(tactical->command).ok);
  const auto* unit = simulation.find_entity(defender);
  CHECK(unit != nullptr && unit->stance == UnitStance::Defensive);
  CHECK(unit != nullptr && unit->order.type == OrderType::Move);
  CHECK(unit != nullptr && unit->order.target == destination);
}

void micro_layer_focuses_the_high_value_visible_target() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(180, 400)));
  const auto vanguard =
      simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(300, 380));
  const auto skirmisher =
      simulation.spawn_entity(PlayerId::One, EntityType::Skirmisher, world(330, 420));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(1'020, 400)));
  const auto target =
      simulation.spawn_entity(PlayerId::Two, EntityType::Skirmisher, world(520, 400));
  hold(simulation, PlayerId::One, {vanguard, skirmisher});
  hold(simulation, PlayerId::Two, {target});
  simulation.run(12);

  const auto plan = CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  const auto* micro = decision_for(plan, AIDecisionLayer::Micro);
  CHECK(micro != nullptr);
  CHECK(micro != nullptr && micro->selected_action == AIAction::FocusFire);
  CHECK(micro != nullptr && micro->command.type == CommandType::Attack);
  CHECK(micro != nullptr && micro->command.target_entity == target);
  CHECK(micro != nullptr && valid_score_trace(*micro));
}

void micro_layer_kites_while_a_ranged_weapon_cools() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(180, 400)));
  const auto skirmisher =
      simulation.spawn_entity(PlayerId::One, EntityType::Skirmisher, world(400, 400));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(1'020, 400)));
  const auto pursuer =
      simulation.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(520, 400));
  hold(simulation, PlayerId::Two, {pursuer});
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Attack,
                                       .entities = {skirmisher},
                                       .target_entity = pursuer})
            .ok);
  simulation.run(12);

  const auto plan = CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  const auto* micro = decision_for(plan, AIDecisionLayer::Micro);
  CHECK(micro != nullptr);
  CHECK(micro != nullptr && micro->selected_action == AIAction::Kite);
  CHECK(micro != nullptr && micro->command.type == CommandType::Move);
  CHECK(micro != nullptr && micro->command.entities == std::vector<EntityId>{skirmisher});
  CHECK(micro != nullptr && valid_score_trace(*micro));
}

void sheltered_wounded_units_do_not_repeat_retreat_orders() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(180, 400)));
  const auto survivor =
      simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(450, 400));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(1'020, 400)));
  const auto attacker =
      simulation.spawn_entity(PlayerId::Two, EntityType::Skirmisher, world(600, 400));
  hold(simulation, PlayerId::One, {survivor});
  CHECK(simulation.execute_now(Command{.player = PlayerId::Two,
                                       .type = CommandType::Attack,
                                       .entities = {attacker},
                                       .target_entity = survivor})
            .ok);

  while (simulation.tick() < 200) {
    const auto* unit = simulation.find_entity(survivor);
    if (unit == nullptr || unit->hit_points * 10 <= unit->max_hit_points * 3) {
      break;
    }
    simulation.step();
  }
  const auto* wounded = simulation.find_entity(survivor);
  CHECK(wounded != nullptr && wounded->alive());
  CHECK(wounded != nullptr && wounded->hit_points * 10 <= wounded->max_hit_points * 3);
  CHECK(simulation.execute_now(
              Command{.player = PlayerId::Two, .type = CommandType::Stop, .entities = {attacker}})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::Two,
                                       .type = CommandType::Move,
                                       .entities = {attacker},
                                       .target = world(980, 400)})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Retreat,
                                       .entities = {survivor}})
            .ok);
  simulation.run(100);
  while (simulation.tick() % kMicroDecisionCadence != 0) {
    simulation.step();
  }

  const auto* sheltered = simulation.find_entity(survivor);
  CHECK(sheltered != nullptr && sheltered->alive());
  CHECK(sheltered != nullptr &&
        (sheltered->order.type == OrderType::Idle || sheltered->order.type == OrderType::Hold));
  const auto plan = CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  const auto* micro = decision_for(plan, AIDecisionLayer::Micro);
  CHECK(micro == nullptr || micro->selected_action != AIAction::Retreat);
}

void match_age_does_not_force_a_healthy_army_into_a_bad_fight() {
  auto config = open_config();
  config.starting_ore = {0, 0};
  Simulation simulation{config};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(180, 400)));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'420, 400)));
  simulation.run(kLateSearchCommitmentTick);

  const auto defender = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(430, 400));
  const auto first = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(610, 340));
  const auto second = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(610, 380));
  const auto third = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(610, 420));
  const auto fourth = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(610, 460));
  hold(simulation, PlayerId::One, {defender});
  hold(simulation, PlayerId::Two, {first, second, third, fourth});
  do {
    simulation.step();
  } while (!ai_decision_due(AIDecisionLayer::Tactical, simulation.tick()));

  const auto plan =
      CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
  CHECK(tactical != nullptr);
  CHECK(tactical != nullptr && tactical->selected_action == AIAction::Retreat);
  CHECK(tactical != nullptr && valid_score_trace(*tactical));
}

void prolonged_no_contact_commits_a_healthy_force_to_search() {
  auto config = open_config();
  config.starting_ore = {0, 0};
  Simulation simulation{config};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(180, 400)));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command,
                                            world(1'420, 400)));
  const auto searcher = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(430, 400));
  hold(simulation, PlayerId::One, {searcher});
  simulation.run(kLateSearchCommitmentTick);
  while (!ai_decision_due(AIDecisionLayer::Tactical, simulation.tick())) {
    simulation.step();
  }

  const auto plan =
      CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  const auto* tactical = decision_for(plan, AIDecisionLayer::Tactical);
  CHECK(tactical != nullptr);
  CHECK(tactical != nullptr &&
        tactical->selected_action == AIAction::SearchEnemyCommand);
  CHECK(tactical != nullptr &&
        tactical->command.type == CommandType::AttackMove);
  CHECK(tactical != nullptr && tactical->command.target == world(1'420, 400));
  CHECK(tactical != nullptr && valid_score_trace(*tactical));
}

void late_base_assault_is_not_retargeted_to_workers() {
  auto config = open_config();
  config.starting_ore = {0, 0};
  Simulation simulation{config};
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                            world(180, 400)));
  const auto enemy_command = simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'420, 400));
  simulation.run(kLateSearchCommitmentTick);

  const auto attacker = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(1'190, 400));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Worker,
                                            world(1'260, 460)));
  simulation.step();
  CHECK(simulation
            .execute_now(Command{.player = PlayerId::One,
                                 .type = CommandType::Attack,
                                 .entities = {attacker},
                                 .target_entity = enemy_command})
            .ok);
  while (!ai_decision_due(AIDecisionLayer::Micro, simulation.tick())) {
    simulation.step();
  }

  const auto plan =
      CommanderAI{PlayerId::One}.plan(simulation.observe(PlayerId::One));
  CHECK(decision_for(plan, AIDecisionLayer::Micro) == nullptr);
  const auto* committed = simulation.find_entity(attacker);
  CHECK(committed != nullptr && committed->order.type == OrderType::Attack);
  CHECK(committed != nullptr &&
        committed->order.target_entity == enemy_command);
}

}  // namespace

int main() {
  run_test("layer cadences are distinct and human bounded",
           layer_cadences_are_distinct_and_human_bounded);
  run_test("strategic layer scores the opening and worker allocation",
           strategic_layer_scores_the_opening_and_worker_allocation);
  run_test("strategic layer rebuilds combat before an exhausted economy",
           strategic_layer_rebuilds_combat_before_an_exhausted_economy);
  run_test("strategic layer uses one worker to escape total attrition",
           strategic_layer_uses_one_worker_to_escape_total_attrition);
  run_test("tactical layer sends the recovery worker to map control",
           tactical_layer_sends_the_recovery_worker_to_map_control);
  run_test("tactical layer stages reinforcements before a fortified assault",
           tactical_layer_stages_reinforcements_before_a_fortified_assault);
  run_test("tactical layer retreats from visible superior force",
           tactical_layer_retreats_from_visible_superior_force);
  run_test("tactical layer flanks away from a defended side",
           tactical_layer_flanks_away_from_a_defended_side);
  run_test(
      "tactical layer avoids remembered static danger on objective approach",
      tactical_layer_avoids_remembered_static_danger_on_objective_approach);
  run_test("tactical layer stages reinforcements on the safer side",
           tactical_layer_stages_reinforcements_on_the_safer_side);
  run_test("retreat command honors the influence-selected shelter",
           retreat_command_honors_the_influence_selected_shelter);
  run_test("micro layer focuses the high-value visible target",
           micro_layer_focuses_the_high_value_visible_target);
  run_test("micro layer kites while a ranged weapon cools",
           micro_layer_kites_while_a_ranged_weapon_cools);
  run_test("sheltered wounded units do not repeat retreat orders",
           sheltered_wounded_units_do_not_repeat_retreat_orders);
  run_test("match age does not force a healthy army into a bad fight",
           match_age_does_not_force_a_healthy_army_into_a_bad_fight);
  run_test("prolonged no-contact commits a healthy force to search",
           prolonged_no_contact_commits_a_healthy_force_to_search);
  run_test("late base assault is not retargeted to workers",
           late_base_assault_is_not_retargeted_to_workers);

  if (failures != 0) {
    std::cerr << failures << " commander AI check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All commander AI checks passed.\n";
  return EXIT_SUCCESS;
}
