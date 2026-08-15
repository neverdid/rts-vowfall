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
    const auto deadline = simulation.tick() + 800;
    while ((simulation.find_casualty(victim_identity) == nullptr ||
            simulation.find_casualty(victim_identity)->state !=
                CasualtyState::Dead) &&
           simulation.tick() < deadline) {
      simulation.step();
    }
    CHECK(simulation.find_casualty(victim_identity) != nullptr);
    CHECK(simulation.find_casualty(victim_identity) != nullptr &&
          simulation.find_casualty(victim_identity)->state ==
              CasualtyState::Dead);
  }

  void run_to_incapacitation() {
    const auto deadline = simulation.tick() + 240;
    while (simulation.find_entity(victim) != nullptr &&
           simulation.tick() < deadline) {
      simulation.step();
    }
    CHECK(simulation.find_entity(victim) == nullptr);
    CHECK(simulation.find_casualty(victim_identity) != nullptr);
    CHECK(simulation.find_casualty(victim_identity) != nullptr &&
          simulation.find_casualty(victim_identity)->state ==
              CasualtyState::Incapacitated);
  }

  void run_to_recoverable() {
    run_to_incapacitation();
    const auto deadline = simulation.tick() + 80;
    while (simulation.find_casualty(victim_identity)->state !=
               CasualtyState::Recoverable &&
           simulation.tick() < deadline) {
      simulation.step();
    }
    CHECK(simulation.find_casualty(victim_identity)->state ==
          CasualtyState::Recoverable);
  }
};

void system_owns_stable_transition_history() {
  CasualtySystem casualties;
  auto first = test_unit(7, 1, world(120, 240));
  auto second = test_unit(8, 2, world(160, 240));
  CHECK(casualties.register_unit(first, 4));
  CHECK(casualties.mark_wounded(first, EntityId{90}, 9));
  CHECK(!casualties.mark_wounded(first, EntityId{91}, 9));
  CHECK(!casualties.mark_incapacitated(first, EntityId{92}, 10));
  first.hit_points = 0;
  CHECK(casualties.mark_incapacitated(first, EntityId{92}, 10));
  CHECK(casualties.register_unit(second, 11));

  CHECK(casualties.records().size() == 2);
  CHECK(casualties.transitions().size() == 2);
  CHECK((casualties.transitions()[0] ==
         CasualtyTransition{UnitIdentityId{1}, EntityId{7},
                            CasualtyState::Active, CasualtyState::Wounded,
                            9, 0, EntityId{90}, world(120, 240)}));
  CHECK((casualties.transitions()[1] ==
         CasualtyTransition{UnitIdentityId{1}, EntityId{7},
                            CasualtyState::Wounded,
                            CasualtyState::Incapacitated, 10, 50,
                            EntityId{92}, world(120, 240)}));
  const auto* record = casualties.find(UnitIdentityId{1});
  CHECK(record != nullptr);
  CHECK(record != nullptr &&
        record->state == CasualtyState::Incapacitated);
  CHECK(record != nullptr && record->state_deadline == 50);
  CHECK(record != nullptr && record->injuries == 1);
  CHECK(!casualties.is_recoverable(UnitIdentityId{1}, 49));
  casualties.advance(49);
  CHECK(casualties.transitions().size() == 2);
  casualties.advance(50);
  CHECK(casualties.transitions().size() == 3);
  CHECK(casualties.find(UnitIdentityId{1})->state ==
        CasualtyState::Recoverable);
  CHECK(casualties.find(UnitIdentityId{1})->state_deadline == 450);
  CHECK(casualties.is_recoverable(UnitIdentityId{1}, 50));
  CHECK(casualties.is_recoverable(UnitIdentityId{1}, 449));
  CHECK(!casualties.is_recoverable(UnitIdentityId{1}, 450));
  casualties.advance(450);
  CHECK(casualties.transitions().size() == 4);
  CHECK(casualties.find(UnitIdentityId{1})->state == CasualtyState::Dead);
  CHECK((casualties.transitions()[2] ==
         CasualtyTransition{UnitIdentityId{1}, EntityId{7},
                            CasualtyState::Incapacitated,
                            CasualtyState::Recoverable, 50, 450,
                            EntityId{92}, world(120, 240)}));
  CHECK((casualties.transitions()[3] ==
         CasualtyTransition{UnitIdentityId{1}, EntityId{7},
                            CasualtyState::Recoverable, CasualtyState::Dead,
                            450, 0, EntityId{92}, world(120, 240)}));
  CHECK(casualties.derivation_matches(
      std::span<const Entity>{&second, 1}, 3, 450));
}

void simultaneous_deadlines_resolve_in_identity_order() {
  CasualtySystem casualties;
  auto first = test_unit(7, 1, world(120, 240));
  auto second = test_unit(8, 2, world(160, 240));
  CHECK(casualties.register_unit(first, 0));
  CHECK(casualties.register_unit(second, 0));
  first.hit_points = 0;
  second.hit_points = 0;
  CHECK(casualties.mark_incapacitated(second, EntityId{90}, 5));
  CHECK(casualties.mark_incapacitated(first, EntityId{91}, 5));
  casualties.advance(45);
  CHECK(casualties.transitions().size() == 4);
  CHECK(casualties.transitions()[2].identity == UnitIdentityId{1});
  CHECK(casualties.transitions()[3].identity == UnitIdentityId{2});
  CHECK(casualties.transitions()[2].current == CasualtyState::Recoverable);
  CHECK(casualties.transitions()[3].current == CasualtyState::Recoverable);
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
  fixture.run_to_incapacitation();

  auto* record = fixture.simulation.find_casualty(fixture.victim_identity);
  CHECK(record != nullptr);
  CHECK(record != nullptr && record->last_entity == fixture.victim);
  CHECK(record != nullptr &&
        record->state == CasualtyState::Incapacitated);
  CHECK(record != nullptr &&
        record->state_deadline ==
            record->state_since + kCasualtyStabilizationTicks);
  CHECK(record != nullptr && record->injuries == 1);
  CHECK(!fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));

  const auto stabilization_deadline = record->state_deadline;
  while (fixture.simulation.tick() < stabilization_deadline) {
    fixture.simulation.step();
  }
  record = fixture.simulation.find_casualty(fixture.victim_identity);
  CHECK(record->state == CasualtyState::Incapacitated);
  CHECK(!fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));
  fixture.simulation.step();
  record = fixture.simulation.find_casualty(fixture.victim_identity);
  CHECK(record->state == CasualtyState::Recoverable);
  CHECK(record->state_deadline ==
        stabilization_deadline + kBaseRecoveryWindowTicks);
  CHECK(fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));

  const auto recovery_deadline = record->state_deadline;
  while (fixture.simulation.tick() < recovery_deadline) {
    fixture.simulation.step();
  }
  record = fixture.simulation.find_casualty(fixture.victim_identity);
  CHECK(record->state == CasualtyState::Recoverable);
  CHECK(!fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));
  CHECK(std::ranges::none_of(
      fixture.simulation.events(), [&](const SimulationEvent& event) {
        return event_type(event) == SimulationEventType::UnitKilled &&
               std::get<UnitKilledEvent>(event.payload).identity ==
                   fixture.victim_identity;
      }));
  fixture.simulation.step();
  record = fixture.simulation.find_casualty(fixture.victim_identity);
  CHECK(record->state == CasualtyState::Dead);
  CHECK(record->state_deadline == 0);

  const auto history = fixture.simulation.casualty_history();
  CHECK(history.size() == 4);
  CHECK(history[0].previous == CasualtyState::Active);
  CHECK(history[0].current == CasualtyState::Wounded);
  CHECK(history[1].previous == CasualtyState::Wounded);
  CHECK(history[1].current == CasualtyState::Incapacitated);
  CHECK(history[2].previous == CasualtyState::Incapacitated);
  CHECK(history[2].current == CasualtyState::Recoverable);
  CHECK(history[3].previous == CasualtyState::Recoverable);
  CHECK(history[3].current == CasualtyState::Dead);

  const auto incapacitated = std::ranges::find_if(
      fixture.simulation.events(), [&](const SimulationEvent& event) {
        return event_type(event) ==
                   SimulationEventType::CasualtyStateChanged &&
               std::get<CasualtyStateChangedEvent>(event.payload).identity ==
                   fixture.victim_identity &&
               std::get<CasualtyStateChangedEvent>(event.payload).current ==
                   CasualtyState::Incapacitated;
      });
  const auto destroyed = std::ranges::find_if(
      fixture.simulation.events(), [&](const SimulationEvent& event) {
        return event_type(event) == SimulationEventType::EntityDestroyed &&
               std::get<EntityDestroyedEvent>(event.payload).identity ==
                   fixture.victim_identity;
      });
  const auto recoverable = std::ranges::find_if(
      fixture.simulation.events(), [&](const SimulationEvent& event) {
        return event_type(event) ==
                   SimulationEventType::CasualtyStateChanged &&
               std::get<CasualtyStateChangedEvent>(event.payload).identity ==
                   fixture.victim_identity &&
               std::get<CasualtyStateChangedEvent>(event.payload).current ==
                   CasualtyState::Recoverable;
      });

  const auto killed = std::ranges::find_if(
      fixture.simulation.events(), [&](const SimulationEvent& event) {
        return event_type(event) == SimulationEventType::UnitKilled &&
               std::get<UnitKilledEvent>(event.payload).identity ==
                   fixture.victim_identity;
      });
  CHECK(incapacitated != fixture.simulation.events().end());
  CHECK(destroyed != fixture.simulation.events().end());
  CHECK(recoverable != fixture.simulation.events().end());
  CHECK(killed != fixture.simulation.events().end());
  CHECK(incapacitated->id < destroyed->id);
  CHECK(destroyed->id < recoverable->id);
  CHECK(recoverable->id < killed->id);
  CHECK(killed != fixture.simulation.events().end() &&
        std::get<UnitKilledEvent>(killed->payload).previous ==
            CasualtyState::Recoverable);
}

void snapshot_and_replay_preserve_casualty_ledger() {
  BattleFixture fixture;
  ReplayRecorder recorder{fixture.simulation};
  fixture.run_to_incapacitation();
  recorder.capture_checkpoint(fixture.simulation);

  const auto incapacitated_snapshot = load_snapshot_v1(
      save_snapshot_v1(fixture.simulation));
  CHECK(incapacitated_snapshot);
  CHECK(incapacitated_snapshot.simulation != nullptr);
  CHECK(incapacitated_snapshot.simulation != nullptr &&
        incapacitated_snapshot.simulation->state_hash() ==
            fixture.simulation.state_hash());

  fixture.run_to_recoverable();
  recorder.capture_checkpoint(fixture.simulation);
  const auto recoverable_snapshot = load_snapshot_v1(
      save_snapshot_v1(fixture.simulation));
  CHECK(recoverable_snapshot);
  CHECK(recoverable_snapshot.simulation != nullptr);
  CHECK(recoverable_snapshot.simulation != nullptr &&
        recoverable_snapshot.simulation->state_hash() ==
            fixture.simulation.state_hash());
  CHECK(recoverable_snapshot.simulation != nullptr &&
        recoverable_snapshot.simulation->is_casualty_recoverable(
            fixture.victim_identity));

  fixture.run_to_death();
  recorder.capture_checkpoint(fixture.simulation);
  const auto dead_snapshot = load_snapshot_v1(
      save_snapshot_v1(fixture.simulation));
  CHECK(dead_snapshot);
  CHECK(dead_snapshot.simulation != nullptr);
  CHECK(dead_snapshot.simulation != nullptr &&
        std::ranges::equal(dead_snapshot.simulation->casualties(),
                           fixture.simulation.casualties()));
  CHECK(dead_snapshot.simulation != nullptr &&
        std::ranges::equal(dead_snapshot.simulation->casualty_history(),
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
  run_test("simultaneous deadlines resolve in identity order",
           simultaneous_deadlines_resolve_in_identity_order);
  run_test("spawn identity is unit-only and observation-safe",
           spawn_identity_is_unit_only_and_observation_safe);
  run_test("death retains identity and orders transitions",
           death_retains_identity_and_orders_transitions);
  run_test("snapshot and replay preserve casualty ledger",
           snapshot_and_replay_preserve_casualty_ledger);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
