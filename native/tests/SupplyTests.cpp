#include "ashen/core/Catalog.hpp"
#include "ashen/core/Content.hpp"
#include "ashen/core/Replay.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/Snapshot.hpp"
#include "ashen/core/SpatialGrid.hpp"
#include "ashen/core/SupplySystem.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <ranges>
#include <string_view>
#include <vector>

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

[[nodiscard]] Entity supply_entity(const std::uint32_t id,
                                   const EntityType type,
                                   const Vec2 position,
                                   const PlayerId owner = PlayerId::One,
                                   const bool under_construction = false) {
  Entity entity{};
  entity.id = EntityId{id};
  entity.owner = owner;
  entity.faction = FactionId::Compact;
  entity.type = type;
  entity.kind = EntityKind::Building;
  entity.position = position;
  entity.hit_points = 1;
  entity.max_hit_points = 1;
  entity.under_construction = under_construction;
  return entity;
}

[[nodiscard]] SpatialGrid grid_for(const std::vector<Entity>& entities) {
  SpatialGrid grid;
  grid.reset(world(2'000, 600), world(160, 0).x);
  grid.rebuild(entities);
  return grid;
}

[[nodiscard]] bool has_content_error(
    const std::vector<ContentValidationIssue>& issues,
    const ContentValidationError error) {
  return std::ranges::any_of(
      issues, [error](const ContentValidationIssue& issue) {
        return issue.error == error;
      });
}

[[nodiscard]] bool has_supply_event(const Simulation& simulation,
                                    const SimulationEventType type,
                                    const EntityId entity) {
  return std::ranges::any_of(simulation.events(), [&](const auto& event) {
    if (event_type(event) != type) {
      return false;
    }
    if (type == SimulationEventType::SupplyConnected) {
      return std::get<SupplyConnectedEvent>(event.payload).entity == entity;
    }
    return std::get<SupplyDisconnectedEvent>(event.payload).entity == entity;
  });
}

[[nodiscard]] SimulationConfig ledger_config() {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.starting_ore = {2'000, 2'000};
  config.player_one_faction = FactionId::Compact;
  config.player_two_faction = FactionId::Ascendancy;
  return config;
}

struct LedgerFixture {
  Simulation simulation{ledger_config()};
  EntityId source{};
  EntityId consumer{};
  EntityId enemy_command{};
  EntityId relay{};

  LedgerFixture() {
    source = simulation.spawn_entity(PlayerId::One, EntityType::Command,
                                     world(100, 100));
    consumer = simulation.spawn_entity(PlayerId::One, EntityType::Barracks,
                                       world(820, 100));
    enemy_command = simulation.spawn_entity(
        PlayerId::Two, EntityType::Command, world(1'250, 500));
    relay = simulation.spawn_entity(PlayerId::One, EntityType::Barracks,
                                    world(460, 100));
  }
};

void supply_content_is_stable_and_validated() {
  const auto& content = builtin_content();
  CHECK(validate_content(content).empty());
  CHECK(content.supply_nodes.size() == 3);
  CHECK(find_supply_node_content(content, FactionId::Compact,
                                 EntityType::Command) != nullptr);
  CHECK(find_supply_node_content(content, FactionId::Ascendancy,
                                 EntityType::Command) == nullptr);

  auto duplicate = content;
  duplicate.supply_nodes.push_back(duplicate.supply_nodes.front());
  CHECK(has_content_error(validate_content(duplicate),
                          ContentValidationError::DuplicateSupplyNodeStructure));

  auto missing = content;
  missing.supply_nodes.front().structure = 999'999;
  CHECK(has_content_error(validate_content(missing),
                          ContentValidationError::MissingContentReference));

  auto invalid = content;
  invalid.supply_nodes[1].link_range = 0;
  CHECK(has_content_error(validate_content(invalid),
                          ContentValidationError::InvalidSupplyNode));
}

void capacity_and_equal_route_ties_are_stable() {
  auto content = builtin_content();
  const auto source_definition = std::ranges::find(
      content.supply_nodes, content_id::CompactLedgerKeep,
      [](const SupplyNodeContentDefinition& definition) {
        return definition.metadata.stable_id;
      });
  CHECK(source_definition != content.supply_nodes.end());
  if (source_definition == content.supply_nodes.end()) {
    return;
  }
  source_definition->capacity = 2;

  std::vector<Entity> capacity_nodes{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Turret, world(200, 100)),
      supply_entity(3, EntityType::Turret, world(200, 100)),
      supply_entity(4, EntityType::Turret, world(200, 100)),
  };
  auto capacity_grid = grid_for(capacity_nodes);
  SupplySystem capacity;
  const auto transitions =
      capacity.evaluate(capacity_nodes, capacity_grid, content);
  CHECK(transitions ==
        std::vector<SupplyTransition>({{EntityId{1}, true},
                                       {EntityId{2}, true},
                                       {EntityId{3}, true}}));
  CHECK(capacity.connected(EntityId{2}));
  CHECK(capacity.connected(EntityId{3}));
  CHECK(!capacity.connected(EntityId{4}));

  std::vector<Entity> projected_nodes{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Turret, world(200, 100)),
  };
  auto projected_grid = grid_for(projected_nodes);
  SupplySystem projected;
  static_cast<void>(
      projected.evaluate(projected_nodes, projected_grid, content));
  CHECK(projected.can_connect_construction_site(
      EntityId{3}, PlayerId::One, FactionId::Compact,
      EntityType::Turret, world(200, 100), projected_nodes,
      projected_grid, content));
  projected_nodes.push_back(
      supply_entity(3, EntityType::Turret, world(200, 100)));
  projected_grid = grid_for(projected_nodes);
  projected.rebuild(projected_nodes, projected_grid, content);
  const auto before_projection = projected.state_hash();
  CHECK(!projected.can_connect_construction_site(
      EntityId{4}, PlayerId::One, FactionId::Compact,
      EntityType::Turret, world(200, 100), projected_nodes,
      projected_grid, content));
  CHECK(projected.state_hash() == before_projection);

  std::vector<Entity> tied_nodes{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Command, world(300, 100)),
      supply_entity(3, EntityType::Turret, world(200, 100)),
  };
  auto tied_grid = grid_for(tied_nodes);
  SupplySystem tied;
  static_cast<void>(tied.evaluate(tied_nodes, tied_grid, builtin_content()));
  const auto* consumer = tied.find(EntityId{3});
  CHECK(consumer != nullptr);
  CHECK(consumer != nullptr && consumer->connected);
  CHECK(consumer != nullptr && consumer->source == EntityId{1});
  CHECK(consumer != nullptr && consumer->predecessor == EntityId{1});
  CHECK(consumer != nullptr && consumer->hops == 1);
}

void construction_sites_consume_but_do_not_relay_supply() {
  std::vector<Entity> entities{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Barracks, world(500, 100),
                    PlayerId::One, true),
      supply_entity(3, EntityType::Turret, world(840, 100)),
  };
  auto grid = grid_for(entities);
  SupplySystem supply;
  static_cast<void>(supply.evaluate(entities, grid, builtin_content()));
  CHECK(supply.connected(EntityId{2}));
  CHECK(!supply.connected(EntityId{3}));

  entities[1].under_construction = false;
  grid = grid_for(entities);
  CHECK(supply.evaluate(entities, grid, builtin_content()) ==
        std::vector<SupplyTransition>({{EntityId{3}, true}}));
  CHECK(supply.connected(EntityId{3}));
  CHECK(supply.find(EntityId{3}) != nullptr &&
        supply.find(EntityId{3})->predecessor == EntityId{2});
}

void retreat_anchors_use_connected_transmitters_and_stable_ties() {
  std::vector<Entity> tied_entities{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Command, world(300, 100)),
  };
  auto tied_grid = grid_for(tied_entities);
  SupplySystem tied;
  tied.rebuild(tied_entities, tied_grid, builtin_content());
  CHECK(tied.retreat_anchor(PlayerId::One, world(200, 100),
                            tied_entities, builtin_content()) == EntityId{1});

  std::vector<Entity> routed_entities{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Barracks, world(500, 100)),
      supply_entity(3, EntityType::Barracks, world(500, 200),
                    PlayerId::One, true),
  };
  auto routed_grid = grid_for(routed_entities);
  SupplySystem routed;
  routed.rebuild(routed_entities, routed_grid, builtin_content());
  CHECK(routed.retreat_anchor(PlayerId::One, world(840, 100),
                              routed_entities, builtin_content()) ==
        EntityId{2});

  routed_entities.erase(routed_entities.begin() + 1);
  routed_grid = grid_for(routed_entities);
  routed.rebuild(routed_entities, routed_grid, builtin_content());
  CHECK(!routed.retreat_anchor(PlayerId::One, world(840, 100),
                               routed_entities, builtin_content()));
}

void recovery_anchors_use_connected_transmitters_and_stable_ties() {
  std::vector<Entity> tied_entities{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Command, world(300, 100)),
  };
  auto tied_grid = grid_for(tied_entities);
  SupplySystem tied;
  tied.rebuild(tied_entities, tied_grid, builtin_content());
  CHECK(tied.recovery_anchor(PlayerId::One, world(200, 100),
                             tied_entities, builtin_content()) ==
        EntityId{1});

  std::vector<Entity> routed_entities{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Barracks, world(500, 100)),
      supply_entity(3, EntityType::Barracks, world(500, 200),
                    PlayerId::One, true),
      supply_entity(4, EntityType::Turret, world(840, 100)),
  };
  auto routed_grid = grid_for(routed_entities);
  SupplySystem routed;
  routed.rebuild(routed_entities, routed_grid, builtin_content());
  CHECK(routed.recovery_anchor(PlayerId::One, world(820, 200),
                               routed_entities, builtin_content()) ==
        EntityId{2});
  CHECK(routed.recovery_anchor(PlayerId::One, world(860, 100),
                               routed_entities, builtin_content()) ==
        EntityId{2});
  CHECK(!routed.recovery_anchor(PlayerId::One, world(861, 100),
                                routed_entities, builtin_content()));

  routed_entities.erase(routed_entities.begin() + 1);
  routed_grid = grid_for(routed_entities);
  routed.rebuild(routed_entities, routed_grid, builtin_content());
  CHECK(!routed.recovery_anchor(PlayerId::One, world(860, 100),
                                routed_entities, builtin_content()));
}

void relay_cut_and_reconnect_change_the_graph_deterministically() {
  std::vector<Entity> entities{
      supply_entity(1, EntityType::Command, world(100, 100)),
      supply_entity(2, EntityType::Barracks, world(500, 100)),
      supply_entity(3, EntityType::Turret, world(840, 100)),
  };
  auto grid = grid_for(entities);
  SupplySystem supply;
  CHECK(supply.evaluate(entities, grid, builtin_content()) ==
        std::vector<SupplyTransition>({{EntityId{1}, true},
                                       {EntityId{2}, true},
                                       {EntityId{3}, true}}));
  CHECK(supply.find(EntityId{3}) != nullptr &&
        supply.find(EntityId{3})->predecessor == EntityId{2});
  const auto connected_hash = supply.state_hash();

  entities.erase(entities.begin() + 1);
  grid = grid_for(entities);
  CHECK(supply.evaluate(entities, grid, builtin_content()) ==
        std::vector<SupplyTransition>({{EntityId{2}, false},
                                       {EntityId{3}, false}}));
  CHECK(!supply.connected(EntityId{3}));
  CHECK(supply.state_hash() != connected_hash);

  entities.push_back(
      supply_entity(4, EntityType::Barracks, world(500, 100)));
  std::ranges::sort(entities, {}, &Entity::id);
  grid = grid_for(entities);
  CHECK(supply.evaluate(entities, grid, builtin_content()) ==
        std::vector<SupplyTransition>({{EntityId{3}, true},
                                       {EntityId{4}, true}}));
  CHECK(supply.connected(EntityId{3}));
  CHECK(supply.find(EntityId{3}) != nullptr &&
        supply.find(EntityId{3})->predecessor == EntityId{4});
  CHECK(supply.derivation_matches(entities, grid, builtin_content()));
}

void reinforcement_legality_and_progress_follow_the_route() {
  Simulation simulation{ledger_config()};
  const auto source = simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100));
  const auto producer = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(820, 100));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'250, 500)));
  CHECK(simulation.is_supply_connected(source));
  CHECK(!simulation.is_supply_connected(producer));
  CHECK(!simulation.observe(PlayerId::One).permits(
      CommandType::Train, producer, EntityType::Vanguard));
  const auto disconnected = simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::Train,
              .producer = producer,
              .train_type = EntityType::Vanguard});
  CHECK(!disconnected.ok);
  CHECK(disconnected.error == CommandError::SupplyBlocked);

  const auto relay = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(460, 100));
  CHECK(simulation.is_supply_connected(relay));
  CHECK(simulation.is_supply_connected(producer));
  CHECK(simulation.observe(PlayerId::One).permits(
      CommandType::Train, producer, EntityType::Vanguard));
  CHECK(simulation.execute_now(
            Command{.player = PlayerId::One,
                    .type = CommandType::Train,
                    .producer = producer,
                    .train_type = EntityType::Vanguard})
            .ok);

  for (std::int32_t index = 0; index < 8; ++index) {
    static_cast<void>(simulation.spawn_entity(
        PlayerId::Two, EntityType::Turret,
        world(500 + index, 100 + index)));
  }
  const auto deadline = simulation.tick() + 240;
  while (simulation.find_entity(relay) != nullptr &&
         simulation.tick() < deadline) {
    simulation.step();
  }
  CHECK(simulation.find_entity(relay) == nullptr);
  CHECK(simulation.find_entity(producer) != nullptr);
  CHECK(!simulation.is_supply_connected(producer));
  CHECK(has_supply_event(simulation, SimulationEventType::SupplyDisconnected,
                         relay));
  CHECK(has_supply_event(simulation, SimulationEventType::SupplyDisconnected,
                         producer));
  CHECK(!simulation.observe(PlayerId::One).permits(
      CommandType::Train, producer, EntityType::Vanguard));

  const auto* cut_producer = simulation.find_entity(producer);
  CHECK(cut_producer != nullptr && !cut_producer->production_queue.empty());
  const auto remaining =
      cut_producer != nullptr && !cut_producer->production_queue.empty()
          ? cut_producer->production_queue.front().remaining_ticks
          : Tick{};
  simulation.run(20);
  const auto* paused = simulation.find_entity(producer);
  CHECK(paused != nullptr && !paused->production_queue.empty());
  CHECK(paused != nullptr && !paused->production_queue.empty() &&
        paused->production_queue.front().remaining_ticks == remaining);

  const auto replacement = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(460, 100));
  CHECK(simulation.is_supply_connected(replacement));
  CHECK(simulation.is_supply_connected(producer));
  simulation.step();
  const auto* resumed = simulation.find_entity(producer);
  CHECK(resumed != nullptr && !resumed->production_queue.empty());
  CHECK(resumed != nullptr && !resumed->production_queue.empty() &&
        resumed->production_queue.front().remaining_ticks + 1 == remaining);
}

void construction_legality_and_progress_follow_the_route() {
  Simulation simulation{ledger_config()};
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100)));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'250, 500)));
  const auto worker = simulation.spawn_entity(
      PlayerId::One, EntityType::Worker, world(770, 100));
  const auto opening_ore = simulation.player(PlayerId::One).ore;
  const auto opening_entities = simulation.entities().size();
  const auto build = Command{.player = PlayerId::One,
                             .type = CommandType::Build,
                             .entities = {worker},
                             .target = world(840, 100),
                             .building_type = EntityType::Turret};

  const auto disconnected = simulation.execute_now(build);
  CHECK(!disconnected.ok);
  CHECK(disconnected.error == CommandError::SupplyBlocked);
  CHECK(simulation.player(PlayerId::One).ore == opening_ore);
  CHECK(simulation.entities().size() == opening_entities);

  const auto relay = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  CHECK(simulation.execute_now(build).ok);
  CHECK(simulation.player(PlayerId::One).ore ==
        opening_ore -
            entity_definition(FactionId::Compact, EntityType::Turret).cost);
  const auto site = std::ranges::find_if(
      simulation.entities(), [](const Entity& entity) {
        return entity.type == EntityType::Turret && entity.under_construction;
      });
  CHECK(site != simulation.entities().end());
  const auto site_id =
      site == simulation.entities().end() ? EntityId{} : site->id;
  CHECK(simulation.is_supply_connected(site_id));
  CHECK(has_supply_event(simulation, SimulationEventType::SupplyConnected,
                         site_id));

  for (std::int32_t index = 0; index < 8; ++index) {
    static_cast<void>(simulation.spawn_entity(
        PlayerId::Two, EntityType::Turret,
        world(540 + index, 100 + index)));
  }
  const auto deadline = simulation.tick() + 180;
  while (simulation.find_entity(relay) != nullptr &&
         simulation.tick() < deadline) {
    simulation.step();
  }
  CHECK(simulation.find_entity(relay) == nullptr);
  CHECK(!simulation.is_supply_connected(site_id));
  CHECK(has_supply_event(simulation, SimulationEventType::SupplyDisconnected,
                         site_id));
  const auto* cut_site = simulation.find_entity(site_id);
  CHECK(cut_site != nullptr && cut_site->under_construction);
  const auto paused_ticks =
      cut_site == nullptr ? Tick{} : cut_site->construction_ticks;
  simulation.run(20);
  CHECK(simulation.find_entity(site_id) != nullptr &&
        simulation.find_entity(site_id)->construction_ticks == paused_ticks);

  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Stop,
                                       .entities = {worker}})
            .ok);
  CHECK(simulation.observe(PlayerId::One).permits(
      CommandType::Build, worker, EntityType::Turret));
  const auto ore_before_resume = simulation.player(PlayerId::One).ore;
  CHECK(simulation.execute_now(
            Command{.player = PlayerId::One,
                    .type = CommandType::Build,
                    .entities = {worker},
                    .target_entity = site_id,
                    .building_type = EntityType::Turret})
            .ok);
  CHECK(simulation.player(PlayerId::One).ore == ore_before_resume);
  simulation.step();
  CHECK(simulation.find_entity(site_id) != nullptr &&
        simulation.find_entity(site_id)->construction_ticks == paused_ticks);

  const auto replacement = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  CHECK(simulation.is_supply_connected(replacement));
  CHECK(simulation.is_supply_connected(site_id));
  simulation.step();
  CHECK(simulation.find_entity(site_id) != nullptr &&
        simulation.find_entity(site_id)->construction_ticks ==
            paused_ticks + 1);
}

void retreat_legality_and_default_destination_follow_the_route() {
  Simulation simulation{ledger_config()};
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100)));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'250, 500)));
  const auto relay = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  const auto unit = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(840, 100));
  CHECK(simulation.observe(PlayerId::One).permits(CommandType::Retreat,
                                                   unit));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Retreat,
                                       .entities = {unit}})
            .ok);
  CHECK(simulation.find_entity(unit) != nullptr &&
        simulation.find_entity(unit)->order.type == OrderType::Move);
  CHECK(simulation.find_entity(unit) != nullptr &&
        simulation.find_entity(unit)->order.target == world(100, 100));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Stop,
                                       .entities = {unit}})
            .ok);

  for (std::int32_t index = 0; index < 8; ++index) {
    static_cast<void>(simulation.spawn_entity(
        PlayerId::Two, EntityType::Turret,
        world(540 + index, 100 + index)));
  }
  const auto deadline = simulation.tick() + 180;
  while (simulation.find_entity(relay) != nullptr &&
         simulation.tick() < deadline) {
    simulation.step();
  }
  CHECK(simulation.find_entity(relay) == nullptr);
  CHECK(!simulation.observe(PlayerId::One).permits(CommandType::Retreat,
                                                    unit));
  const auto rejected = simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::Retreat,
              .entities = {unit}});
  CHECK(!rejected.ok);
  CHECK(rejected.error == CommandError::SupplyBlocked);

  auto loaded = load_snapshot_v1(save_snapshot_v1(simulation));
  CHECK(loaded);
  CHECK(loaded.simulation != nullptr);
  if (loaded.simulation == nullptr) {
    return;
  }
  CHECK(!loaded.simulation->observe(PlayerId::One).permits(
      CommandType::Retreat, unit));
  const auto loaded_rejection = loaded.simulation->execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::Retreat,
              .entities = {unit}});
  const auto original_rejection = simulation.execute_now(
      Command{.player = PlayerId::One,
              .type = CommandType::Retreat,
              .entities = {unit}});
  CHECK(loaded_rejection.error == CommandError::SupplyBlocked);
  CHECK(original_rejection.error == loaded_rejection.error);

  const auto original_replacement = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  const auto loaded_replacement = loaded.simulation->spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  CHECK(original_replacement == loaded_replacement);
  CHECK(simulation.observe(PlayerId::One).permits(CommandType::Retreat,
                                                   unit));
  CHECK(loaded.simulation->observe(PlayerId::One).permits(
      CommandType::Retreat, unit));
  const auto restored_command = Command{.player = PlayerId::One,
                                        .type = CommandType::Retreat,
                                        .entities = {unit}};
  CHECK(simulation.execute_now(restored_command).ok);
  CHECK(loaded.simulation->execute_now(restored_command).ok);
  CHECK(loaded.simulation->state_hash() == simulation.state_hash());
  CHECK(loaded.simulation->events() == simulation.events());
}

void non_compact_retreat_behavior_is_unchanged() {
  auto config = ledger_config();
  config.player_one_faction = FactionId::Ascendancy;
  Simulation simulation{config};
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100)));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'250, 500)));
  const auto unit = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(840, 100));
  CHECK(simulation.observe(PlayerId::One).permits(CommandType::Retreat,
                                                   unit));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Retreat,
                                       .entities = {unit}})
            .ok);
  CHECK(simulation.find_entity(unit) != nullptr &&
        simulation.find_entity(unit)->order.target == world(100, 100));
}

void checkpoint_rebuilds_supply_without_duplicate_transitions() {
  LedgerFixture fixture;
  CHECK(fixture.simulation.is_supply_connected(fixture.consumer));
  const auto before_events = fixture.simulation.events().size();
  const auto snapshot = save_snapshot_v1(fixture.simulation);
  auto loaded = load_snapshot_v1(snapshot);
  CHECK(loaded);
  CHECK(loaded.simulation != nullptr);
  if (loaded.simulation == nullptr) {
    return;
  }
  CHECK(loaded.simulation->state_hash() == fixture.simulation.state_hash());
  CHECK(std::ranges::equal(loaded.simulation->supply_nodes(),
                           fixture.simulation.supply_nodes()));
  CHECK(loaded.simulation->events().size() == before_events);

  fixture.simulation.step();
  loaded.simulation->step();
  CHECK(loaded.simulation->state_hash() == fixture.simulation.state_hash());
  CHECK(loaded.simulation->events() == fixture.simulation.events());
}

void checkpoint_rebuilds_an_active_construction_route() {
  Simulation simulation{ledger_config()};
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100)));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'250, 500)));
  const auto worker = simulation.spawn_entity(
      PlayerId::One, EntityType::Worker, world(330, 100));
  CHECK(simulation.execute_now(
            Command{.player = PlayerId::One,
                    .type = CommandType::Build,
                    .entities = {worker},
                    .target = world(400, 100),
                    .building_type = EntityType::Barracks})
            .ok);
  simulation.run(24);
  const auto site = std::ranges::find_if(
      simulation.entities(), [](const Entity& entity) {
        return entity.type == EntityType::Barracks &&
               entity.under_construction;
      });
  CHECK(site != simulation.entities().end());
  const auto site_id =
      site == simulation.entities().end() ? EntityId{} : site->id;
  CHECK(simulation.is_supply_connected(site_id));

  auto loaded = load_snapshot_v1(save_snapshot_v1(simulation));
  CHECK(loaded);
  CHECK(loaded.simulation != nullptr);
  if (loaded.simulation == nullptr) {
    return;
  }
  CHECK(loaded.simulation->is_supply_connected(site_id));
  CHECK(loaded.simulation->state_hash() == simulation.state_hash());
  simulation.run(40);
  loaded.simulation->run(40);
  CHECK(loaded.simulation->state_hash() == simulation.state_hash());
  CHECK(loaded.simulation->events() == simulation.events());
}

void replay_verifies_ledger_gated_reinforcement() {
  LedgerFixture fixture;
  ReplayRecorder recorder{fixture.simulation};
  CHECK(recorder.execute_now(
            fixture.simulation,
            Command{.player = PlayerId::One,
                    .type = CommandType::Train,
                    .producer = fixture.consumer,
                    .train_type = EntityType::Vanguard})
            .ok);
  recorder.capture_checkpoint(fixture.simulation);
  fixture.simulation.run(80);
  recorder.capture_checkpoint(fixture.simulation);
  fixture.simulation.run(90);
  const auto replay = recorder.finish(fixture.simulation);
  CHECK(!replay.expected_events.empty());
  const auto verification = verify_replay_v1(save_replay_v1(replay));
  CHECK(verification);
  CHECK(verification.simulation != nullptr);
  CHECK(verification.simulation != nullptr &&
        verification.simulation->state_hash() ==
            fixture.simulation.state_hash());
}

void replay_verifies_route_bound_construction() {
  Simulation simulation{ledger_config()};
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100)));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'250, 500)));
  const auto relay = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  const auto worker = simulation.spawn_entity(
      PlayerId::One, EntityType::Worker, world(770, 100));
  for (std::int32_t index = 0; index < 8; ++index) {
    static_cast<void>(simulation.spawn_entity(
        PlayerId::Two, EntityType::Turret,
        world(540 + index, 100 + index)));
  }

  ReplayRecorder recorder{simulation};
  CHECK(recorder.execute_now(
            simulation,
            Command{.player = PlayerId::One,
                    .type = CommandType::Build,
                    .entities = {worker},
                    .target = world(840, 100),
                    .building_type = EntityType::Turret})
            .ok);
  const auto site = std::ranges::find_if(
      simulation.entities(), [](const Entity& entity) {
        return entity.type == EntityType::Turret &&
               entity.owner == PlayerId::One && entity.under_construction;
      });
  CHECK(site != simulation.entities().end());
  const auto site_id =
      site == simulation.entities().end() ? EntityId{} : site->id;
  simulation.run(60);
  recorder.capture_checkpoint(simulation);
  simulation.run(100);
  CHECK(simulation.find_entity(relay) == nullptr);
  CHECK(simulation.find_entity(site_id) != nullptr &&
        simulation.find_entity(site_id)->under_construction);
  CHECK(!simulation.is_supply_connected(site_id));
  const auto replay = recorder.finish(simulation);
  const auto verification = verify_replay_v1(save_replay_v1(replay));
  CHECK(verification);
  CHECK(verification.simulation != nullptr);
  CHECK(verification.simulation != nullptr &&
        verification.simulation->state_hash() == simulation.state_hash());
}

void replay_verifies_retreat_rejection_after_a_route_cut() {
  Simulation simulation{ledger_config()};
  static_cast<void>(simulation.spawn_entity(
      PlayerId::One, EntityType::Command, world(100, 100)));
  static_cast<void>(simulation.spawn_entity(
      PlayerId::Two, EntityType::Command, world(1'250, 500)));
  const auto relay = simulation.spawn_entity(
      PlayerId::One, EntityType::Barracks, world(500, 100));
  const auto unit = simulation.spawn_entity(
      PlayerId::One, EntityType::Vanguard, world(840, 100));
  for (std::int32_t index = 0; index < 8; ++index) {
    static_cast<void>(simulation.spawn_entity(
        PlayerId::Two, EntityType::Turret,
        world(540 + index, 100 + index)));
  }

  ReplayRecorder recorder{simulation};
  const auto deadline = simulation.tick() + 180;
  while (simulation.find_entity(relay) != nullptr &&
         simulation.tick() < deadline) {
    simulation.step();
  }
  CHECK(simulation.find_entity(relay) == nullptr);
  const auto rejected = recorder.execute_now(
      simulation,
      Command{.player = PlayerId::One,
              .type = CommandType::Retreat,
              .entities = {unit}});
  CHECK(!rejected.ok);
  CHECK(rejected.error == CommandError::SupplyBlocked);
  recorder.capture_checkpoint(simulation);
  const auto replay = recorder.finish(simulation);
  const auto verification = verify_replay_v1(save_replay_v1(replay));
  CHECK(verification);
  CHECK(verification.simulation != nullptr);
  CHECK(verification.simulation != nullptr &&
        verification.simulation->state_hash() == simulation.state_hash());
}

}  // namespace

int main() {
  run_test("supply content is stable and validated",
           supply_content_is_stable_and_validated);
  run_test("capacity and equal-route ties are stable",
           capacity_and_equal_route_ties_are_stable);
  run_test("construction sites consume but do not relay supply",
           construction_sites_consume_but_do_not_relay_supply);
  run_test("retreat anchors use connected transmitters and stable ties",
           retreat_anchors_use_connected_transmitters_and_stable_ties);
  run_test("recovery anchors use connected transmitters and stable ties",
           recovery_anchors_use_connected_transmitters_and_stable_ties);
  run_test("relay cut and reconnect change the graph deterministically",
           relay_cut_and_reconnect_change_the_graph_deterministically);
  run_test("reinforcement legality and progress follow the route",
           reinforcement_legality_and_progress_follow_the_route);
  run_test("construction legality and progress follow the route",
           construction_legality_and_progress_follow_the_route);
  run_test("retreat legality and default destination follow the route",
           retreat_legality_and_default_destination_follow_the_route);
  run_test("non-Compact retreat behavior is unchanged",
           non_compact_retreat_behavior_is_unchanged);
  run_test("checkpoint rebuilds supply without duplicate transitions",
           checkpoint_rebuilds_supply_without_duplicate_transitions);
  run_test("checkpoint rebuilds an active construction route",
           checkpoint_rebuilds_an_active_construction_route);
  run_test("replay verifies ledger-gated reinforcement",
           replay_verifies_ledger_gated_reinforcement);
  run_test("replay verifies route-bound construction",
           replay_verifies_route_bound_construction);
  run_test("replay verifies retreat rejection after a route cut",
           replay_verifies_retreat_rejection_after_a_route_cut);
  return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
