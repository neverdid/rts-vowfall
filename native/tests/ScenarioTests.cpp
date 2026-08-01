#include "ashen/core/Content.hpp"
#include "ashen/core/ObjectiveSystem.hpp"
#include "ashen/core/Scenario.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/Snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using namespace ashen::core;

int failures = 0;

#define CHECK(condition)                                                        \
  do {                                                                          \
    if (!(condition)) {                                                         \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition   \
                << '\n';                                                        \
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

SimulationConfig bridge_config() {
  SimulationConfig config{};
  config.mode = MatchMode::Story;
  config.story_mission = StoryMissionId::BridgeOfNames;
  config.seed_starting_forces = false;
  return config;
}

bool has_scenario_error(const std::vector<ScenarioValidationIssue>& issues,
                        const ScenarioValidationError error) {
  return std::ranges::any_of(issues, [&](const auto& issue) {
    return issue.error == error;
  });
}

void builtin_scenarios_are_valid_and_explicitly_scoped() {
  const auto definitions = builtin_scenarios();
  CHECK(definitions.size() == 3);
  CHECK(validate_scenarios(definitions, builtin_content()).empty());
  CHECK(scenario_catalog_digest() != 0);

  SimulationConfig skirmish{};
  CHECK(find_scenario(skirmish) != nullptr);

  auto bridge = bridge_config();
  CHECK(find_scenario(bridge) != nullptr);

  bridge.story_mission = StoryMissionId::OpenBowl;
  CHECK(find_scenario(bridge) == nullptr);
}

void scenario_validation_rejects_invalid_stage_graphs() {
  const auto* bridge = find_scenario(bridge_config());
  CHECK(bridge != nullptr);
  if (bridge == nullptr) {
    return;
  }
  std::array<MissionObjectiveDefinition, 2> objectives{
      bridge->objectives[0], bridge->objectives[1]};
  ScenarioDefinition definition{"invalid_bridge", MatchMode::Story,
                                StoryMissionId::BridgeOfNames, objectives};

  objectives[1].prerequisite = 99'999;
  CHECK(has_scenario_error(
      validate_scenarios(std::span{&definition, 1}, builtin_content()),
      ScenarioValidationError::MissingObjectivePrerequisite));

  objectives = {bridge->objectives[0], bridge->objectives[1]};
  objectives[0].prerequisite = objectives[1].content_id;
  CHECK(has_scenario_error(
      validate_scenarios(std::span{&definition, 1}, builtin_content()),
      ScenarioValidationError::CyclicObjectivePrerequisite));

  objectives = {bridge->objectives[0], bridge->objectives[1]};
  objectives[1].required = false;
  CHECK(has_scenario_error(
      validate_scenarios(std::span{&definition, 1}, builtin_content()),
      ScenarioValidationError::OptionalPrimaryObjective));

  objectives = {bridge->objectives[0], bridge->objectives[1]};
  objectives[0].required = false;
  CHECK(has_scenario_error(
      validate_scenarios(std::span{&definition, 1}, builtin_content()),
      ScenarioValidationError::RequiredObjectiveDependsOnOptional));
}

void objective_conditions_cover_active_success_and_failure() {
  ObjectiveSystem objectives;
  SimulationConfig skirmish{};
  objectives.reset(skirmish);
  CHECK(objectives.states().size() == 1);
  CHECK(objectives.states().front().status == MissionObjectiveStatus::Active);

  MissionObjectiveContext context{
      .tick = 1,
      .command_seen = {true, true},
      .command_alive = {true, true},
  };
  CHECK(objectives.evaluate(context).empty());
  context.command_alive[1] = false;
  const auto succeeded = objectives.evaluate(context);
  CHECK(succeeded.size() == 1);
  CHECK(succeeded.front().primary);
  CHECK(succeeded.front().current == MissionObjectiveStatus::Succeeded);

  objectives.reset(skirmish);
  context.command_alive = {false, true};
  const auto failed = objectives.evaluate(context);
  CHECK(failed.size() == 1);
  CHECK(failed.front().current == MissionObjectiveStatus::Failed);

  objectives.reset(skirmish);
  context.command_alive = {false, false};
  const auto simultaneous = objectives.evaluate(context);
  CHECK(simultaneous.size() == 1);
  CHECK(simultaneous.front().current == MissionObjectiveStatus::Failed);

  objectives.reset(bridge_config());
  auto bridge = objectives.primary_view(0);
  CHECK(bridge.has_value());
  CHECK(bridge.has_value() &&
        bridge->content_id == content_id::BridgeApproachesObjective);
  CHECK(objectives.states().size() == 2);
  CHECK(objectives.states()[0].status == MissionObjectiveStatus::Active);
  CHECK(objectives.states()[1].status == MissionObjectiveStatus::Inactive);
  context.command_alive = {true, true};
  context.tick = 10;
  context.objective_count = 2;
  context.player_one_controlled_objectives = 2;
  const auto secured = objectives.evaluate(context);
  CHECK(secured.size() == 2);
  CHECK(secured[0].content_id == content_id::BridgeApproachesObjective);
  CHECK(secured[0].current == MissionObjectiveStatus::Succeeded);
  CHECK(secured[1].content_id == content_id::BridgeObjective);
  CHECK(secured[1].previous == MissionObjectiveStatus::Inactive);
  CHECK(secured[1].current == MissionObjectiveStatus::Active);
  CHECK(objectives.outcome_matches(MatchMode::Story, MatchStatus::Playing,
                                   std::nullopt));
  ObjectiveSystem later_activation;
  later_activation.reset(bridge_config());
  auto later_context = context;
  ++later_context.tick;
  CHECK(later_activation.evaluate(later_context).size() == 2);
  CHECK(later_activation.state_hash() != objectives.state_hash());
  bridge = objectives.primary_view(context.tick);
  CHECK(bridge.has_value() && bridge->stage_index == 2);
  CHECK(bridge.has_value() &&
        bridge->target_tick == context.tick + 60 * kTicksPerSecond);

  context.tick += 60 * kTicksPerSecond - 1;
  CHECK(objectives.evaluate(context).empty());
  ++context.tick;
  const auto held = objectives.evaluate(context);
  CHECK(held.size() == 1);
  CHECK(held.front().current == MissionObjectiveStatus::Succeeded);
  CHECK(objectives.outcome_matches(MatchMode::Story, MatchStatus::Won,
                                   PlayerId::One));

  objectives.reset(bridge_config());
  context = {.tick = 1,
             .command_seen = {true, true},
             .command_alive = {false, true}};
  const auto approach_failed = objectives.evaluate(context);
  CHECK(approach_failed.size() == 1);
  CHECK(approach_failed.front().required);
  CHECK(approach_failed.front().content_id ==
        content_id::BridgeApproachesObjective);
  CHECK(objectives.outcome_matches(MatchMode::Story, MatchStatus::Lost,
                                   PlayerId::Two));
  CHECK(!objectives.outcome_matches(MatchMode::Story, MatchStatus::Playing,
                                    std::nullopt));
}

void bridge_fixture_restores_and_resolves_deterministically() {
  Simulation original{bridge_config()};
  static_cast<void>(original.spawn_entity(PlayerId::One, EntityType::Command,
                                          world(300, 700)));
  static_cast<void>(original.spawn_entity(PlayerId::Two, EntityType::Command,
                                          world(2'100, 700)));
  const auto north = original.add_control_point(world(500, 500));
  const auto south = original.add_control_point(world(700, 500));
  static_cast<void>(north);
  static_cast<void>(south);
  static_cast<void>(original.spawn_entity(PlayerId::One, EntityType::Worker,
                                          world(500, 500)));
  static_cast<void>(original.spawn_entity(PlayerId::One, EntityType::Worker,
                                          world(700, 500)));

  while (original.tick() < 500 &&
         original.primary_mission_objective()->content_id ==
             content_id::BridgeApproachesObjective) {
    original.step();
  }
  CHECK(original.status() == MatchStatus::Playing);
  const auto hold = original.primary_mission_objective();
  CHECK(hold.has_value());
  CHECK(hold.has_value() && hold->content_id == content_id::BridgeObjective);
  CHECK(hold.has_value() && hold->status == MissionObjectiveStatus::Active);

  std::vector<MissionObjectiveChangedEvent> mission_events;
  std::vector<Tick> mission_event_ticks;
  for (const auto& event : original.events()) {
    if (event_type(event) == SimulationEventType::MissionObjectiveChanged) {
      mission_events.push_back(
          std::get<MissionObjectiveChangedEvent>(event.payload));
      mission_event_ticks.push_back(event.tick);
    }
  }
  CHECK(mission_events.size() == 2);
  CHECK(mission_events.size() == 2 &&
        mission_events[0].objective ==
            content_id::BridgeApproachesObjective &&
        mission_events[0].current == MissionObjectiveStatus::Succeeded);
  CHECK(mission_events.size() == 2 &&
        mission_events[1].objective == content_id::BridgeObjective &&
        mission_events[1].current == MissionObjectiveStatus::Active);
  CHECK(mission_event_ticks.size() == 2 &&
        mission_event_ticks[0] == mission_event_ticks[1]);

  const auto target_tick = hold.has_value() ? hold->target_tick : original.tick();
  if (target_tick > original.tick() + 1) {
    original.run(target_tick - original.tick() - 1);
  }
  CHECK(original.status() == MatchStatus::Playing);

  const auto checkpoint = save_snapshot_v1(original);
  auto load = load_snapshot_v1(checkpoint);
  CHECK(static_cast<bool>(load));
  CHECK(load.simulation != nullptr);
  if (load.simulation == nullptr) {
    return;
  }
  Simulation& restored = *load.simulation;
  CHECK(restored.state_hash() == original.state_hash());
  CHECK(restored.primary_mission_objective()->status ==
        MissionObjectiveStatus::Active);
  CHECK(restored.primary_mission_objective()->target_tick == target_tick);

  original.step();
  restored.step();
  CHECK(original.tick() == target_tick);
  CHECK(original.status() == MatchStatus::Won);
  CHECK(original.winner() == PlayerId::One);
  CHECK(original.state_hash() == restored.state_hash());
  CHECK(original.events() == restored.events());
  CHECK(original.primary_mission_objective()->status ==
        MissionObjectiveStatus::Succeeded);
  CHECK(!original.events().empty());
  CHECK(event_type(original.events().back()) ==
        SimulationEventType::MissionObjectiveChanged);

  const auto terminal_checkpoint = save_snapshot_v1(original);
  const auto terminal_load = load_snapshot_v1(terminal_checkpoint);
  CHECK(static_cast<bool>(terminal_load));
  CHECK(terminal_load.simulation != nullptr);
  CHECK(terminal_load.simulation != nullptr &&
        terminal_load.simulation->state_hash() == original.state_hash());
  CHECK(terminal_load.simulation != nullptr &&
        terminal_load.simulation->primary_mission_objective()->status ==
            MissionObjectiveStatus::Succeeded);
}

void required_bridge_stage_failure_ends_the_mission() {
  Simulation simulation{bridge_config()};
  const auto player_command = simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(500, 100)));
  const auto attacker = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(250, 100));
  CHECK(simulation.execute_now(Command{
            .player = PlayerId::Two,
            .type = CommandType::Attack,
            .entities = {attacker},
            .target_entity = player_command,
        }).ok);

  simulation.run(1'200);
  CHECK(simulation.status() == MatchStatus::Lost);
  CHECK(simulation.winner() == PlayerId::Two);
  CHECK(simulation.find_entity(player_command) == nullptr);
  const auto objective = simulation.primary_mission_objective();
  CHECK(objective.has_value());
  CHECK(objective.has_value() &&
        objective->content_id == content_id::BridgeApproachesObjective);
  CHECK(objective.has_value() &&
        objective->status == MissionObjectiveStatus::Failed);
}

}  // namespace

int main() {
  run_test("built-in scenarios are valid and explicitly scoped",
           builtin_scenarios_are_valid_and_explicitly_scoped);
  run_test("scenario validation rejects invalid stage graphs",
           scenario_validation_rejects_invalid_stage_graphs);
  run_test("objective conditions cover active success and failure",
           objective_conditions_cover_active_success_and_failure);
  run_test("Bridge fixture restores and resolves deterministically",
           bridge_fixture_restores_and_resolves_deterministically);
  run_test("required Bridge stage failure ends the mission",
           required_bridge_stage_failure_ends_the_mission);

  if (failures != 0) {
    std::cerr << failures << " scenario check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All scenario/objective checks passed.\n";
  return EXIT_SUCCESS;
}
