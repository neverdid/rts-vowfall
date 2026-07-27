#include "ashen/core/Catalog.hpp"
#include "ashen/core/CommanderAI.hpp"
#include "ashen/core/Simulation.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace ashen::core;

int failures = 0;

#define CHECK(condition)                                                                                 \
  do {                                                                                                   \
    if (!(condition)) {                                                                                  \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition "\n";                    \
      ++failures;                                                                                        \
    }                                                                                                    \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  // cppcheck-suppress knownConditionTrueFalse
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name << '\n';
}

[[nodiscard]] Simulation sandbox(const FactionId one = FactionId::Compact,
                                 const FactionId two = FactionId::Ascendancy) {
  SimulationConfig config{};
  config.player_one_faction = one;
  config.player_two_faction = two;
  config.seed_starting_forces = false;
  return Simulation{config};
}

void deterministic_fixed_step() {
  Simulation first{};
  Simulation second{};
  CHECK(first.state_hash() == second.state_hash());
  first.run(240);
  second.run(240);
  CHECK(first.tick() == 240);
  CHECK(first.state_hash() == second.state_hash());
}

void starter_resource_fields_are_mirrored_and_clear_of_landmarks() {
  Simulation simulation{};
  CHECK(simulation.resources().size() >= 2);
  if (simulation.resources().size() < 2) {
    return;
  }

  const auto map_size = simulation.config().map_size;
  const auto middle_y = map_size.y / 2;
  const auto left_x = world(300, 0).x;
  const auto right_x = map_size.x - left_x;
  const auto expected_left = Vec2{left_x + world(260, 0).x, middle_y + world(180, 0).x};
  const auto expected_right = Vec2{right_x - world(260, 0).x, middle_y - world(180, 0).x};
  const auto& left_field = simulation.resources()[0];
  const auto& right_field = simulation.resources()[1];

  CHECK(left_field.position == expected_left);
  CHECK(right_field.position == expected_right);
  CHECK(left_field.position.x + right_field.position.x == map_size.x);
  CHECK(left_field.position.y + right_field.position.y == map_size.y);
  CHECK(left_field.amount == right_field.amount);
}

void movement_reaches_an_exact_target() {
  auto simulation = sandbox();
  const auto worker = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  const auto target = world(160, 100);
  const auto result = simulation.execute_now(
      Command{.player = PlayerId::One, .type = CommandType::Move, .entities = {worker}, .target = target});
  CHECK(result.ok);
  simulation.run(20);
  const auto* entity = simulation.find_entity(worker);
  CHECK(entity != nullptr);
  CHECK(entity != nullptr && entity->position == target);
  CHECK(entity != nullptr && entity->order.type == OrderType::Idle);
}

void workers_complete_repeatable_ore_trips() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto worker = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(145, 100));
  const auto resource = simulation.add_resource(world(220, 100), 100);
  const auto starting_ore = simulation.player(PlayerId::One).ore;
  const auto result = simulation.execute_now(Command{.player = PlayerId::One,
                                                      .type = CommandType::Gather,
                                                      .entities = {worker},
                                                      .resource = resource});
  CHECK(result.ok);
  simulation.run(300);
  CHECK(simulation.player(PlayerId::One).ore > starting_ore);
  CHECK(simulation.find_resource(resource)->amount < 100);
}

void production_obeys_cost_supply_and_build_time() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto barracks = simulation.spawn_entity(PlayerId::One, EntityType::Barracks, world(180, 100));
  const auto before_ore = simulation.player(PlayerId::One).ore;
  const auto definition = entity_definition(FactionId::Compact, EntityType::Vanguard);
  const auto result = simulation.execute_now(Command{.player = PlayerId::One,
                                                      .type = CommandType::Train,
                                                      .producer = barracks,
                                                      .train_type = EntityType::Vanguard});
  CHECK(result.ok);
  CHECK(simulation.player(PlayerId::One).ore == before_ore - definition.cost);
  simulation.run(definition.build_ticks);
  const auto count = std::count_if(simulation.entities().begin(), simulation.entities().end(),
                                   [](const Entity& entity) { return entity.type == EntityType::Vanguard; });
  CHECK(count == 1);
  CHECK(simulation.player(PlayerId::One).supply_used == definition.supply_cost);
}

void factions_keep_meaningful_asymmetry() {
  const auto compact = entity_definition(FactionId::Compact, EntityType::Vanguard);
  const auto ascendancy = entity_definition(FactionId::Ascendancy, EntityType::Vanguard);
  const auto concord = entity_definition(FactionId::Concord, EntityType::Vanguard);
  CHECK(ascendancy.cost > compact.cost);
  CHECK(ascendancy.hit_points > compact.hit_points);
  CHECK(ascendancy.speed_per_tick < compact.speed_per_tick);
  CHECK(concord.hit_points > ascendancy.hit_points);
  CHECK(concord.damage > compact.damage);
  CHECK(faction_definition(FactionId::Ascendancy).income_basis_points >
        faction_definition(FactionId::Concord).income_basis_points);
}

void command_destruction_ends_the_match() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto enemy_command = simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(400, 100));
  const auto attacker = simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(300, 100));
  const auto result = simulation.execute_now(Command{.player = PlayerId::One,
                                                      .type = CommandType::Attack,
                                                      .entities = {attacker},
                                                      .target_entity = enemy_command});
  CHECK(result.ok);
  simulation.run(1'200);
  CHECK(simulation.status() == MatchStatus::Won);
  CHECK(simulation.winner() == PlayerId::One);
  CHECK(simulation.find_entity(enemy_command) == nullptr);
}

void queued_commands_replay_to_the_same_hash() {
  Simulation first{};
  Simulation second{};
  const auto first_worker = std::find_if(first.entities().begin(), first.entities().end(), [](const Entity& entity) {
    return entity.owner == PlayerId::One && entity.type == EntityType::Worker;
  })->id;
  const auto second_worker = std::find_if(second.entities().begin(), second.entities().end(), [](const Entity& entity) {
    return entity.owner == PlayerId::One && entity.type == EntityType::Worker;
  })->id;
  CHECK(first_worker == second_worker);

  Command first_command{.execute_tick = 10,
                        .sequence = 7,
                        .player = PlayerId::One,
                        .type = CommandType::Move,
                        .entities = {first_worker},
                        .target = world(700, 450)};
  auto second_command = first_command;
  second_command.entities = {second_worker};
  first.enqueue(first_command);
  second.enqueue(second_command);
  first.run(400);
  second.run(400);
  CHECK(first.state_hash() == second.state_hash());
}

void same_tick_commands_have_stable_sequence_order() {
  auto simulation = sandbox();
  const auto worker = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  simulation.enqueue(Command{.execute_tick = 2,
                             .sequence = 1,
                             .player = PlayerId::One,
                             .type = CommandType::Move,
                             .entities = {worker},
                             .target = world(300, 100)});
  simulation.enqueue(Command{.execute_tick = 2,
                             .sequence = 2,
                             .player = PlayerId::One,
                             .type = CommandType::Move,
                             .entities = {worker},
                             .target = world(100, 300)});
  simulation.run(80);
  CHECK(simulation.find_entity(worker)->position == world(100, 300));
}

void state_hash_covers_queued_command_payloads() {
  Simulation first{};
  Simulation second{};
  const auto worker = std::find_if(first.entities().begin(), first.entities().end(), [](const Entity& entity) {
    return entity.owner == PlayerId::One && entity.type == EntityType::Worker;
  })->id;
  first.enqueue(Command{.execute_tick = 50,
                        .sequence = 1,
                        .player = PlayerId::One,
                        .type = CommandType::Move,
                        .entities = {worker},
                        .target = world(600, 400)});
  second.enqueue(Command{.execute_tick = 50,
                         .sequence = 1,
                         .player = PlayerId::One,
                         .type = CommandType::Move,
                         .entities = {worker},
                         .target = world(700, 400)});
  CHECK(first.state_hash() != second.state_hash());
}

void navigation_routes_through_the_visible_bridge() {
  auto simulation = sandbox();
  const auto unit = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(1'000, 700));
  const auto target = world(1'400, 700);
  CHECK(simulation.execute_now(
            Command{.player = PlayerId::One, .type = CommandType::Move, .entities = {unit}, .target = target})
            .ok);

  bool crossed_river = false;
  for (int tick = 0; tick < 500; ++tick) {
    simulation.step();
    const auto* entity = simulation.find_entity(unit);
    CHECK(entity != nullptr);
    if (entity == nullptr) {
      break;
    }
    if (entity->position.x >= world(1'115, 0).x && entity->position.x <= world(1'285, 0).x) {
      crossed_river = true;
      CHECK(entity->position.y > world(635, 0).x && entity->position.y < world(765, 0).x);
    }
  }

  CHECK(crossed_river);
  CHECK(simulation.find_entity(unit)->position == target);
}

void strategic_flanks_are_distinct_and_navigable() {
  auto simulation = sandbox();
  const auto mountain_scout =
      simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(300, 700));
  const auto gravewood_scout =
      simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(2'100, 700));

  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {mountain_scout},
                                       .target = world(620, 170)})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {mountain_scout},
                                       .target = world(1'500, 380),
                                       .queue = true})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {gravewood_scout},
                                       .target = world(1'780, 1'230)})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {gravewood_scout},
                                       .target = world(900, 1'020),
                                       .queue = true})
            .ok);

  bool used_mountain_pass = false;
  bool used_north_crossing = false;
  bool entered_gravewood = false;
  bool used_south_crossing = false;
  for (int tick = 0; tick < 2'400; ++tick) {
    simulation.step();
    const auto* north = simulation.find_entity(mountain_scout);
    const auto* south = simulation.find_entity(gravewood_scout);
    CHECK(north != nullptr);
    CHECK(south != nullptr);
    if (north == nullptr || south == nullptr) {
      break;
    }

    used_mountain_pass = used_mountain_pass ||
                         (north->position.x >= world(500, 0).x &&
                          north->position.x <= world(900, 0).x && north->position.y < world(250, 0).x);
    used_north_crossing = used_north_crossing ||
                          (north->position.x >= world(1'115, 0).x &&
                           north->position.x <= world(1'285, 0).x &&
                           north->position.y > world(315, 0).x && north->position.y < world(445, 0).x);
    entered_gravewood = entered_gravewood ||
                        (south->position.x >= world(1'580, 0).x &&
                         south->position.x <= world(1'900, 0).x && south->position.y > world(1'150, 0).x);
    used_south_crossing = used_south_crossing ||
                          (south->position.x >= world(1'115, 0).x &&
                           south->position.x <= world(1'285, 0).x &&
                           south->position.y > world(955, 0).x && south->position.y < world(1'085, 0).x);
  }

  CHECK(used_mountain_pass);
  CHECK(used_north_crossing);
  CHECK(entered_gravewood);
  CHECK(used_south_crossing);
  CHECK(simulation.find_entity(mountain_scout)->position == world(1'500, 380));
  CHECK(simulation.find_entity(gravewood_scout)->position == world(900, 1'020));
}

void formation_orders_assign_distinct_non_overlapping_slots() {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.navigation_obstacles.clear();
  Simulation simulation{config};
  std::vector<EntityId> units;
  for (int index = 0; index < 6; ++index) {
    units.push_back(simulation.spawn_entity(PlayerId::One, EntityType::Worker,
                                            world(100 + index * 18, 100)));
  }
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = units,
                                       .target = world(520, 420)})
            .ok);

  std::set<std::pair<std::int32_t, std::int32_t>> destinations;
  for (const auto id : units) {
    const auto* entity = simulation.find_entity(id);
    CHECK(entity != nullptr);
    if (entity != nullptr) {
      destinations.emplace(entity->order.target.x, entity->order.target.y);
    }
  }
  CHECK(destinations.size() == units.size());

  simulation.run(300);
  for (std::size_t left = 0; left < units.size(); ++left) {
    const auto* left_entity = simulation.find_entity(units[left]);
    CHECK(left_entity != nullptr && left_entity->order.type == OrderType::Idle);
    for (std::size_t right = left + 1; right < units.size(); ++right) {
      const auto* right_entity = simulation.find_entity(units[right]);
      if (left_entity == nullptr || right_entity == nullptr) {
        continue;
      }
      const auto dx = static_cast<std::int64_t>(left_entity->position.x) - right_entity->position.x;
      const auto dy = static_cast<std::int64_t>(left_entity->position.y) - right_entity->position.y;
      const auto required = static_cast<std::int64_t>(left_entity->radius + right_entity->radius);
      CHECK(dx * dx + dy * dy >= required * required);
    }
  }
}

void shift_queued_orders_execute_in_sequence() {
  auto simulation = sandbox();
  const auto unit = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {unit},
                                       .target = world(180, 100)})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {unit},
                                       .target = world(180, 220),
                                       .queue = true})
            .ok);
  CHECK(simulation.find_entity(unit)->order_queue.size() == 1);
  simulation.run(100);
  CHECK(simulation.find_entity(unit)->position == world(180, 220));
  CHECK(simulation.find_entity(unit)->order.type == OrderType::Idle);
}

void attack_move_engages_then_resumes_the_march() {
  auto simulation = sandbox();
  const auto attacker = simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(100, 100));
  const auto enemy = simulation.spawn_entity(PlayerId::Two, EntityType::Worker, world(250, 100));
  const auto destination = world(430, 100);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::AttackMove,
                                       .entities = {attacker},
                                       .target = destination})
            .ok);
  simulation.run(300);
  CHECK(simulation.find_entity(enemy) == nullptr);
  CHECK(simulation.find_entity(attacker)->position == destination);
  CHECK(simulation.find_entity(attacker)->order.type == OrderType::Idle);
}

void hold_position_fights_without_chasing() {
  auto simulation = sandbox();
  const auto defender = simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(100, 100));
  const auto enemy = simulation.spawn_entity(PlayerId::Two, EntityType::Worker, world(145, 100));
  const auto starting_position = simulation.find_entity(defender)->position;
  CHECK(simulation.execute_now(
            Command{.player = PlayerId::One, .type = CommandType::Hold, .entities = {defender}})
            .ok);
  simulation.run(120);
  CHECK(simulation.find_entity(defender)->position == starting_position);
  CHECK(simulation.find_entity(defender)->order.type == OrderType::Hold);
  CHECK(simulation.find_entity(enemy) == nullptr);
}

void stop_clears_movement_and_queued_orders() {
  auto simulation = sandbox();
  const auto unit = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {unit},
                                       .target = world(600, 100)})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {unit},
                                       .target = world(600, 300),
                                       .queue = true})
            .ok);
  simulation.run(5);
  CHECK(simulation.execute_now(
            Command{.player = PlayerId::One, .type = CommandType::Stop, .entities = {unit}})
            .ok);
  const auto stopped_at = simulation.find_entity(unit)->position;
  simulation.run(100);
  CHECK(simulation.find_entity(unit)->position == stopped_at);
  CHECK(simulation.find_entity(unit)->order.type == OrderType::Idle);
  CHECK(simulation.find_entity(unit)->order_queue.empty());
}

void patrol_reverses_and_remains_active() {
  auto simulation = sandbox();
  const auto unit = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Patrol,
                                       .entities = {unit},
                                       .target = world(220, 100)})
            .ok);

  bool moved_back = false;
  auto previous_x = simulation.find_entity(unit)->position.x;
  for (int tick = 0; tick < 100; ++tick) {
    simulation.step();
    const auto current_x = simulation.find_entity(unit)->position.x;
    moved_back = moved_back || current_x < previous_x;
    previous_x = current_x;
  }
  CHECK(moved_back);
  CHECK(simulation.find_entity(unit)->order.type == OrderType::Patrol);
}

void rally_point_controls_completed_production() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto barracks = simulation.spawn_entity(PlayerId::One, EntityType::Barracks, world(180, 100));
  const auto rally = world(320, 180);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::SetRallyPoint,
                                       .target = rally,
                                       .producer = barracks})
            .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Train,
                                       .producer = barracks,
                                       .train_type = EntityType::Vanguard})
            .ok);
  simulation.run(entity_definition(FactionId::Compact, EntityType::Vanguard).build_ticks);
  const auto trained = std::find_if(simulation.entities().begin(), simulation.entities().end(),
                                    [](const Entity& entity) { return entity.type == EntityType::Vanguard; });
  CHECK(trained != simulation.entities().end());
  CHECK(trained != simulation.entities().end() && trained->order.type == OrderType::Move);
  simulation.run(100);
  const auto arrived = std::find_if(simulation.entities().begin(), simulation.entities().end(),
                                    [](const Entity& entity) { return entity.type == EntityType::Vanguard; });
  CHECK(arrived != simulation.entities().end() && arrived->position == rally);
}

void worker_construction_is_blocked_costed_and_completed() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto worker = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(155, 100));
  const auto opening_ore = simulation.player(PlayerId::One).ore;
  const auto opening_cap = simulation.player(PlayerId::One).supply_cap;

  CHECK(!simulation.execute_now(Command{.player = PlayerId::One,
                                        .type = CommandType::Build,
                                        .entities = {worker},
                                        .target = world(115, 100),
                                        .building_type = EntityType::Barracks})
             .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Build,
                                       .entities = {worker},
                                       .target = world(310, 180),
                                       .building_type = EntityType::Barracks})
            .ok);
  CHECK(simulation.player(PlayerId::One).ore ==
        opening_ore - entity_definition(FactionId::Compact, EntityType::Barracks).cost);
  const auto site = std::find_if(simulation.entities().begin(), simulation.entities().end(),
                                 [](const Entity& entity) { return entity.type == EntityType::Barracks; });
  CHECK(site != simulation.entities().end());
  CHECK(site != simulation.entities().end() && site->under_construction);
  CHECK(simulation.player(PlayerId::One).supply_cap == opening_cap);

  simulation.run(380);
  const auto completed = std::find_if(simulation.entities().begin(), simulation.entities().end(),
                                      [](const Entity& entity) { return entity.type == EntityType::Barracks; });
  CHECK(completed != simulation.entities().end() && !completed->under_construction);
  CHECK(completed != simulation.entities().end() && completed->hit_points == completed->max_hit_points);
  CHECK(simulation.player(PlayerId::One).supply_cap == opening_cap + completed->supply_provided);
}

void orphaned_construction_can_be_resumed_without_paying_twice() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto first_worker =
      simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(155, 100));
  const auto replacement_worker =
      simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(170, 150));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Build,
                                       .entities = {first_worker},
                                       .target = world(310, 180),
                                       .building_type = EntityType::Barracks})
            .ok);
  const auto site = std::ranges::find_if(simulation.entities(), [](const Entity& entity) {
    return entity.type == EntityType::Barracks && entity.under_construction;
  });
  CHECK(site != simulation.entities().end());
  const auto site_id = site == simulation.entities().end() ? EntityId{} : site->id;
  const auto ore_after_purchase = simulation.player(PlayerId::One).ore;
  simulation.run(40);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Stop,
                                       .entities = {first_worker}})
            .ok);

  const auto orphaned = simulation.observe(PlayerId::One);
  CHECK(orphaned.permits(CommandType::Build, replacement_worker, EntityType::Barracks));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Build,
                                       .entities = {replacement_worker},
                                       .target_entity = site_id,
                                       .building_type = EntityType::Barracks})
            .ok);
  CHECK(simulation.player(PlayerId::One).ore == ore_after_purchase);
  CHECK(!simulation.execute_now(Command{.player = PlayerId::One,
                                        .type = CommandType::Build,
                                        .entities = {first_worker},
                                        .target_entity = site_id,
                                        .building_type = EntityType::Barracks})
             .ok);

  simulation.run(420);
  const auto* completed = simulation.find_entity(site_id);
  CHECK(completed != nullptr && !completed->under_construction);
  CHECK(completed != nullptr && completed->hit_points == completed->max_hit_points);
}

void research_unlocks_units_and_refreshes_existing_troops() {
  auto simulation = sandbox();
  const auto command = simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100));
  const auto barracks = simulation.spawn_entity(PlayerId::One, EntityType::Barracks, world(220, 100));
  const auto soldier = simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(160, 180));
  const auto worker = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(150, 100));
  const auto seam = simulation.add_resource(world(185, 100), 500);

  CHECK(!simulation.execute_now(Command{.player = PlayerId::One,
                                        .type = CommandType::Train,
                                        .producer = barracks,
                                        .train_type = EntityType::Skirmisher})
             .ok);
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Research,
                                       .producer = command,
                                       .research = ResearchId::TierTwo})
            .ok);
  CHECK(!simulation.execute_now(Command{.player = PlayerId::One,
                                        .type = CommandType::Research,
                                        .producer = command,
                                        .research = ResearchId::TierTwo})
             .ok);
  simulation.run(research_definition(ResearchId::TierTwo).research_ticks);
  CHECK(simulation.player(PlayerId::One).tech_tier == 2);
  CHECK(simulation.has_research(PlayerId::One, ResearchId::TierTwo));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Train,
                                       .producer = barracks,
                                       .train_type = EntityType::Skirmisher})
            .ok);

  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Gather,
                                       .entities = {worker},
                                       .resource = seam})
            .ok);
  simulation.run(500);
  const auto before = simulation.find_entity(soldier);
  CHECK(before != nullptr);
  const auto health_before = before == nullptr ? 0 : before->max_hit_points;
  const auto damage_before = before == nullptr ? 0 : before->damage;
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Research,
                                       .producer = barracks,
                                       .research = ResearchId::TemperedOaths})
            .ok);
  simulation.run(research_definition(ResearchId::TemperedOaths).research_ticks);
  const auto upgraded = simulation.find_entity(soldier);
  CHECK(upgraded != nullptr && upgraded->max_hit_points > health_before);
  CHECK(upgraded != nullptr && upgraded->damage > damage_before);
}

void faction_powers_are_distinct_and_obey_cooldowns() {
  auto compact = sandbox(FactionId::Compact, FactionId::Ascendancy);
  static_cast<void>(compact.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto compact_unit = compact.spawn_entity(PlayerId::One, EntityType::Vanguard, world(170, 100));
  const auto compact_enemy = compact.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(220, 100));
  CHECK(compact.execute_now(Command{.player = PlayerId::Two,
                                    .type = CommandType::Attack,
                                    .entities = {compact_enemy},
                                    .target_entity = compact_unit})
            .ok);
  compact.step();
  const auto damaged_health = compact.find_entity(compact_unit)->hit_points;
  CHECK(compact.execute_now(Command{.player = PlayerId::One, .type = CommandType::ActivatePower}).ok);
  CHECK(compact.find_entity(compact_unit)->hit_points > damaged_health);
  CHECK(!compact.execute_now(Command{.player = PlayerId::One, .type = CommandType::ActivatePower}).ok);

  auto ascendancy = sandbox(FactionId::Ascendancy, FactionId::Compact);
  static_cast<void>(ascendancy.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto before_count = std::ranges::count_if(ascendancy.entities(), [](const Entity& entity) {
    return entity.owner == PlayerId::One && entity.type == EntityType::Vanguard;
  });
  CHECK(ascendancy.execute_now(Command{.player = PlayerId::One, .type = CommandType::ActivatePower}).ok);
  CHECK(std::ranges::count_if(ascendancy.entities(), [](const Entity& entity) {
          return entity.owner == PlayerId::One && entity.type == EntityType::Vanguard;
        }) == before_count + 1);

  auto concord = sandbox(FactionId::Concord, FactionId::Ascendancy);
  const auto concord_command = concord.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100));
  const auto concord_enemy = concord.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(170, 100));
  CHECK(concord.execute_now(Command{.player = PlayerId::Two,
                                    .type = CommandType::Attack,
                                    .entities = {concord_enemy},
                                    .target_entity = concord_command})
            .ok);
  concord.step();
  const auto damaged_command = concord.find_entity(concord_command)->hit_points;
  CHECK(concord.execute_now(Command{.player = PlayerId::One, .type = CommandType::ActivatePower}).ok);
  CHECK(concord.find_entity(concord_command)->hit_points > damaged_command);
}

void control_points_capture_and_generate_income() {
  auto simulation = sandbox();
  const auto point = simulation.add_control_point(world(320, 260), 90'000);
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(320, 260)));
  const auto opening_ore = simulation.player(PlayerId::One).ore;
  simulation.run(170);
  const auto* captured = simulation.find_control_point(point);
  CHECK(captured != nullptr && captured->owner == PlayerId::One);
  CHECK(captured != nullptr && captured->influence == 10'000);
  CHECK(simulation.player(PlayerId::One).ore > opening_ore);

  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(320, 260)));
  const auto frozen_influence = simulation.find_control_point(point)->influence;
  simulation.run(80);
  CHECK(simulation.find_control_point(point)->influence == frozen_influence);
}

void fog_of_war_tracks_hidden_explored_and_visible_ground() {
  auto simulation = sandbox();
  const auto scout = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  const auto distant_ground = world(800, 100);

  CHECK(simulation.visibility_state_at(distant_ground, PlayerId::One) == VisibilityState::Hidden);
  CHECK(!simulation.is_position_visible_to(distant_ground, PlayerId::One));
  CHECK(simulation.visibility(PlayerId::One).cells().size() ==
        static_cast<std::size_t>(simulation.visibility(PlayerId::One).columns()) *
            static_cast<std::size_t>(simulation.visibility(PlayerId::One).rows()));

  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {scout},
                                       .target = distant_ground})
            .ok);
  simulation.run(220);
  CHECK(simulation.visibility_state_at(distant_ground, PlayerId::One) == VisibilityState::Visible);

  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {scout},
                                       .target = world(100, 100)})
            .ok);
  simulation.run(220);
  CHECK(simulation.visibility_state_at(distant_ground, PlayerId::One) == VisibilityState::Explored);
  CHECK(!simulation.is_position_visible_to(distant_ground, PlayerId::One));
  CHECK(simulation.visibility_state_at(world(2'500, 1'500), PlayerId::One) == VisibilityState::Hidden);
  CHECK(!simulation.is_position_visible_to(world(2'500, 1'500), PlayerId::One));
}

void hidden_targets_cannot_be_commanded_or_pursued() {
  auto simulation = sandbox();
  const auto attacker = simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(100, 700));
  const auto target = simulation.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(1'250, 700));

  const auto hidden_attack = simulation.execute_now(Command{.player = PlayerId::One,
                                                             .type = CommandType::Attack,
                                                             .entities = {attacker},
                                                             .target_entity = target});
  CHECK(!hidden_attack.ok);
  CHECK(hidden_attack.error == CommandError::InvalidTarget);
  CHECK(simulation.visible_enemy_ids(PlayerId::One).empty());

  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(1'000, 700)));
  CHECK(simulation.is_entity_visible_to(*simulation.find_entity(target), PlayerId::One));
  const auto visible_enemies = simulation.visible_enemy_ids(PlayerId::One);
  CHECK(std::ranges::find(visible_enemies, target) != visible_enemies.end());
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Attack,
                                       .entities = {attacker},
                                       .target_entity = target})
            .ok);

  CHECK(simulation.execute_now(Command{.player = PlayerId::Two,
                                       .type = CommandType::Move,
                                       .entities = {target},
                                       .target = world(1'800, 700)})
            .ok);
  simulation.run(80);

  const auto* escaped = simulation.find_entity(target);
  const auto* pursuer = simulation.find_entity(attacker);
  CHECK(escaped != nullptr && !simulation.is_entity_visible_to(*escaped, PlayerId::One));
  CHECK(escaped != nullptr && escaped->hit_points == escaped->max_hit_points);
  CHECK(pursuer != nullptr && pursuer->order.type == OrderType::Idle);
  CHECK(simulation.visible_enemy_ids(PlayerId::One).empty());
}

void autonomous_orders_ignore_enemies_outside_current_vision() {
  SimulationConfig config{};
  config.seed_starting_forces = false;
  config.visibility_cell_size = world(4, 0).x;
  Simulation simulation{config};
  const auto attacker = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  const auto hidden_enemy = simulation.spawn_entity(PlayerId::Two, EntityType::Worker, world(325, 100));
  const auto enemy_health = simulation.find_entity(hidden_enemy)->hit_points;

  CHECK(!simulation.is_entity_visible_to(*simulation.find_entity(hidden_enemy), PlayerId::One));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::AttackMove,
                                       .entities = {attacker},
                                       .target = world(500, 100)})
            .ok);
  simulation.step();

  const auto* advancing = simulation.find_entity(attacker);
  CHECK(advancing != nullptr && advancing->order.type == OrderType::AttackMove);
  CHECK(advancing != nullptr && !advancing->order.target_entity);
  CHECK(simulation.find_entity(hidden_enemy)->hit_points == enemy_health);
}

void tide_resolve_and_visibility_are_authoritative() {
  auto simulation = sandbox(FactionId::Compact, FactionId::Ascendancy);
  const auto scout = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  const auto soldier = simulation.spawn_entity(PlayerId::One, EntityType::Vanguard, world(500, 500));
  static_cast<void>(simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(535, 500)));
  const auto distant_enemy = simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(1'000, 700));

  CHECK(!simulation.is_entity_visible_to(*simulation.find_entity(distant_enemy), PlayerId::One));
  CHECK(simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {scout},
                                       .target = world(900, 700)})
            .ok);
  simulation.run(220);
  CHECK(simulation.is_entity_visible_to(*simulation.find_entity(distant_enemy), PlayerId::One));

  const auto opening_tide = simulation.ruin_tide();
  simulation.run(480);
  CHECK(simulation.ruin_tide() > opening_tide);
  const auto* resolved = simulation.find_entity(soldier);
  CHECK(resolved != nullptr && resolved->resolve < 100);
  CHECK(simulation.player(PlayerId::One).resolve < 100);
}

void observations_contain_only_seen_or_remembered_facts() {
  auto simulation = sandbox();
  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Command, world(100, 100)));
  const auto scout = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(140, 100));
  const auto enemy_command = simulation.spawn_entity(PlayerId::Two, EntityType::Command, world(1'000, 700));
  static_cast<void>(simulation.add_resource(world(180, 100), 600));
  const auto distant_resource = simulation.add_resource(world(1'040, 700), 900);
  const auto objective = simulation.add_control_point(world(930, 700));

  const auto opening = simulation.observe(PlayerId::One);
  CHECK(opening.player() == PlayerId::One);
  CHECK(opening.owned_entities().size() == 2);
  CHECK(opening.known_enemies().empty());
  CHECK(opening.known_resources().size() == 1);
  CHECK(opening.public_objectives().size() == 1);
  CHECK(!opening.public_objectives().front().has_observed_state);
  CHECK(opening.permits(CommandType::Move, scout));
  CHECK(!opening.permits(CommandType::Attack, scout));

  static_cast<void>(simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(890, 700)));
  const auto revealed = simulation.observe(PlayerId::One);
  const auto seen_enemy = std::ranges::find(revealed.known_enemies(), enemy_command, &ObservedEnemy::id);
  CHECK(seen_enemy != revealed.known_enemies().end());
  CHECK(seen_enemy != revealed.known_enemies().end() && seen_enemy->currently_visible);
  CHECK(std::ranges::any_of(revealed.known_resources(), [distant_resource](const ObservedResource& resource) {
    return resource.id == distant_resource && resource.visibility == VisibilityState::Visible;
  }));
  const auto seen_objective = std::ranges::find(revealed.public_objectives(), objective,
                                                &ObservedControlPoint::id);
  CHECK(seen_objective != revealed.public_objectives().end() && seen_objective->has_observed_state);

  const auto distant_scout = std::ranges::find_if(simulation.entities(), [](const Entity& entity) {
    return entity.owner == PlayerId::One && entity.type == EntityType::Worker && entity.position == world(890, 700);
  });
  CHECK(distant_scout != simulation.entities().end());
  CHECK(distant_scout != simulation.entities().end() &&
        simulation.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Move,
                                       .entities = {distant_scout->id},
                                       .target = world(140, 100)})
            .ok);
  simulation.run(260);

  const auto remembered = simulation.observe(PlayerId::One);
  const auto remembered_enemy = std::ranges::find(remembered.known_enemies(), enemy_command,
                                                  &ObservedEnemy::id);
  CHECK(remembered_enemy != remembered.known_enemies().end());
  CHECK(remembered_enemy != remembered.known_enemies().end() && !remembered_enemy->currently_visible);
  CHECK(remembered_enemy != remembered.known_enemies().end() &&
        remembered_enemy->last_observed_tick < remembered.tick());
  CHECK(std::ranges::any_of(remembered.known_resources(),
                            [distant_resource](const ObservedResource& resource) {
                              return resource.id == distant_resource &&
                                     resource.visibility == VisibilityState::Explored &&
                                     resource.last_observed_amount == 900;
                            }));
  CHECK(!remembered.permits(CommandType::Attack, scout));
}

void hidden_state_cannot_change_a_commander_decision() {
  Simulation baseline{};
  Simulation perturbed{};
  baseline.run(160);
  perturbed.run(160);

  const auto hidden_worker = std::ranges::find_if(perturbed.entities(), [](const Entity& entity) {
    return entity.owner == PlayerId::Two && entity.type == EntityType::Worker;
  });
  CHECK(hidden_worker != perturbed.entities().end());
  CHECK(hidden_worker != perturbed.entities().end() &&
        !perturbed.is_entity_visible_to(*hidden_worker, PlayerId::One));
  CHECK(hidden_worker != perturbed.entities().end() &&
        perturbed.execute_now(Command{.player = PlayerId::Two,
                                      .type = CommandType::Move,
                                      .entities = {hidden_worker->id},
                                      .target = world(1'700, 1'100)})
            .ok);

  CHECK(baseline.state_hash() != perturbed.state_hash());
  const auto baseline_observation = baseline.observe(PlayerId::One);
  const auto perturbed_observation = perturbed.observe(PlayerId::One);
  CHECK(baseline_observation.hash() == perturbed_observation.hash());

  const CommanderAI commander{PlayerId::One};
  const auto baseline_plan = commander.plan(baseline_observation);
  const auto perturbed_plan = commander.plan(perturbed_observation);
  CHECK(!baseline_plan.decisions.empty());
  CHECK(baseline_plan == perturbed_plan);
}

void commander_commands_use_the_normal_validation_path() {
  Simulation simulation{};
  simulation.step();
  const CommanderAI commander{PlayerId::One};
  const auto commands = commander.decide(simulation.observe(PlayerId::One));
  CHECK(commands.size() == 2);
  for (const auto& command : commands) {
    const auto result = simulation.execute_now(command);
    CHECK(result.ok);
  }
  CHECK(std::ranges::any_of(simulation.entities(), [](const Entity& entity) {
    return entity.owner == PlayerId::One && entity.type == EntityType::Barracks && entity.under_construction;
  }));
}

void core_commanders_can_own_both_players_and_queue_actions() {
  SimulationConfig config{};
  config.commander_players = {true, true};
  Simulation simulation{config};

  simulation.step();
  CHECK(simulation.tick() == 1);
  CHECK(simulation.entities().size() == 10);
  simulation.step();
  CHECK(simulation.tick() == 2);
  CHECK(std::ranges::count_if(simulation.entities(), [](const Entity& entity) {
          return entity.type == EntityType::Barracks && entity.under_construction;
        }) == 2);
  CHECK(std::ranges::any_of(simulation.entities(), [](const Entity& entity) {
    return entity.owner == PlayerId::One && entity.type == EntityType::Barracks;
  }));
  CHECK(std::ranges::any_of(simulation.entities(), [](const Entity& entity) {
    return entity.owner == PlayerId::Two && entity.type == EntityType::Barracks;
  }));
  CHECK(!simulation.command_trace().empty());
  CHECK(std::ranges::all_of(simulation.command_trace(), [](const CommandTraceEntry& entry) {
    return entry.source == CommandSource::CommanderAI && entry.observation_hash != 0 &&
           entry.ai_decision_id != 0 && entry.accepted && entry.issued_tick <= entry.applied_tick;
  }));
  CHECK(simulation.ai_decision_trace().size() == simulation.command_trace().size());
  CHECK(std::ranges::all_of(simulation.ai_decision_trace(), [](const AIDecisionRecord& record) {
    return record.id != 0 && record.observation_hash != 0 && !record.candidates.empty() &&
           record.selected_candidate < record.candidates.size() &&
           record.command_status == AICommandStatus::Accepted && record.command_sequence != 0 &&
           record.applied_tick >= record.observation_tick;
  }));
}

void command_trace_records_external_provenance_and_validation() {
  auto simulation = sandbox();
  const auto worker = simulation.spawn_entity(PlayerId::One, EntityType::Worker, world(100, 100));
  simulation.enqueue(Command{.execute_tick = 2,
                             .player = PlayerId::One,
                             .type = CommandType::Move,
                             .entities = {worker},
                             .target = world(180, 100)});
  simulation.run(3);
  const auto rejected = simulation.execute_now(Command{.player = PlayerId::One,
                                                        .type = CommandType::Move,
                                                        .entities = {EntityId{99'999}},
                                                        .target = world(220, 100)});

  CHECK(!rejected.ok);
  CHECK(simulation.command_trace().size() == 2);
  const auto& applied = simulation.command_trace()[0];
  CHECK(applied.source == CommandSource::External);
  CHECK(applied.observation_hash == 0);
  CHECK(applied.ai_decision_id == 0);
  CHECK(applied.issued_tick == 0);
  CHECK(applied.applied_tick == 2);
  CHECK(applied.accepted);
  const auto& invalid = simulation.command_trace()[1];
  CHECK(invalid.source == CommandSource::External);
  CHECK(!invalid.accepted);
  CHECK(invalid.error == CommandError::InvalidEntity);
}

void core_bots_finish_a_deterministic_headless_match() {
  SimulationConfig config{};
  config.commander_players = {true, true};
  Simulation first{config};
  Simulation second{config};

  constexpr Tick maximum_match_ticks = 60'000;
  while (first.status() == MatchStatus::Playing && first.tick() < maximum_match_ticks) {
    first.step();
    second.step();
    if (first.tick() % 1'000 == 0) {
      CHECK(first.state_hash() == second.state_hash());
    }
  }

  CHECK(first.status() != MatchStatus::Playing);
  CHECK(first.winner().has_value());
  CHECK(first.tick() == second.tick());
  CHECK(first.winner() == second.winner());
  CHECK(first.state_hash() == second.state_hash());
  CHECK(first.command_trace() == second.command_trace());
  CHECK(first.ai_decision_trace() == second.ai_decision_trace());

  std::array<bool, 3> observed_layers{};
  for (const auto& decision : first.ai_decision_trace()) {
    observed_layers[static_cast<std::size_t>(decision.layer)] = true;
    CHECK(decision.command_status != AICommandStatus::Queued);
    CHECK(decision.selected_candidate < decision.candidates.size());
    const auto command = std::ranges::find(first.command_trace(), decision.id,
                                           &CommandTraceEntry::ai_decision_id);
    CHECK(command != first.command_trace().end());
    CHECK(command != first.command_trace().end() &&
          command->command.sequence == decision.command_sequence);
    CHECK(command != first.command_trace().end() &&
          command->accepted == (decision.command_status == AICommandStatus::Accepted));
  }
  CHECK(std::ranges::all_of(observed_layers, [](const bool observed) { return observed; }));
}

}  // namespace

int main() {
  run_test("deterministic fixed step", deterministic_fixed_step);
  run_test("starter resource fields are mirrored and clear of landmarks",
           starter_resource_fields_are_mirrored_and_clear_of_landmarks);
  run_test("movement reaches an exact target", movement_reaches_an_exact_target);
  run_test("workers complete repeatable ore trips", workers_complete_repeatable_ore_trips);
  run_test("production obeys cost, supply, and build time", production_obeys_cost_supply_and_build_time);
  run_test("factions keep meaningful asymmetry", factions_keep_meaningful_asymmetry);
  run_test("command destruction ends the match", command_destruction_ends_the_match);
  run_test("queued commands replay to the same hash", queued_commands_replay_to_the_same_hash);
  run_test("same-tick commands have stable sequence order", same_tick_commands_have_stable_sequence_order);
  run_test("state hash covers queued command payloads", state_hash_covers_queued_command_payloads);
  run_test("navigation routes through the visible bridge", navigation_routes_through_the_visible_bridge);
  run_test("strategic flanks are distinct and navigable", strategic_flanks_are_distinct_and_navigable);
  run_test("formation orders assign distinct non-overlapping slots",
           formation_orders_assign_distinct_non_overlapping_slots);
  run_test("shift-queued orders execute in sequence", shift_queued_orders_execute_in_sequence);
  run_test("attack-move engages then resumes the march", attack_move_engages_then_resumes_the_march);
  run_test("hold position fights without chasing", hold_position_fights_without_chasing);
  run_test("stop clears movement and queued orders", stop_clears_movement_and_queued_orders);
  run_test("patrol reverses and remains active", patrol_reverses_and_remains_active);
  run_test("rally point controls completed production", rally_point_controls_completed_production);
  run_test("worker construction is blocked, costed, and completed",
           worker_construction_is_blocked_costed_and_completed);
  run_test("orphaned construction can be resumed without paying twice",
           orphaned_construction_can_be_resumed_without_paying_twice);
  run_test("research unlocks units and refreshes existing troops",
           research_unlocks_units_and_refreshes_existing_troops);
  run_test("faction powers are distinct and obey cooldowns", faction_powers_are_distinct_and_obey_cooldowns);
  run_test("control points capture and generate income", control_points_capture_and_generate_income);
  run_test("fog of war tracks hidden, explored, and visible ground",
           fog_of_war_tracks_hidden_explored_and_visible_ground);
  run_test("hidden targets cannot be commanded or pursued", hidden_targets_cannot_be_commanded_or_pursued);
  run_test("autonomous orders ignore enemies outside current vision",
           autonomous_orders_ignore_enemies_outside_current_vision);
  run_test("Tide, resolve, and visibility are authoritative", tide_resolve_and_visibility_are_authoritative);
  run_test("observations contain only seen or remembered facts",
           observations_contain_only_seen_or_remembered_facts);
  run_test("hidden state cannot change a commander decision",
           hidden_state_cannot_change_a_commander_decision);
  run_test("commander commands use the normal validation path",
           commander_commands_use_the_normal_validation_path);
  run_test("core commanders can own both players and queue actions",
           core_commanders_can_own_both_players_and_queue_actions);
  run_test("command trace records external provenance and validation",
           command_trace_records_external_provenance_and_validation);
  run_test("core bots finish a deterministic headless match",
           core_bots_finish_a_deterministic_headless_match);

  if (failures != 0) {
    std::cerr << failures << " native simulation check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All native simulation checks passed.\n";
  return EXIT_SUCCESS;
}
