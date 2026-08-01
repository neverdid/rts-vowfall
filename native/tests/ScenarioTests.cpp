#include "ashen/core/Content.hpp"
#include "ashen/core/ObjectiveSystem.hpp"
#include "ashen/core/Scenario.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/Snapshot.hpp"

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
  const auto bridge = objectives.primary_view(0);
  CHECK(bridge.has_value());
  CHECK(bridge.has_value() && bridge->target_tick == 60 * kTicksPerSecond);
  context.command_alive = {true, true};
  context.tick = 60 * kTicksPerSecond - 1;
  CHECK(objectives.evaluate(context).empty());
  ++context.tick;
  const auto held = objectives.evaluate(context);
  CHECK(held.size() == 1);
  CHECK(held.front().current == MissionObjectiveStatus::Succeeded);
}

void bridge_fixture_restores_and_resolves_deterministically() {
  Simulation original{bridge_config()};
  static_cast<void>(original.spawn_entity(PlayerId::One, EntityType::Command,
                                          world(300, 700)));
  static_cast<void>(original.spawn_entity(PlayerId::Two, EntityType::Command,
                                          world(2'100, 700)));
  original.run(60 * kTicksPerSecond - 1);
  CHECK(original.status() == MatchStatus::Playing);
  CHECK(original.primary_mission_objective()->status ==
        MissionObjectiveStatus::Active);

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

  original.step();
  restored.step();
  CHECK(original.tick() == 60 * kTicksPerSecond);
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

}  // namespace

int main() {
  run_test("built-in scenarios are valid and explicitly scoped",
           builtin_scenarios_are_valid_and_explicitly_scoped);
  run_test("objective conditions cover active success and failure",
           objective_conditions_cover_active_success_and_failure);
  run_test("Bridge fixture restores and resolves deterministically",
           bridge_fixture_restores_and_resolves_deterministically);

  if (failures != 0) {
    std::cerr << failures << " scenario check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All scenario/objective checks passed.\n";
  return EXIT_SUCCESS;
}
