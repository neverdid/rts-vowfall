#include "ashen/core/CasualtySystem.hpp"
#include "ashen/core/Replay.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/Snapshot.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <string_view>

namespace {

using namespace ashen::core;

int failures = 0;

#define CHECK(condition)                                                      \
  do {                                                                        \
    if (!(condition)) {                                                       \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition \
                << '\n';                                                      \
      ++failures;                                                             \
    }                                                                         \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name
            << '\n';
}

[[nodiscard]] Entity test_unit(const std::uint32_t entity_id,
                               const std::uint32_t identity_id,
                               const Vec2 position) {
  Entity entity{};
  entity.id = EntityId{entity_id};
  entity.identity = UnitIdentityId{identity_id};
  entity.owner = PlayerId::One;
  entity.faction = FactionId::Compact;
  entity.type = EntityType::Vanguard;
  entity.kind = EntityKind::Unit;
  entity.position = position;
  entity.hit_points = 100;
  entity.max_hit_points = 100;
  return entity;
}

[[nodiscard]] SimulationConfig empty_config() {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.starting_ore = {2'000, 2'000};
  return config;
}

struct BattleFixture {
  Simulation simulation{empty_config()};
  EntityId victim{};
  UnitIdentityId victim_identity{};

  BattleFixture() {
    static_cast<void>(simulation.spawn_entity(
        PlayerId::One, EntityType::Command, world(100, 100)));
    static_cast<void>(simulation.spawn_entity(
        PlayerId::Two, EntityType::Command, world(1'300, 100)));
    victim = simulation.spawn_entity(
        PlayerId::One, EntityType::Vanguard, world(500, 100));
    victim_identity = simulation.find_entity(victim)->identity;
    for (std::int32_t index = 0; index < 8; ++index) {
      static_cast<void>(simulation.spawn_entity(
          PlayerId::Two, EntityType::Turret,
          world(545 + index, 90 + index * 3)));
    }
  }

  void run_to_death() {
    const auto deadline = simulation.tick() + 240;
    while (simulation.find_entity(victim) != nullptr &&
           simulation.tick() < deadline) {
      simulation.step();
    }
    CHECK(simulation.find_entity(victim) == nullptr);
  }
};

void system_owns_stable_transition_history() {
  CasualtySystem casualties;
  auto first = test_unit(7, 1, world(120, 240));
  auto second = test_unit(8, 2, world(160, 240));
  CHECK(casualties.register_unit(first, 4));
  CHECK(casualties.mark_wounded(first, EntityId{90}, 9));
  CHECK(!casualties.mark_wounded(first, EntityId{91}, 9));
  CHECK(casualties.mark_dead(first, EntityId{92}, 10));
  CHECK(casualties.register_unit(second, 11));

  CHECK(casualties.records().size() == 2);
  CHECK(casualties.transitions().size() == 2);
  CHECK((casualties.transitions()[0] ==
         CasualtyTransition{UnitIdentityId{1}, EntityId{7},
                            CasualtyState::Active, CasualtyState::Wounded,
                            9, EntityId{90}, world(120, 240)}));
  CHECK((casualties.transitions()[1] ==
         CasualtyTransition{UnitIdentityId{1}, EntityId{7},
                            CasualtyState::Wounded, CasualtyState::Dead,
                            10, EntityId{92}, world(120, 240)}));
  const auto* record = casualties.find(UnitIdentityId{1});
  CHECK(record != nullptr);
  CHECK(record != nullptr && record->state == CasualtyState::Dead);
  CHECK(record != nullptr && record->injuries == 1);
  CHECK(casualties.derivation_matches(
      std::span<const Entity>{&second, 1}, 3, 11));
}

void spawn_identity_is_unit_only_and_observation_safe() {
  Simulation simulation{empty_config()};
  const auto command = simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100));
  const auto worker = simulation.spawn_entity(
      PlayerId::One, EntityType::Worker, world(180, 100));
  const auto enemy = simulation.spawn_entity(
      PlayerId::Two, EntityType::Vanguard, world(220, 100));

  CHECK(!simulation.find_entity(command)->identity);
  CHECK(simulation.find_entity(worker)->identity == UnitIdentityId{1});
  CHECK(simulation.find_entity(enemy)->identity == UnitIdentityId{2});
  CHECK(simulation.casualties().size() == 2);

  const auto observation = simulation.observe(PlayerId::One);
  const auto observed_worker = std::ranges::find(
      observation.owned_entities(), worker, &Entity::id);
  CHECK(observed_worker != observation.owned_entities().end());
  CHECK(observed_worker != observation.owned_entities().end() &&
        observed_worker->identity == UnitIdentityId{1});
  CHECK(observed_worker != observation.owned_entities().end() &&
        observed_worker->casualty_state == CasualtyState::Active);

  const auto spawn_event = std::ranges::find_if(
      simulation.events(), [worker](const SimulationEvent& event) {
        return event_type(event) == SimulationEventType::EntitySpawned &&
               std::get<EntitySpawnedEvent>(event.payload).entity == worker;
      });
  CHECK(spawn_event != simulation.events().end());
  CHECK(spawn_event != simulation.events().end() &&
        std::get<EntitySpawnedEvent>(spawn_event->payload).identity ==
            UnitIdentityId{1});
}

void death_retains_identity_and_orders_transitions() {
  BattleFixture fixture;
  fixture.run_to_death();

  const auto* record = fixture.simulation.find_casualty(
      fixture.victim_identity);
  CHECK(record != nullptr);
  CHECK(record != nullptr && record->last_entity == fixture.victim);
  CHECK(record != nullptr && record->state == CasualtyState::Dead);
  CHECK(record != nullptr && record->injuries == 1);

  const auto history = fixture.simulation.casualty_history();
  const auto first = std::ranges::find(
      history, fixture.victim_identity, &CasualtyTransition::identity);
  CHECK(first != history.end());
  CHECK(first != history.end() && first->previous == CasualtyState::Active);
  CHECK(first != history.end() && first->current == CasualtyState::Wounded);
  CHECK(first != history.end() && first + 1 != history.end());
  CHECK(first != history.end() && first + 1 != history.end() &&
        (first + 1)->identity == fixture.victim_identity);
  CHECK(first != history.end() && first + 1 != history.end() &&
        (first + 1)->previous == CasualtyState::Wounded);
  CHECK(first != history.end() && first + 1 != history.end() &&
        (first + 1)->current == CasualtyState::Dead);

  const auto killed = std::ranges::find_if(
      fixture.simulation.events(), [&](const SimulationEvent& event) {
        return event_type(event) == SimulationEventType::UnitKilled &&
               std::get<UnitKilledEvent>(event.payload).identity ==
                   fixture.victim_identity;
      });
  CHECK(killed != fixture.simulation.events().end());
  CHECK(killed != fixture.simulation.events().end() &&
        std::get<UnitKilledEvent>(killed->payload).previous ==
            CasualtyState::Wounded);
}

void snapshot_and_replay_preserve_casualty_ledger() {
  BattleFixture fixture;
  ReplayRecorder recorder{fixture.simulation};
  fixture.run_to_death();
  recorder.capture_checkpoint(fixture.simulation);

  const auto snapshot = load_snapshot_v1(
      save_snapshot_v1(fixture.simulation));
  CHECK(snapshot);
  CHECK(snapshot.simulation != nullptr);
  CHECK(snapshot.simulation != nullptr &&
        snapshot.simulation->state_hash() == fixture.simulation.state_hash());
  CHECK(snapshot.simulation != nullptr &&
        std::ranges::equal(snapshot.simulation->casualties(),
                           fixture.simulation.casualties()));
  CHECK(snapshot.simulation != nullptr &&
        std::ranges::equal(snapshot.simulation->casualty_history(),
                           fixture.simulation.casualty_history()));

  const auto verification = verify_replay_v1(
      save_replay_v1(recorder.finish(fixture.simulation)));
  CHECK(verification);
  CHECK(verification.simulation != nullptr);
  CHECK(verification.simulation != nullptr &&
        std::ranges::equal(verification.simulation->casualties(),
                           fixture.simulation.casualties()));
  CHECK(verification.simulation != nullptr &&
        std::ranges::equal(verification.simulation->casualty_history(),
                           fixture.simulation.casualty_history()));
}

}  // namespace

int main() {
  run_test("system owns stable transition history",
           system_owns_stable_transition_history);
  run_test("spawn identity is unit-only and observation-safe",
           spawn_identity_is_unit_only_and_observation_safe);
  run_test("death retains identity and orders transitions",
           death_retains_identity_and_orders_transitions);
  run_test("snapshot and replay preserve casualty ledger",
           snapshot_and_replay_preserve_casualty_ledger);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
