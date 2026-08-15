#include "ashen/core/AIStrategyState.hpp"
#include "ashen/core/Catalog.hpp"
#include "ashen/core/Content.hpp"
#include "ashen/core/ResolveSystem.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/SpatialGrid.hpp"
#include "ashen/core/SystemPipeline.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <string_view>
#include <vector>

namespace {

using namespace ashen::core;

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition   \
                << "\n";                                                       \
      ++failures;                                                               \
    }                                                                           \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name
            << '\n';
}

[[nodiscard]] Simulation sandbox(
    const FactionId one = FactionId::Compact,
    const FactionId two = FactionId::Ascendancy) {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.player_one_faction = one;
  config.player_two_faction = two;
  config.map_size = world(1'200, 800);
  config.navigation_obstacles.clear();
  config.starting_ore = {2'000, 2'000};
  return Simulation{config};
}

[[nodiscard]] bool has_error(
    const std::vector<ContentValidationIssue>& issues,
    const ContentValidationError error) {
  return std::ranges::any_of(
      issues, [error](const ContentValidationIssue& issue) {
        return issue.error == error;
      });
}

void target_system_pipeline_is_complete_and_ordered() {
  CHECK(kTargetSystemPipeline.size() == 21);
  for (std::size_t index = 0; index < kTargetSystemPipeline.size();
       ++index) {
    CHECK(static_cast<std::size_t>(kTargetSystemPipeline[index].phase) ==
          index);
    CHECK(!kTargetSystemPipeline[index].development_name.empty());
  }
  CHECK(kTargetSystemPipeline.front().phase ==
        SystemPhase::CommandValidation);
  CHECK(kTargetSystemPipeline.back().phase == SystemPhase::StateHashing);
}

void builtin_content_is_valid_and_repository_backed() {
  const auto& registry = builtin_content();
  CHECK(validate_content(registry).empty());
  CHECK(registry.factions.size() == 3);
  CHECK(registry.units.size() == 9);
  CHECK(registry.structures.size() == 9);
  CHECK(registry.supply_nodes.size() == 3);
  CHECK(registry.research.size() == kResearchCount);
  CHECK(find_vow_content(registry, kBridgeOpenVow) != nullptr);
  CHECK(find_faction_power_ability(registry, FactionId::Compact) !=
        nullptr);
  CHECK(faction_presentation_key(FactionId::Compact) ==
        "vowfall.faction.compact");
  CHECK(faction_presentation_key(FactionId::Ascendancy) ==
        "vowfall.faction.ascendancy");
}

void content_validation_rejects_invalid_definitions() {
  {
    auto registry = builtin_content();
    registry.abilities.front().metadata.stable_id =
        registry.factions.front().metadata.stable_id;
    CHECK(has_error(validate_content(registry),
                    ContentValidationError::DuplicateStableId));
  }
  {
    auto registry = builtin_content();
    registry.abilities.front().projectile = 99'999;
    CHECK(has_error(validate_content(registry),
                    ContentValidationError::MissingContentReference));
  }
  {
    auto registry = builtin_content();
    registry.research.front().prerequisite =
        static_cast<ResearchId>(99);
    CHECK(has_error(validate_content(registry),
                    ContentValidationError::InvalidPrerequisite));
  }
  {
    auto registry = builtin_content();
    registry.units.front().cost = -1;
    CHECK(has_error(validate_content(registry),
                    ContentValidationError::NegativeCost));
  }
  {
    auto registry = builtin_content();
    registry.abilities.front().windup_ticks = -1;
    CHECK(has_error(validate_content(registry),
                    ContentValidationError::InvalidDuration));
  }
  {
    auto registry = builtin_content();
    registry.units.front().capabilities |=
        command_capability(CommandType::Train);
    CHECK(has_error(
        validate_content(registry),
        ContentValidationError::UnsupportedCommandCapability));
  }
  {
    auto registry = builtin_content();
    registry.units.front().faction = static_cast<FactionId>(99);
    CHECK(has_error(validate_content(registry),
                    ContentValidationError::MissingFactionReference));
  }
  {
    auto registry = builtin_content();
    registry.units.front().metadata.presentation_key = "Invalid Key";
    CHECK(has_error(validate_content(registry),
                    ContentValidationError::InvalidPresentationKey));
  }
  {
    auto registry = builtin_content();
    registry.projectiles.front().speed_per_tick = 0;
    CHECK(has_error(
        validate_content(registry),
        ContentValidationError::InvalidDeterministicValue));
  }
  {
    auto registry = builtin_content();
    registry.objectives.front().capture_radius = -1;
    CHECK(has_error(
        validate_content(registry),
        ContentValidationError::InvalidDeterministicValue));
  }
  {
    auto registry = builtin_content();
    registry.research[0].prerequisite = ResearchId::TemperedOaths;
    registry.research[1].prerequisite = ResearchId::TierTwo;
    CHECK(has_error(
        validate_content(registry),
        ContentValidationError::CyclicResearchPrerequisite));
  }
}

void faction_identity_is_independent_of_slot_and_control_source() {
  SimulationConfig swapped{};
  swapped.seed_starting_forces = false;
  swapped.player_one_faction = FactionId::Ascendancy;
  swapped.player_two_faction = FactionId::Compact;
  swapped.commander_players = {true, false};
  Simulation simulation{swapped};
  const auto ai_entity = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(200, 200));
  const auto human_entity = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(1'000, 600));
  CHECK(simulation.find_entity(ai_entity)->faction ==
        FactionId::Ascendancy);
  CHECK(simulation.find_entity(human_entity)->faction ==
        FactionId::Compact);
  CHECK(entity_definition(simulation.find_entity(ai_entity)->faction,
                          EntityType::Vanguard)
            .label == "Crowned Bulwark");
  CHECK(entity_definition(simulation.find_entity(human_entity)->faction,
                          EntityType::Vanguard)
            .label == "Pikeguard");

  SimulationConfig mirror = swapped;
  mirror.player_two_faction = FactionId::Ascendancy;
  mirror.commander_players = {false, true};
  Simulation mirror_match{mirror};
  const auto one = mirror_match.spawn_entity(
      PlayerId::One, EntityType::Worker, world(200, 200));
  const auto two = mirror_match.spawn_entity(
      PlayerId::Two, EntityType::Worker, world(1'000, 600));
  CHECK(mirror_match.find_entity(one)->faction ==
        FactionId::Ascendancy);
  CHECK(mirror_match.find_entity(two)->faction ==
        FactionId::Ascendancy);
  CHECK(faction_presentation_key(mirror_match.find_entity(one)->faction) ==
        faction_presentation_key(mirror_match.find_entity(two)->faction));

  Simulation duplicate{swapped};
  static_cast<void>(duplicate.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(200, 200)));
  static_cast<void>(duplicate.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(1'000, 600)));
  CHECK(simulation.state_hash() == duplicate.state_hash());
}

void indexed_entity_lookup_survives_deletion_without_reuse() {
  auto simulation = sandbox();
  const auto attacker = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(200, 200));
  const auto victim = simulation.spawn_entity(
      PlayerId::Two, EntityType::Worker, world(220, 200));
  const auto survivor = simulation.spawn_entity(
      PlayerId::One, EntityType::Worker, world(100, 700));
  CHECK(simulation.execute_now(Command{
            .player = PlayerId::One,
            .type = CommandType::Attack,
            .entities = {attacker},
            .target_entity = victim})
            .ok);
  simulation.run(400);
  CHECK(simulation.find_entity(victim) == nullptr);
  CHECK(simulation.find_entity(attacker) != nullptr);
  CHECK(simulation.find_entity(survivor) != nullptr);
  CHECK(simulation.find_entity(EntityId{0}) == nullptr);
  CHECK(simulation.find_entity(EntityId{99'999}) == nullptr);
  const auto replacement = simulation.spawn_entity(
      PlayerId::Two, EntityType::Worker, world(900, 700));
  CHECK(replacement.value > survivor.value);
  CHECK(replacement != victim);
}

void spatial_queries_are_exact_stable_and_rebuildable() {
  std::vector<Entity> entities(4);
  entities[0].id = EntityId{9};
  entities[0].position = world(200, 200);
  entities[0].hit_points = 1;
  entities[1].id = EntityId{2};
  entities[1].position = world(100, 200);
  entities[1].hit_points = 1;
  entities[2].id = EntityId{7};
  entities[2].position = world(300, 200);
  entities[2].hit_points = 1;
  entities[3].id = EntityId{4};
  entities[3].position = world(301, 200);
  entities[3].hit_points = 1;

  SpatialGrid grid;
  grid.reset(world(600, 400), world(100, 0).x);
  grid.rebuild(entities);
  const auto hits =
      grid.query_radius(world(200, 200), world(100, 0).x);
  CHECK(hits.size() == 3);
  CHECK(hits.size() == 3 && hits[0].id == EntityId{2});
  CHECK(hits.size() == 3 && hits[1].id == EntityId{7});
  CHECK(hits.size() == 3 && hits[2].id == EntityId{9});
  CHECK(std::ranges::is_sorted(hits, {}, [](const SpatialQueryHit& hit) {
    return hit.id.value;
  }));

  entities[1].hit_points = 0;
  grid.rebuild(entities);
  const auto rebuilt =
      grid.query_radius(world(200, 200), world(100, 0).x);
  CHECK(std::ranges::none_of(rebuilt, [](const SpatialQueryHit& hit) {
    return hit.id == EntityId{2};
  }));
}

void events_are_ordered_emitted_and_replayable() {
  auto first = sandbox();
  auto second = sandbox();
  const auto first_attacker = first.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(200, 200));
  const auto first_victim = first.spawn_entity(
      PlayerId::Two, EntityType::Worker, world(220, 200));
  const auto second_attacker = second.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(200, 200));
  const auto second_victim = second.spawn_entity(
      PlayerId::Two, EntityType::Worker, world(220, 200));
  CHECK(first.execute_now(Command{
            .player = PlayerId::One,
            .type = CommandType::Attack,
            .entities = {first_attacker},
            .target_entity = first_victim})
            .ok);
  CHECK(second.execute_now(Command{
            .player = PlayerId::One,
            .type = CommandType::Attack,
            .entities = {second_attacker},
            .target_entity = second_victim})
            .ok);
  first.run(900);
  second.run(900);

  CHECK(first.events() == second.events());
  CHECK(first.event_digest() == second.event_digest());
  CHECK(first.state_hash() == second.state_hash());
  for (std::size_t index = 0; index < first.events().size(); ++index) {
    CHECK(first.events()[index].id.value == index + 1);
  }
  const auto killed = std::ranges::find_if(
      first.events(), [](const SimulationEvent& event) {
        return event_type(event) == SimulationEventType::UnitKilled;
      });
  const auto destroyed = std::ranges::find_if(
      first.events(), [](const SimulationEvent& event) {
        return event_type(event) == SimulationEventType::EntityDestroyed;
      });
  const auto incapacitated = std::ranges::find_if(
      first.events(), [](const SimulationEvent& event) {
        return event_type(event) ==
                   SimulationEventType::CasualtyStateChanged &&
               std::get<CasualtyStateChangedEvent>(event.payload).current ==
                   CasualtyState::Incapacitated;
      });
  CHECK(killed != first.events().end());
  CHECK(destroyed != first.events().end());
  CHECK(incapacitated != first.events().end());
  CHECK(killed != first.events().end() &&
        destroyed != first.events().end() &&
        incapacitated != first.events().end() &&
        incapacitated->id < destroyed->id && destroyed->id < killed->id);
  const auto before_read = first.state_hash();
  static_cast<void>(first.events().size());
  CHECK(first.state_hash() == before_read);
}

void objective_and_ability_events_come_from_authoritative_transitions() {
  auto simulation = sandbox();
  const auto point = simulation.add_control_point(world(400, 400));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(400, 400)));
  simulation.run(180);
  CHECK(std::ranges::any_of(
      simulation.events(), [point](const SimulationEvent& event) {
        if (event_type(event) != SimulationEventType::ObjectiveCaptured) {
          return false;
        }
        return std::get<ObjectiveCapturedEvent>(event.payload).objective ==
               point;
      }));
  CHECK(simulation.execute_now(Command{
            .player = PlayerId::One,
            .type = CommandType::ActivatePower})
            .ok);
  CHECK(std::ranges::any_of(
      simulation.events(), [](const SimulationEvent& event) {
        return event_type(event) ==
               SimulationEventType::AbilityStarted;
      }));
}

void resolve_thresholds_are_named_and_emit_transitions() {
  CHECK(resolve_state_from_value(100) == ResolveState::Steady);
  CHECK(resolve_state_from_value(80) == ResolveState::Steady);
  CHECK(resolve_state_from_value(79) == ResolveState::Strained);
  CHECK(resolve_state_from_value(60) == ResolveState::Strained);
  CHECK(resolve_state_from_value(59) == ResolveState::Wavering);
  CHECK(resolve_state_from_value(40) == ResolveState::Wavering);
  CHECK(resolve_state_from_value(39) == ResolveState::Broken);

  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Worker, world(400, 400)));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(420, 400)));
  simulation.step();
  CHECK(std::ranges::any_of(
      simulation.events(), [](const SimulationEvent& event) {
        return event_type(event) ==
               SimulationEventType::ResolveThresholdChanged;
      }));
}

void minimal_vow_lifecycle_is_authoritative() {
  const auto make = [](Simulation& simulation, const PlayerId player) {
    return simulation.execute_now(Command{.player = player,
                                          .type = CommandType::MakeVow,
                                          .vow = kBridgeOpenVow});
  };
  {
    auto simulation = sandbox();
    CHECK(make(simulation, PlayerId::One).ok);
    CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                         .type = CommandType::KeepVow,
                                         .vow = kBridgeOpenVow})
              .ok);
    CHECK(simulation.vows().front().resolution ==
          VowResolution::Kept);
  }
  {
    auto simulation = sandbox();
    CHECK(make(simulation, PlayerId::One).ok);
    CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                         .type = CommandType::BreakVow,
                                         .vow = kBridgeOpenVow})
              .ok);
    CHECK(simulation.vows().front().resolution ==
          VowResolution::Broken);
  }
  {
    auto first = sandbox();
    auto second = sandbox();
    CHECK(make(first, PlayerId::One).ok);
    CHECK(make(second, PlayerId::One).ok);
    CHECK(!first.execute_now(Command{.player = PlayerId::One,
                                     .type = CommandType::AmendVow,
                                     .vow = kBridgeOpenVow})
               .ok);
    CHECK(!second.execute_now(Command{.player = PlayerId::One,
                                      .type = CommandType::AmendVow,
                                      .vow = kBridgeOpenVow})
               .ok);
    CHECK(first.execute_now(Command{.player = PlayerId::Two,
                                    .type = CommandType::AmendVow,
                                    .vow = kBridgeOpenVow})
              .ok);
    CHECK(second.execute_now(Command{.player = PlayerId::Two,
                                     .type = CommandType::AmendVow,
                                     .vow = kBridgeOpenVow})
              .ok);
    CHECK(first.vows().front().resolution == VowResolution::Amended);
    CHECK(first.vows().front().participating_affected_player ==
          PlayerId::Two);
    CHECK(first.events() == second.events());
    CHECK(first.state_hash() == second.state_hash());
  }
}

void ai_strategy_state_is_deterministic_and_fog_limited() {
  auto first = sandbox(FactionId::Ascendancy, FactionId::Compact);
  auto second = sandbox(FactionId::Ascendancy, FactionId::Compact);
  static_cast<void>(first.spawn_entity(
      PlayerId::One, EntityType::Command, world(150, 150)));
  static_cast<void>(second.spawn_entity(
      PlayerId::One, EntityType::Command, world(150, 150)));
  static_cast<void>(first.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(220, 150)));
  static_cast<void>(second.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(220, 150)));
  static_cast<void>(first.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(1'100, 700)));
  static_cast<void>(second.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(900, 700)));
  first.step();
  second.step();
  CHECK(first.observe(PlayerId::One).hash() ==
        second.observe(PlayerId::One).hash());

  CommanderAI first_ai{PlayerId::One};
  CommanderAI second_ai{PlayerId::One};
  static_cast<void>(first_ai.update(first.observe(PlayerId::One)));
  static_cast<void>(second_ai.update(second.observe(PlayerId::One)));
  CHECK(first_ai.strategy_state() == second_ai.strategy_state());
  CHECK(ai_strategy_state_hash(first_ai.strategy_state()) ==
        ai_strategy_state_hash(second_ai.strategy_state()));
  CHECK(first_ai.strategy_state().opening ==
        AIOpeningPlan::PreparedAbsolution);
  CHECK(first_ai.state_hash() == second_ai.state_hash());
}

}  // namespace

int main() {
  run_test("target system pipeline is complete and ordered",
           target_system_pipeline_is_complete_and_ordered);
  run_test("built-in content is valid and repository-backed",
           builtin_content_is_valid_and_repository_backed);
  run_test("content validation rejects invalid definitions",
           content_validation_rejects_invalid_definitions);
  run_test("faction identity is independent of slot and control source",
           faction_identity_is_independent_of_slot_and_control_source);
  run_test("indexed entity lookup survives deletion without reuse",
           indexed_entity_lookup_survives_deletion_without_reuse);
  run_test("spatial queries are exact, stable, and rebuildable",
           spatial_queries_are_exact_stable_and_rebuildable);
  run_test("events are ordered, emitted, and replayable",
           events_are_ordered_emitted_and_replayable);
  run_test("objective and ability events are authoritative",
           objective_and_ability_events_come_from_authoritative_transitions);
  run_test("resolve thresholds are named and emit transitions",
           resolve_thresholds_are_named_and_emit_transitions);
  run_test("minimal Vow lifecycle is authoritative",
           minimal_vow_lifecycle_is_authoritative);
  run_test("AI strategy state is deterministic and fog-limited",
           ai_strategy_state_is_deterministic_and_fog_limited);

  if (failures != 0) {
    std::cerr << failures << " foundation check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All Vowfall foundation checks passed.\n";
  return EXIT_SUCCESS;
}
