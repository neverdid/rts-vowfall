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

[[nodiscard]] SimulationConfig empty_config(
    const FactionId player_one_faction = FactionId::Compact) {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.starting_ore = {2'000, 2'000};
  config.player_one_faction = player_one_faction;
  config.player_two_faction = player_one_faction == FactionId::Ascendancy
                                  ? FactionId::Compact
                                  : FactionId::Ascendancy;
  return config;
}

struct BattleFixture {
  Simulation simulation;
  EntityId victim{};
  UnitIdentityId victim_identity{};

  explicit BattleFixture(
      const FactionId victim_faction = FactionId::Compact)
      : simulation(empty_config(victim_faction)) {
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

struct RoadLedgerCasualtyFixture {
  Simulation simulation{empty_config()};
  EntityId source{};
  EntityId relay{};
  EntityId victim{};
  UnitIdentityId victim_identity{};

  RoadLedgerCasualtyFixture() {
    source = simulation.spawn_entity(
        PlayerId::One, EntityType::Command, world(100, 100));
    relay = simulation.spawn_entity(
        PlayerId::One, EntityType::Barracks, world(500, 100));
    static_cast<void>(simulation.spawn_entity(
        PlayerId::Two, EntityType::Command, world(1'300, 100)));
    victim = simulation.spawn_entity(
        PlayerId::One, EntityType::Vanguard, world(840, 100));
    victim_identity = simulation.find_entity(victim)->identity;
    for (std::int32_t index = 0; index < 8; ++index) {
      static_cast<void>(simulation.spawn_entity(
          PlayerId::Two, EntityType::Turret,
          world(885 + index, 90 + index * 3)));
    }
  }

  void run_to_recoverable() {
    const auto deadline = simulation.tick() + 240;
    while ((simulation.find_casualty(victim_identity) == nullptr ||
            simulation.find_casualty(victim_identity)->state !=
                CasualtyState::Recoverable) &&
           simulation.tick() < deadline) {
      simulation.step();
    }
    CHECK(simulation.find_entity(victim) == nullptr);
    CHECK(simulation.find_casualty(victim_identity) != nullptr);
    CHECK(simulation.find_casualty(victim_identity) != nullptr &&
          simulation.find_casualty(victim_identity)->state ==
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

void recovery_moves_identity_and_supports_reinjury() {
  CasualtySystem casualties;
  auto original = test_unit(7, 1, world(120, 240));
  CHECK(casualties.register_unit(original, 0));
  original.hit_points = 0;
  CHECK(casualties.mark_incapacitated(original, EntityId{90}, 5));
  casualties.advance(45);

  auto recovered = test_unit(12, 1, world(200, 260));
  recovered.casualty_state = CasualtyState::Recoverable;
  recovered.hit_points = 50;
  CHECK(casualties.recover(recovered, EntityId{4}, 46));
  const auto* record = casualties.find(UnitIdentityId{1});
  CHECK(record != nullptr);
  CHECK(record != nullptr && record->last_entity == EntityId{12});
  CHECK(record != nullptr && record->state == CasualtyState::Recovered);
  CHECK(record != nullptr && record->state_deadline == 0);
  CHECK(record != nullptr && record->last_source == EntityId{4});
  CHECK(record != nullptr && record->last_transition_position == world(200, 260));
  CHECK(casualties.transitions().back() ==
        (CasualtyTransition{UnitIdentityId{1}, EntityId{12},
                            CasualtyState::Recoverable,
                            CasualtyState::Recovered, 46, 0, EntityId{4},
                            world(200, 260)}));
  CHECK(casualties.derivation_matches(
      std::span<const Entity>{&recovered, 1}, 2, 46));

  CHECK(casualties.mark_wounded(recovered, EntityId{91}, 47));
  CHECK(casualties.find(UnitIdentityId{1})->injuries == 1);
  recovered.hit_points = 0;
  CHECK(casualties.mark_incapacitated(recovered, EntityId{92}, 48));
  CHECK(casualties.transitions()[3].previous == CasualtyState::Recovered);
  CHECK(casualties.transitions()[3].current == CasualtyState::Wounded);
  CHECK(casualties.transitions()[4].previous == CasualtyState::Wounded);
  CHECK(casualties.transitions()[4].current == CasualtyState::Incapacitated);
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
  const auto unstabilized_recovery = fixture.simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(!unstabilized_recovery.ok);
  CHECK(unstabilized_recovery.error == CommandError::CasualtyUnavailable);

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
  const auto expired_recovery = fixture.simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(!expired_recovery.ok);
  CHECK(expired_recovery.error == CommandError::CasualtyUnavailable);
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

void road_ledger_access_gates_compact_recovery() {
  RoadLedgerCasualtyFixture fixture;
  ReplayRecorder recorder{fixture.simulation};
  CHECK(!fixture.simulation.casualty_recovery_anchor(
      fixture.victim_identity));
  CHECK(fixture.simulation.is_supply_connected(fixture.source));
  CHECK(fixture.simulation.is_supply_connected(fixture.relay));
  fixture.run_to_recoverable();

  CHECK(fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));
  CHECK(fixture.simulation.casualty_recovery_anchor(
            fixture.victim_identity) == fixture.relay);

  const auto connected_snapshot = load_snapshot_v1(
      save_snapshot_v1(fixture.simulation));
  CHECK(connected_snapshot);
  CHECK(connected_snapshot.simulation != nullptr);
  CHECK(connected_snapshot.simulation != nullptr &&
        connected_snapshot.simulation->casualty_recovery_anchor(
            fixture.victim_identity) == fixture.relay);

  const auto connected_replay = verify_replay_v1(
      save_replay_v1(recorder.finish(fixture.simulation)));
  CHECK(connected_replay);
  CHECK(connected_replay.simulation != nullptr);
  CHECK(connected_replay.simulation != nullptr &&
        connected_replay.simulation->is_casualty_recoverable(
            fixture.victim_identity));
  CHECK(connected_replay.simulation != nullptr &&
        connected_replay.simulation->casualty_recovery_anchor(
            fixture.victim_identity) == fixture.relay);

  for (std::int32_t index = 0; index < 128; ++index) {
    static_cast<void>(fixture.simulation.spawn_entity(
        PlayerId::Two, EntityType::Turret, world(545, 100)));
  }
  fixture.simulation.step();
  CHECK(fixture.simulation.find_entity(fixture.relay) == nullptr);
  CHECK(!fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));
  CHECK(!fixture.simulation.casualty_recovery_anchor(
      fixture.victim_identity));
  const auto cut_recovery = fixture.simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(!cut_recovery.ok);
  CHECK(cut_recovery.error == CommandError::SupplyBlocked);

  const auto disconnected_snapshot = load_snapshot_v1(
      save_snapshot_v1(fixture.simulation));
  CHECK(disconnected_snapshot);
  CHECK(disconnected_snapshot.simulation != nullptr);
  CHECK(disconnected_snapshot.simulation != nullptr &&
        !disconnected_snapshot.simulation->is_casualty_recoverable(
            fixture.victim_identity));

  const auto replacement = fixture.simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  CHECK(fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));
  CHECK(fixture.simulation.casualty_recovery_anchor(
            fixture.victim_identity) == replacement);
}

void recovery_command_reembodies_the_same_identity() {
  RoadLedgerCasualtyFixture fixture;
  fixture.run_to_recoverable();
  const auto* retained = fixture.simulation.find_casualty(
      fixture.victim_identity);
  CHECK(retained != nullptr);
  const auto retained_injuries = retained == nullptr ? 0U : retained->injuries;
  const auto supply_before = fixture.simulation.player(PlayerId::One).supply_used;
  const auto maximum_entity = std::ranges::max_element(
      fixture.simulation.entities(), {}, &Entity::id)->id;

  const auto own_observation = fixture.simulation.observe(PlayerId::One);
  const auto enemy_observation = fixture.simulation.observe(PlayerId::Two);
  CHECK(own_observation.permits(CommandType::RecoverCasualty, {},
                                std::nullopt, std::nullopt,
                                fixture.victim_identity));
  CHECK(!enemy_observation.permits(CommandType::RecoverCasualty, {},
                                   std::nullopt, std::nullopt,
                                   fixture.victim_identity));

  const auto unknown = fixture.simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = UnitIdentityId{4'000}});
  CHECK(!unknown.ok);
  CHECK(unknown.error == CommandError::CasualtyUnavailable);
  const auto wrong_owner = fixture.simulation.execute_now(
      Command{.player = PlayerId::Two,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(!wrong_owner.ok);
  CHECK(wrong_owner.error == CommandError::InvalidOwner);

  const auto anchor = fixture.simulation.casualty_recovery_anchor(
      fixture.victim_identity);
  const auto* anchor_entity = fixture.simulation.find_entity(anchor);
  CHECK(anchor_entity != nullptr);
  const auto anchor_position = anchor_entity == nullptr
                                   ? Vec2{}
                                   : anchor_entity->position;
  const auto anchor_radius = anchor_entity == nullptr ? 0 : anchor_entity->radius;
  const auto recovery = fixture.simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(recovery.ok);

  retained = fixture.simulation.find_casualty(fixture.victim_identity);
  CHECK(retained != nullptr);
  CHECK(retained != nullptr && retained->state == CasualtyState::Recovered);
  CHECK(retained != nullptr && retained->last_entity.value == maximum_entity.value + 1U);
  CHECK(retained != nullptr && retained->injuries == retained_injuries);
  CHECK(retained != nullptr && retained->last_source == anchor);
  const auto* embodied = retained == nullptr
                             ? nullptr
                             : fixture.simulation.find_entity(retained->last_entity);
  CHECK(embodied != nullptr);
  CHECK(embodied != nullptr && embodied->identity == fixture.victim_identity);
  CHECK(embodied != nullptr && embodied->casualty_state == CasualtyState::Recovered);
  CHECK(embodied != nullptr && embodied->hit_points == embodied->max_hit_points / 2);
  CHECK((embodied != nullptr && anchor_entity != nullptr &&
         embodied->position ==
             Vec2{anchor_position.x + anchor_radius + embodied->radius + 8'000,
                  anchor_position.y}));
  CHECK(fixture.simulation.player(PlayerId::One).supply_used ==
        supply_before + (embodied == nullptr ? 0 : embodied->supply_cost));
  CHECK(!fixture.simulation.observe(PlayerId::One).permits(
      CommandType::RecoverCasualty, {}, std::nullopt, std::nullopt,
      fixture.victim_identity));

  const auto& events = fixture.simulation.events();
  CHECK(events.size() >= 3);
  CHECK(event_type(events[events.size() - 3]) ==
        SimulationEventType::EntitySpawned);
  CHECK(event_type(events[events.size() - 2]) ==
        SimulationEventType::CasualtyStateChanged);
  CHECK(event_type(events.back()) == SimulationEventType::UnitRecovered);
  if (event_type(events.back()) == SimulationEventType::UnitRecovered) {
    const auto& payload = std::get<UnitRecoveredEvent>(events.back().payload);
    CHECK(payload.entity == retained->last_entity);
    CHECK(payload.recovery_source == anchor);
    CHECK(payload.identity == fixture.victim_identity);
  }

  const auto duplicate = fixture.simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(!duplicate.ok);
  CHECK(duplicate.error == CommandError::CasualtyUnavailable);

  if (embodied != nullptr) {
    static_cast<void>(fixture.simulation.spawn_entity(
        PlayerId::Two, EntityType::Turret,
        {embodied->position.x + world(45, 0).x, embodied->position.y}));
    fixture.simulation.step();
    const auto wounded = std::ranges::find_if(
        fixture.simulation.events(), [&](const SimulationEvent& event) {
          return event_type(event) == SimulationEventType::UnitWounded &&
                 std::get<UnitWoundedEvent>(event.payload).identity ==
                     fixture.victim_identity &&
                 std::get<UnitWoundedEvent>(event.payload).previous ==
                     CasualtyState::Recovered;
        });
    CHECK(wounded != fixture.simulation.events().end());
  }
}

void recovery_respects_population_capacity() {
  RoadLedgerCasualtyFixture fixture;
  fixture.run_to_recoverable();
  while (fixture.simulation.player(PlayerId::One).supply_used <
         fixture.simulation.player(PlayerId::One).supply_cap) {
    static_cast<void>(fixture.simulation.spawn_entity(
        PlayerId::One, EntityType::Worker, world(300, 300)));
  }
  CHECK(!fixture.simulation.observe(PlayerId::One).permits(
      CommandType::RecoverCasualty, {}, std::nullopt, std::nullopt,
      fixture.victim_identity));
  const auto result = fixture.simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(!result.ok);
  CHECK(result.error == CommandError::SupplyBlocked);
  CHECK(fixture.simulation.find_casualty(fixture.victim_identity)->state ==
        CasualtyState::Recoverable);
}

void non_compact_recovery_does_not_require_road_ledger_access() {
  BattleFixture fixture{FactionId::Ascendancy};
  ReplayRecorder recorder{fixture.simulation};
  fixture.run_to_recoverable();
  CHECK(fixture.simulation.is_casualty_recoverable(
      fixture.victim_identity));
  CHECK(!fixture.simulation.casualty_recovery_anchor(
      fixture.victim_identity));
  const auto retained_position = fixture.simulation.find_casualty(
      fixture.victim_identity)->last_transition_position;
  const auto result = recorder.execute_now(
      fixture.simulation,
      Command{.player = PlayerId::One,
              .type = CommandType::RecoverCasualty,
              .casualty = fixture.victim_identity});
  CHECK(result.ok);
  const auto* record = fixture.simulation.find_casualty(
      fixture.victim_identity);
  const auto* entity = record == nullptr
                           ? nullptr
                           : fixture.simulation.find_entity(record->last_entity);
  CHECK(record != nullptr && record->state == CasualtyState::Recovered);
  CHECK(entity != nullptr && entity->position == retained_position);

  const auto loaded = load_snapshot_v1(save_snapshot_v1(fixture.simulation));
  CHECK(loaded);
  CHECK(loaded.simulation != nullptr);
  CHECK(loaded.simulation != nullptr &&
        loaded.simulation->state_hash() == fixture.simulation.state_hash());
  const auto verified = verify_replay_v1(
      save_replay_v1(recorder.finish(fixture.simulation)));
  CHECK(verified);
  CHECK(verified.simulation != nullptr);
  CHECK(verified.simulation != nullptr &&
        verified.simulation->state_hash() == fixture.simulation.state_hash());
}

void queued_recovery_survives_checkpoint_and_replay() {
  BattleFixture fixture{FactionId::Ascendancy};
  ReplayRecorder recorder{fixture.simulation};
  fixture.run_to_recoverable();

  Command recovery{.execute_tick = fixture.simulation.tick() + 2,
                   .player = PlayerId::One,
                   .type = CommandType::RecoverCasualty,
                   .casualty = fixture.victim_identity};
  const auto sequence = recorder.enqueue(fixture.simulation, recovery);
  CHECK(sequence != 0);
  const auto loaded = load_snapshot_v1(save_snapshot_v1(fixture.simulation));
  CHECK(loaded);
  CHECK(loaded.simulation != nullptr);
  CHECK(loaded.simulation != nullptr &&
        loaded.simulation->state_hash() == fixture.simulation.state_hash());

  fixture.simulation.run(3);
  if (loaded.simulation != nullptr) {
    loaded.simulation->run(3);
    CHECK(loaded.simulation->state_hash() == fixture.simulation.state_hash());
  }
  CHECK(std::ranges::any_of(
      fixture.simulation.casualty_history(),
      [&](const CasualtyTransition& transition) {
        return transition.identity == fixture.victim_identity &&
               transition.current == CasualtyState::Recovered;
      }));
  CHECK(!fixture.simulation.command_trace().empty());
  CHECK(!fixture.simulation.command_trace().empty() &&
        fixture.simulation.command_trace().back().accepted);
  CHECK(!fixture.simulation.command_trace().empty() &&
        fixture.simulation.command_trace().back().command.casualty ==
            fixture.victim_identity);

  const auto verified = verify_replay_v1(
      save_replay_v1(recorder.finish(fixture.simulation)));
  CHECK(verified);
  CHECK(verified.simulation != nullptr);
  CHECK(verified.simulation != nullptr &&
        verified.simulation->state_hash() == fixture.simulation.state_hash());
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
  run_test("recovery moves identity and supports reinjury",
           recovery_moves_identity_and_supports_reinjury);
  run_test("spawn identity is unit-only and observation-safe",
           spawn_identity_is_unit_only_and_observation_safe);
  run_test("death retains identity and orders transitions",
           death_retains_identity_and_orders_transitions);
  run_test("Road Ledger access gates Compact recovery",
           road_ledger_access_gates_compact_recovery);
  run_test("recovery command reembodies the same identity",
           recovery_command_reembodies_the_same_identity);
  run_test("recovery respects population capacity",
           recovery_respects_population_capacity);
  run_test("non-Compact recovery does not require Road Ledger access",
           non_compact_recovery_does_not_require_road_ledger_access);
  run_test("queued recovery survives checkpoint and replay",
           queued_recovery_survives_checkpoint_and_replay);
  run_test("snapshot and replay preserve casualty ledger",
           snapshot_and_replay_preserve_casualty_ledger);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
