#include "ashen/core/Simulation.hpp"

#include "ashen/core/Catalog.hpp"
#include "ashen/core/Content.hpp"
#include "ashen/core/ResolveSystem.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <numeric>
#include <tuple>
#include <utility>
#include <vector>

namespace ashen::core {
namespace {

inline constexpr Tick kHarvestTicks = 22;
inline constexpr std::int32_t kOrePerTrip = 10;
inline constexpr std::int32_t kAttackMoveAcquisitionRange = 220'000;
inline constexpr std::int32_t kSeparationPadding = 2'000;
inline constexpr Tick kRuinTidePeriodTicks = 92 * kTicksPerSecond;
inline constexpr std::int32_t kCaptureMaximum = 10'000;
inline constexpr std::int32_t kControlIncomeThreshold = 2'000;
inline constexpr std::int32_t kControlIncomePerTick = 155;
inline constexpr std::int32_t kConstructionReach = 12'000;
inline constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;
inline constexpr std::size_t kInvalidEntityIndex =
    std::numeric_limits<std::size_t>::max();

[[nodiscard]] CommandResult success() noexcept {
  return {true, CommandError::None, {}};
}

[[nodiscard]] CommandResult failure(const CommandError error, const std::string_view reason) noexcept {
  return {false, error, reason};
}

[[nodiscard]] std::uint64_t squared_distance(const Vec2 a, const Vec2 b) noexcept {
  const auto dx = static_cast<std::int64_t>(b.x) - static_cast<std::int64_t>(a.x);
  const auto dy = static_cast<std::int64_t>(b.y) - static_cast<std::int64_t>(a.y);
  return static_cast<std::uint64_t>(dx * dx + dy * dy);
}

[[nodiscard]] std::uint64_t integer_sqrt(const std::uint64_t value) noexcept {
  if (value == 0) {
    return 0;
  }

  auto estimate = value;
  auto next = (estimate + 1) / 2;
  while (next < estimate) {
    estimate = next;
    next = (estimate + value / estimate) / 2;
  }
  return estimate;
}

[[nodiscard]] bool within_reach(const Entity& entity, const Vec2 target, const std::int32_t target_radius,
                                const std::int32_t extra_range = 0) noexcept {
  const auto reach = static_cast<std::int64_t>(entity.radius) + target_radius + extra_range;
  return squared_distance(entity.position, target) <= static_cast<std::uint64_t>(reach * reach);
}

[[nodiscard]] std::int32_t damage_against(const Entity& attacker, const Entity& target,
                                          const std::int32_t resolve_basis) noexcept {
  const auto bonus = attacker.has_damage_bonus && attacker.bonus_against == target.armor
                         ? attacker.bonus_damage
                         : 0;
  return std::max(1, (attacker.damage + bonus) * resolve_basis / 10'000);
}

[[nodiscard]] std::vector<EntityId> sorted_unique_ids(std::vector<EntityId> ids) {
  std::ranges::sort(ids, {}, &EntityId::value);
  const auto duplicate = std::ranges::unique(ids, {}, &EntityId::value);
  ids.erase(duplicate.begin(), duplicate.end());
  return ids;
}

[[nodiscard]] CommandResult validate_units(const Simulation& simulation, const Command& command,
                                           const bool workers_only = false) noexcept {
  if (command.entities.empty()) {
    return failure(CommandError::InvalidEntity, workers_only ? "No workers were selected."
                                                              : "No units were selected.");
  }
  for (const auto id : command.entities) {
    const auto* entity = simulation.find_entity(id);
    if (entity == nullptr || entity->kind != EntityKind::Unit ||
        (workers_only && entity->type != EntityType::Worker)) {
      return failure(CommandError::InvalidEntity, workers_only ? "Only workers can perform that order."
                                                                : "A selected unit does not exist.");
    }
    if (entity->owner != command.player) {
      return failure(CommandError::InvalidOwner, "A selected unit belongs to another player.");
    }
  }
  return success();
}

template <typename Value>
void hash_integral(std::uint64_t& hash, const Value value) noexcept {
  auto bits = static_cast<std::uint64_t>(value);
  for (std::size_t index = 0; index < sizeof(Value); ++index) {
    hash ^= bits & 0xffU;
    hash *= kFnvPrime;
    bits >>= 8U;
  }
}

void hash_vec(std::uint64_t& hash, const Vec2 value) noexcept {
  hash_integral(hash, value.x);
  hash_integral(hash, value.y);
}

void hash_order(std::uint64_t& hash, const Order& order) noexcept {
  hash_integral(hash, static_cast<std::uint8_t>(order.type));
  hash_vec(hash, order.target);
  hash_vec(hash, order.secondary_target);
  hash_integral(hash, order.target_entity.value);
  hash_integral(hash, order.resource.value);
  hash_integral(hash, static_cast<std::uint8_t>(order.gather_phase));
  hash_integral(hash, order.phase_ticks);
  hash_vec(hash, order.route_goal);
  hash_integral(hash, order.route_index);
  hash_integral(hash, order.route.size());
  for (const auto waypoint : order.route) {
    hash_vec(hash, waypoint);
  }
}

}  // namespace

Simulation::Simulation(const SimulationConfig& config) {
  reset(config);
}

void Simulation::reset(const SimulationConfig& config) {
  config_ = config;
  tick_ = 0;
  status_ = MatchStatus::Playing;
  winner_.reset();
  players_ = {PlayerState{PlayerId::One, config.player_one_faction},
              PlayerState{PlayerId::Two, config.player_two_faction}};
  for (const auto player_id : {PlayerId::One, PlayerId::Two}) {
    players_[player_index(player_id)].ore = config.starting_ore[player_index(player_id)];
  }
  command_seen_.fill(false);
  for (auto& grid : visibility_) {
    grid.reset(config_.map_size, config_.visibility_cell_size);
  }
  spatial_grid_.reset(config_.map_size, config_.spatial_cell_size);
  for (auto& memory : resource_memory_) {
    memory.clear();
  }
  for (auto& memory : control_point_memory_) {
    memory.clear();
  }
  for (auto& memory : enemy_memory_) {
    memory.clear();
  }
  for (const auto player_id : {PlayerId::One, PlayerId::Two}) {
    const auto index = player_index(player_id);
    commanders_[index].reset(config_.commander_difficulties[index]);
  }
  entities_.clear();
  entity_index_by_id_.assign(1, kInvalidEntityIndex);
  resources_.clear();
  control_points_.clear();
  vows_.clear();
  command_queue_.clear();
  command_trace_.clear();
  ai_decision_trace_.clear();
  events_.clear();
  ruin_tide_ = 4;
  next_entity_id_ = 1;
  next_resource_id_ = 1;
  next_control_point_id_ = 1;
  next_sequence_ = 1;
  next_ai_decision_id_ = 1;
  next_event_id_ = 1;
  event_digest_ = kFnvOffset;

  if (!config.seed_starting_forces) {
    return;
  }

  const auto middle_y = config.map_size.y / 2;
  const auto left_x = world(300, 0).x;
  const auto right_x = config.map_size.x - left_x;

  for (const auto& [player_id, base_x, direction] :
       std::array{std::tuple{PlayerId::One, left_x, 1}, std::tuple{PlayerId::Two, right_x, -1}}) {
    static_cast<void>(spawn_entity(player_id, EntityType::Command, {base_x, middle_y}));
    for (std::int32_t index = 0; index < 3; ++index) {
      static_cast<void>(spawn_entity(player_id, EntityType::Worker,
                                     {base_x + direction * world(70 + index * 20, 0).x,
                                      middle_y + world(-30 + index * 30, 0).x}));
    }
    static_cast<void>(spawn_entity(player_id, EntityType::Vanguard,
                                   {base_x + direction * world(125, 0).x, middle_y + world(80, 0).x}));
  }

  // Starter fields sit in open base-side pockets, away from Blackridge and Gravewood.
  static_cast<void>(add_resource({left_x + world(260, 0).x, middle_y + world(180, 0).x}, 1'200));
  static_cast<void>(add_resource({right_x - world(260, 0).x, middle_y - world(180, 0).x}, 1'200));
  static_cast<void>(add_resource({config.map_size.x / 2, middle_y}, 2'400, 32'000));
  static_cast<void>(add_resource({world(620, 0).x, world(170, 0).x}, 900));
  static_cast<void>(add_resource({world(790, 0).x, world(180, 0).x}, 900));
  static_cast<void>(add_resource({world(1'780, 0).x, world(1'230, 0).x}, 900));
  static_cast<void>(add_resource({world(1'610, 0).x, world(1'220, 0).x}, 900));
  static_cast<void>(add_control_point({config.map_size.x / 2, world(380, 0).x}));
  static_cast<void>(add_control_point({config.map_size.x / 2, world(1'020, 0).x}));
}

std::uint64_t Simulation::enqueue(Command command) {
  return enqueue_with_context(std::move(command), CommandSource::External, 0);
}

std::uint64_t Simulation::enqueue_with_context(Command command, const CommandSource source,
                                               const std::uint64_t observation_hash,
                                               const std::uint64_t ai_decision_id) {
  if (command.sequence == 0) {
    command.sequence = next_sequence_++;
  } else {
    next_sequence_ = std::max(next_sequence_, command.sequence + 1);
  }
  const auto sequence = command.sequence;
  command_queue_.push_back(
      QueuedCommand{std::move(command), tick_, source, observation_hash, ai_decision_id});
  std::stable_sort(command_queue_.begin(), command_queue_.end(),
                   [](const QueuedCommand& left, const QueuedCommand& right) {
                     return std::tuple{left.command.execute_tick, left.command.sequence,
                                       player_index(left.command.player)} <
                            std::tuple{right.command.execute_tick, right.command.sequence,
                                       player_index(right.command.player)};
                   });
  return sequence;
}

CommandResult Simulation::execute_now(Command command) {
  command.execute_tick = tick_;
  if (command.sequence == 0) {
    command.sequence = next_sequence_++;
  } else {
    next_sequence_ = std::max(next_sequence_, command.sequence + 1);
  }
  const auto result = apply_command(command);
  command_trace_.push_back(CommandTraceEntry{tick_, tick_, CommandSource::External, 0, 0,
                                              std::move(command), result.ok, result.error});
  return result;
}

void Simulation::step() {
  if (status_ != MatchStatus::Playing) {
    return;
  }

  apply_due_commands();

  for (auto& entity : entities_) {
    if (entity.cooldown_ticks > 0) {
      --entity.cooldown_ticks;
    }
  }
  for (auto& player_state : players_) {
    if (player_state.power_cooldown_ticks > 0) {
      --player_state.power_cooldown_ticks;
    }
  }

  update_ruin_tide();
  update_research();
  update_production();
  update_control_points();
  update_resolve();
  update_orders();
  update_auto_aggro();
  update_defenses();
  remove_dead_entities();
  refresh_visibility();
  update_match_status();
  ++tick_;
  refresh_observation_memory();
  update_commanders();
}

void Simulation::run(const Tick ticks) {
  for (Tick count = 0; count < ticks && status_ == MatchStatus::Playing; ++count) {
    step();
  }
}

EntityId Simulation::spawn_entity(const PlayerId owner, const EntityType type, const Vec2 position,
                                  const bool under_construction) {
  const auto definition = entity_definition(player(owner).faction, type);
  Entity entity{};
  entity.id = EntityId{next_entity_id_++};
  entity.owner = owner;
  entity.faction = player(owner).faction;
  entity.type = type;
  entity.kind = definition.kind;
  entity.position = position;
  entity.radius = definition.radius;
  entity.hit_points = definition.hit_points;
  entity.max_hit_points = definition.hit_points;
  entity.speed_per_tick = definition.speed_per_tick;
  entity.attack_range = definition.attack_range;
  entity.damage = definition.damage;
  entity.attack_cooldown_ticks = definition.attack_cooldown_ticks;
  entity.sight = definition.sight;
  entity.armor = definition.armor;
  entity.bonus_against = definition.bonus_against;
  entity.has_damage_bonus = definition.has_damage_bonus;
  entity.bonus_damage = definition.bonus_damage;
  entity.terror = definition.terror;
  entity.ward = definition.ward;
  entity.resolve = 100;
  entity.resolve_state = ResolveState::Steady;
  entity.supply_cost = definition.supply_cost;
  entity.supply_provided = definition.supply_provided;
  entity.guard_position = position;
  entity.under_construction = under_construction;
  entity.construction_total_ticks = under_construction ? definition.build_ticks : 0;
  entity.construction_ticks = 0;
  entity.rally_point = {position.x + (owner == PlayerId::One ? world(55, 0).x : -world(55, 0).x), position.y};

  apply_research_bonuses(entity, false);
  if (under_construction) {
    entity.hit_points = std::max(1, entity.max_hit_points * 24 / 100);
  }

  auto& owner_state = mutable_player(owner);
  owner_state.supply_used += definition.supply_cost;
  if (!under_construction) {
    owner_state.supply_cap += definition.supply_provided;
  }
  if (type == EntityType::Command) {
    command_seen_[player_index(owner)] = true;
  }

  entities_.push_back(std::move(entity));
  if (entity_index_by_id_.size() <= entities_.back().id.value) {
    entity_index_by_id_.resize(
        static_cast<std::size_t>(entities_.back().id.value) + 1,
        kInvalidEntityIndex);
  }
  entity_index_by_id_[entities_.back().id.value] = entities_.size() - 1;
  emit_event(EntitySpawnedEvent{entities_.back().id, owner,
                                entities_.back().faction, type});
  refresh_visibility();
  refresh_observation_memory();
  return entities_.back().id;
}

ResourceId Simulation::add_resource(const Vec2 position, const std::int32_t amount, const std::int32_t radius) {
  const auto id = ResourceId{next_resource_id_++};
  resources_.push_back(ResourceNode{id, position, radius, std::max(0, amount)});
  refresh_observation_memory();
  return id;
}

ControlPointId Simulation::add_control_point(const Vec2 position, const std::int32_t radius) {
  const auto id = ControlPointId{next_control_point_id_++};
  control_points_.push_back(ControlPoint{id, position, radius});
  refresh_observation_memory();
  return id;
}

const PlayerState& Simulation::player(const PlayerId id) const noexcept {
  return players_[player_index(id)];
}

PlayerState& Simulation::mutable_player(const PlayerId id) noexcept {
  return players_[player_index(id)];
}

const Entity* Simulation::find_entity(const EntityId id) const noexcept {
  if (!id || id.value >= entity_index_by_id_.size()) {
    return nullptr;
  }
  const auto index = entity_index_by_id_[id.value];
  if (index == kInvalidEntityIndex || index >= entities_.size()) {
    return nullptr;
  }
  const auto& entity = entities_[index];
  return entity.id == id && entity.alive() ? &entity : nullptr;
}

Entity* Simulation::find_entity_mutable(const EntityId id) noexcept {
  if (!id || id.value >= entity_index_by_id_.size()) {
    return nullptr;
  }
  const auto index = entity_index_by_id_[id.value];
  if (index == kInvalidEntityIndex || index >= entities_.size()) {
    return nullptr;
  }
  auto& entity = entities_[index];
  return entity.id == id && entity.alive() ? &entity : nullptr;
}

const ResourceNode* Simulation::find_resource(const ResourceId id) const noexcept {
  const auto found = std::find_if(resources_.begin(), resources_.end(),
                                  [id](const ResourceNode& node) { return node.id == id; });
  return found == resources_.end() ? nullptr : &*found;
}

ResourceNode* Simulation::find_resource_mutable(const ResourceId id) noexcept {
  const auto found = std::find_if(resources_.begin(), resources_.end(),
                                  [id](const ResourceNode& node) { return node.id == id; });
  return found == resources_.end() ? nullptr : &*found;
}

const ControlPoint* Simulation::find_control_point(const ControlPointId id) const noexcept {
  const auto found = std::find_if(control_points_.begin(), control_points_.end(),
                                  [id](const ControlPoint& point) { return point.id == id; });
  return found == control_points_.end() ? nullptr : &*found;
}

ControlPoint* Simulation::find_control_point_mutable(const ControlPointId id) noexcept {
  const auto found = std::find_if(control_points_.begin(), control_points_.end(),
                                  [id](const ControlPoint& point) { return point.id == id; });
  return found == control_points_.end() ? nullptr : &*found;
}

VowState* Simulation::find_vow_mutable(const VowId id) noexcept {
  const auto found = std::ranges::find(vows_, id, &VowState::id);
  return found == vows_.end() ? nullptr : &*found;
}

CommandResult Simulation::apply_command(const Command& command) {
  if (status_ != MatchStatus::Playing) {
    return failure(CommandError::InvalidTarget, "The match has already ended.");
  }

  switch (command.type) {
    case CommandType::Move:
      return apply_move(command);
    case CommandType::Attack:
      return apply_attack(command);
    case CommandType::AttackMove:
      return apply_attack_move(command);
    case CommandType::Gather:
      return apply_gather(command);
    case CommandType::Train:
      return apply_train(command);
    case CommandType::Stop:
      return apply_stop(command);
    case CommandType::Hold:
      return apply_hold(command);
    case CommandType::Patrol:
      return apply_patrol(command);
    case CommandType::SetRallyPoint:
      return apply_set_rally_point(command);
    case CommandType::Build:
      return apply_build(command);
    case CommandType::Research:
      return apply_research(command);
    case CommandType::ActivatePower:
      return apply_activate_power(command);
    case CommandType::Retreat:
      return apply_retreat(command);
    case CommandType::SetStance:
      return apply_set_stance(command);
    case CommandType::MakeVow:
      return apply_make_vow(command);
    case CommandType::KeepVow:
      return apply_keep_vow(command);
    case CommandType::BreakVow:
      return apply_break_vow(command);
    case CommandType::AmendVow:
      return apply_amend_vow(command);
  }
  return failure(CommandError::InvalidEntity, "Unsupported command.");
}

CommandResult Simulation::apply_move(const Command& command) {
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }

  const auto ids = sorted_unique_ids(command.entities);
  const auto targets = formation_targets(ids, command.target);
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (auto* entity = find_entity_mutable(ids[index])) {
      Order order{};
      order.type = OrderType::Move;
      order.target = targets[index];
      set_order(*entity, std::move(order), command.queue);
    }
  }
  return success();
}

CommandResult Simulation::apply_attack(const Command& command) {
  const auto* target = find_entity(command.target_entity);
  if (target == nullptr || !target->alive() || target->owner == command.player ||
      !is_entity_visible_to(*target, command.player)) {
    return failure(CommandError::InvalidTarget, "No visible enemy target.");
  }
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }
  for (const auto id : sorted_unique_ids(command.entities)) {
    if (auto* entity = find_entity_mutable(id)) {
      Order order{};
      order.type = OrderType::Attack;
      order.target_entity = command.target_entity;
      set_order(*entity, std::move(order), command.queue);
    }
  }
  return success();
}

CommandResult Simulation::apply_attack_move(const Command& command) {
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }

  const auto ids = sorted_unique_ids(command.entities);
  const auto targets = formation_targets(ids, command.target);
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (auto* entity = find_entity_mutable(ids[index])) {
      Order order{};
      order.type = OrderType::AttackMove;
      order.target = targets[index];
      set_order(*entity, std::move(order), command.queue);
    }
  }
  return success();
}

CommandResult Simulation::apply_gather(const Command& command) {
  if (find_resource(command.resource) == nullptr) {
    return failure(CommandError::InvalidTarget, "The resource node does not exist.");
  }
  if (const auto validation = validate_units(*this, command, true); !validation) {
    return validation;
  }
  for (const auto id : sorted_unique_ids(command.entities)) {
    if (auto* entity = find_entity_mutable(id)) {
      Order order{};
      order.type = OrderType::Gather;
      order.resource = command.resource;
      set_order(*entity, std::move(order), command.queue);
    }
  }
  return success();
}

CommandResult Simulation::apply_train(const Command& command) {
  auto* producer = find_entity_mutable(command.producer);
  if (producer == nullptr || producer->kind != EntityKind::Building || producer->owner != command.player) {
    return failure(CommandError::InvalidProducer, "The production building is invalid.");
  }
  if (!is_unit(command.train_type) || !can_train(producer->type, command.train_type)) {
    return failure(CommandError::InvalidUnitType, "That building cannot train the requested unit.");
  }
  if (producer->under_construction) {
    return failure(CommandError::UnderConstruction, "The production building is not complete.");
  }
  if (producer->production_queue.size() >= 5) {
    return failure(CommandError::QueueFull, "The production queue is full.");
  }
  if (command.train_type == EntityType::Skirmisher && !has_research(command.player, ResearchId::TierTwo)) {
    return failure(CommandError::PrerequisiteMissing, "Reach the Black-Iron Age before training ranged units.");
  }

  const auto definition = entity_definition(player(command.player).faction, command.train_type);
  auto& owner = mutable_player(command.player);
  const auto cost = production_cost(command.player, command.train_type);
  if (owner.ore < cost) {
    return failure(CommandError::InsufficientOre, "Not enough ore.");
  }
  if (owner.supply_used + queued_supply(command.player) + definition.supply_cost > owner.supply_cap) {
    return failure(CommandError::SupplyBlocked, "Supply cap reached.");
  }

  const auto ticks = production_ticks(command.player, command.train_type);
  owner.ore -= cost;
  producer->production_queue.push_back({command.train_type, ticks, ticks});
  return success();
}

CommandResult Simulation::apply_stop(const Command& command) {
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }
  for (const auto id : sorted_unique_ids(command.entities)) {
    if (auto* entity = find_entity_mutable(id)) {
      entity->order = {};
      entity->order_queue.clear();
    }
  }
  return success();
}

CommandResult Simulation::apply_hold(const Command& command) {
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }
  for (const auto id : sorted_unique_ids(command.entities)) {
    if (auto* entity = find_entity_mutable(id)) {
      Order order{};
      order.type = OrderType::Hold;
      set_order(*entity, std::move(order), command.queue);
    }
  }
  return success();
}

CommandResult Simulation::apply_patrol(const Command& command) {
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }

  const auto ids = sorted_unique_ids(command.entities);
  const auto targets = formation_targets(ids, command.target);
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (auto* entity = find_entity_mutable(ids[index])) {
      Order order{};
      order.type = OrderType::Patrol;
      order.target = targets[index];
      order.secondary_target = entity->position;
      set_order(*entity, std::move(order), command.queue);
    }
  }
  return success();
}

CommandResult Simulation::apply_set_rally_point(const Command& command) {
  auto* producer = find_entity_mutable(command.producer);
  if (producer == nullptr || producer->kind != EntityKind::Building || producer->owner != command.player ||
      producer->under_construction) {
    return failure(CommandError::InvalidProducer, "The rally-point building is invalid.");
  }
  producer->rally_point = nearest_navigable(command.target, entity_definition(player(command.player).faction,
                                                                               EntityType::Worker).radius);
  return success();
}

CommandResult Simulation::apply_build(const Command& command) {
  if (command.entities.size() != 1) {
    return failure(CommandError::InvalidEntity, "Select exactly one worker to construct a building.");
  }
  auto* worker = find_entity_mutable(command.entities.front());
  if (worker == nullptr || worker->owner != command.player || worker->type != EntityType::Worker) {
    return failure(CommandError::InvalidOwner, "The construction worker is invalid.");
  }
  if (!is_building(command.building_type) || command.building_type == EntityType::Command) {
    return failure(CommandError::InvalidUnitType, "That structure cannot be constructed in a match.");
  }
  if (command.target_entity) {
    auto* site = find_entity_mutable(command.target_entity);
    if (site == nullptr || site->owner != command.player || site->type != command.building_type ||
        !site->under_construction) {
      return failure(CommandError::InvalidTarget, "That construction site cannot be resumed.");
    }
    const auto staffed = std::ranges::any_of(entities_, [site_id = site->id](const Entity& entity) {
      return entity.type == EntityType::Worker && entity.alive() &&
             entity.order.type == OrderType::Build && entity.order.target_entity == site_id;
    });
    if (staffed) {
      return failure(CommandError::InvalidTarget, "That construction site already has a builder.");
    }

    Order order{};
    order.type = OrderType::Build;
    order.target = site->position;
    order.target_entity = site->id;
    set_order(*worker, std::move(order), false);
    return success();
  }
  if (!can_place_building(command.target, command.building_type)) {
    return failure(CommandError::PlacementBlocked, "The construction site is blocked.");
  }

  const auto definition = entity_definition(player(command.player).faction, command.building_type);
  auto& owner = mutable_player(command.player);
  if (owner.ore < definition.cost) {
    return failure(CommandError::InsufficientOre, "Not enough ore for that structure.");
  }

  owner.ore -= definition.cost;
  const auto site = spawn_entity(command.player, command.building_type, command.target, true);
  worker = find_entity_mutable(command.entities.front());
  if (worker == nullptr) {
    return failure(CommandError::InvalidEntity, "The construction worker disappeared.");
  }
  Order order{};
  order.type = OrderType::Build;
  order.target = command.target;
  order.target_entity = site;
  set_order(*worker, std::move(order), false);
  return success();
}

CommandResult Simulation::apply_research(const Command& command) {
  auto* producer = find_entity_mutable(command.producer);
  const auto definition = research_definition(command.research);
  auto& owner = mutable_player(command.player);
  if (producer == nullptr || producer->owner != command.player || producer->type != definition.producer) {
    return failure(CommandError::InvalidProducer, "The selected structure cannot research that doctrine.");
  }
  if (producer->under_construction) {
    return failure(CommandError::UnderConstruction, "The research structure is not complete.");
  }
  if (definition.faction.has_value() && *definition.faction != owner.faction) {
    return failure(CommandError::InvalidTarget, "That doctrine belongs to another faction.");
  }
  if (has_research(command.player, command.research) ||
      std::ranges::any_of(owner.research_queue, [command](const ResearchTask& task) {
        return task.id == command.research;
      })) {
    return failure(CommandError::AlreadyResearched, "That doctrine is already known or in progress.");
  }
  if (definition.prerequisite.has_value() && !has_research(command.player, *definition.prerequisite)) {
    return failure(CommandError::PrerequisiteMissing, "A prerequisite doctrine is missing.");
  }
  if (!owner.research_queue.empty()) {
    return failure(CommandError::ResearchBusy, "Another doctrine is already being researched.");
  }
  if (owner.ore < definition.cost) {
    return failure(CommandError::InsufficientOre, "Not enough ore for research.");
  }

  owner.ore -= definition.cost;
  owner.research_queue.push_back({command.research, definition.research_ticks, definition.research_ticks});
  return success();
}

CommandResult Simulation::apply_activate_power(const Command& command) {
  auto& owner = mutable_player(command.player);
  const auto power = power_definition(owner.faction);
  if (owner.power_cooldown_ticks > 0) {
    return failure(CommandError::PowerCooldown, "The faction doctrine is still recovering.");
  }
  if (owner.ore < power.cost) {
    return failure(CommandError::InsufficientOre, "Not enough ore for the faction doctrine.");
  }
  const auto* ability =
      find_faction_power_ability(builtin_content(), owner.faction);

  if (owner.faction == FactionId::Ascendancy) {
    const auto* command_structure = nearest_command(command.player, config_.map_size);
    if (command_structure == nullptr || command_structure->under_construction) {
      return failure(CommandError::InvalidProducer, "Manifest Absolution requires a House of Quiet.");
    }
    const auto unit = entity_definition(owner.faction, EntityType::Vanguard);
    if (owner.supply_used + queued_supply(command.player) + unit.supply_cost > owner.supply_cap) {
      return failure(CommandError::SupplyBlocked, "Army capacity reached.");
    }
    if (ability != nullptr) {
      emit_event(AbilityStartedEvent{ability->metadata.stable_id,
                                     command.player, command_structure->id});
    }
    const auto command_position = command_structure->position;
    const auto direction = command.player == PlayerId::One ? 1 : -1;
    const auto spawn = nearest_navigable(
        {command_position.x + direction * (command_structure->radius + unit.radius + 8'000), command_position.y},
        unit.radius);
    const auto manifested = spawn_entity(command.player, EntityType::Vanguard, spawn);
    if (auto* entity = find_entity_mutable(manifested)) {
      Order order{};
      order.type = OrderType::AttackMove;
      order.target = nearest_navigable({spawn.x + direction * 110'000, spawn.y}, entity->radius);
      set_order(*entity, std::move(order), false);
    }
  } else if (owner.faction == FactionId::Compact) {
    if (ability != nullptr) {
      emit_event(AbilityStartedEvent{ability->metadata.stable_id,
                                     command.player, {}});
    }
    for (auto& entity : entities_) {
      if (entity.owner == command.player && entity.alive() && entity.kind == EntityKind::Unit) {
        entity.resolve = 100;
        entity.hit_points = std::min(entity.max_hit_points,
                                     entity.hit_points + std::max(1, entity.max_hit_points * 12 / 100));
      }
    }
  } else {
    if (ability != nullptr) {
      emit_event(AbilityStartedEvent{ability->metadata.stable_id,
                                     command.player, {}});
    }
    for (auto& entity : entities_) {
      if (entity.owner != command.player || !entity.alive()) {
        continue;
      }
      if (entity.kind == EntityKind::Building) {
        entity.hit_points = std::min(entity.max_hit_points,
                                     entity.hit_points + std::max(1, entity.max_hit_points * 24 / 100));
      } else {
        entity.resolve = std::min(100, entity.resolve + 18);
      }
    }
  }

  owner.ore -= power.cost;
  owner.power_cooldown_ticks = power.cooldown_ticks;
  return success();
}

CommandResult Simulation::apply_retreat(const Command& command) {
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }
  const auto* command_structure = nearest_command(command.player, find_entity(command.entities.front())->position);
  if (command_structure == nullptr && command.target == Vec2{}) {
    return failure(CommandError::InvalidTarget, "No retreat route is available.");
  }
  const auto ids = sorted_unique_ids(command.entities);
  auto destination = command.target;
  if (destination == Vec2{}) {
    destination = command_structure->position;
  } else {
    auto maximum_radius = 1;
    for (const auto id : ids) {
      if (const auto* entity = find_entity(id)) {
        maximum_radius = std::max(maximum_radius, entity->radius);
      }
    }
    destination = nearest_navigable(destination, maximum_radius);
  }
  const auto targets = formation_targets(ids, destination);
  for (std::size_t index = 0; index < ids.size(); ++index) {
    if (auto* entity = find_entity_mutable(ids[index])) {
      entity->resolve = std::min(100, entity->resolve + 8);
      entity->stance = UnitStance::Defensive;
      Order order{};
      order.type = OrderType::Move;
      order.target = targets[index];
      set_order(*entity, std::move(order), false);
    }
  }
  return success();
}

CommandResult Simulation::apply_set_stance(const Command& command) {
  if (const auto validation = validate_units(*this, command); !validation) {
    return validation;
  }
  for (const auto id : sorted_unique_ids(command.entities)) {
    if (auto* entity = find_entity_mutable(id)) {
      entity->stance = command.stance;
      entity->guard_position = entity->position;
      if (command.stance == UnitStance::Hold) {
        Order order{};
        order.type = OrderType::Hold;
        set_order(*entity, std::move(order), false);
      }
    }
  }
  return success();
}

CommandResult Simulation::apply_make_vow(const Command& command) {
  if (find_vow_content(builtin_content(), command.vow) == nullptr) {
    return failure(CommandError::InvalidVow,
                   "The requested Vow definition does not exist.");
  }
  if (find_vow_mutable(command.vow) != nullptr) {
    return failure(CommandError::VowAlreadyExists,
                   "That Vow has already been made in this match.");
  }
  vows_.push_back(
      VowState{command.vow, command.player, VowResolution::Unresolved,
               tick_, 0, 1, std::nullopt});
  std::ranges::sort(vows_, {}, [](const VowState& vow) {
    return vow.id.value;
  });
  emit_event(VowMadeEvent{command.vow, command.player});
  return success();
}

CommandResult Simulation::apply_keep_vow(const Command& command) {
  auto* vow = find_vow_mutable(command.vow);
  if (vow == nullptr) {
    return failure(CommandError::InvalidVow, "That Vow has not been made.");
  }
  if (vow->resolution != VowResolution::Unresolved) {
    return failure(CommandError::VowAlreadyResolved,
                   "That Vow has already been resolved.");
  }
  if (vow->maker != command.player) {
    return failure(CommandError::VowAuthorityRequired,
                   "Only the Vow's maker can declare it kept.");
  }
  vow->resolution = VowResolution::Kept;
  vow->resolved_tick = tick_;
  emit_event(VowKeptEvent{vow->id, vow->maker});
  return success();
}

CommandResult Simulation::apply_break_vow(const Command& command) {
  auto* vow = find_vow_mutable(command.vow);
  if (vow == nullptr) {
    return failure(CommandError::InvalidVow, "That Vow has not been made.");
  }
  if (vow->resolution != VowResolution::Unresolved) {
    return failure(CommandError::VowAlreadyResolved,
                   "That Vow has already been resolved.");
  }
  if (vow->maker != command.player) {
    return failure(CommandError::VowAuthorityRequired,
                   "Only the Vow's maker can declare it broken.");
  }
  vow->resolution = VowResolution::Broken;
  vow->resolved_tick = tick_;
  emit_event(VowBrokenEvent{vow->id, vow->maker});
  return success();
}

CommandResult Simulation::apply_amend_vow(const Command& command) {
  auto* vow = find_vow_mutable(command.vow);
  const auto* definition =
      find_vow_content(builtin_content(), command.vow);
  if (vow == nullptr || definition == nullptr) {
    return failure(CommandError::InvalidVow, "That Vow has not been made.");
  }
  if (vow->resolution != VowResolution::Unresolved) {
    return failure(CommandError::VowAlreadyResolved,
                   "That Vow has already been resolved.");
  }
  if (definition->amendment_requires_affected_party &&
      command.player == vow->maker) {
    return failure(
        CommandError::VowAuthorityRequired,
        "An affected party must participate in amending this Vow.");
  }
  vow->resolution = VowResolution::Amended;
  vow->resolved_tick = tick_;
  ++vow->revision;
  vow->participating_affected_player = command.player;
  emit_event(VowAmendedEvent{vow->id, vow->maker, command.player,
                             vow->revision});
  return success();
}

void Simulation::apply_due_commands() {
  while (!command_queue_.empty() && command_queue_.front().command.execute_tick <= tick_) {
    auto pending = std::move(command_queue_.front());
    command_queue_.erase(command_queue_.begin());
    const auto result = apply_command(pending.command);
    command_trace_.push_back(CommandTraceEntry{pending.issued_tick, tick_, pending.source,
                                                pending.observation_hash, pending.ai_decision_id,
                                                std::move(pending.command), result.ok, result.error});
    resolve_ai_decision(pending.ai_decision_id, tick_, result);
  }
}

void Simulation::resolve_ai_decision(const std::uint64_t decision_id, const Tick applied_tick,
                                     const CommandResult& result) noexcept {
  if (decision_id == 0) {
    return;
  }
  const auto record = std::ranges::find(ai_decision_trace_.rbegin(), ai_decision_trace_.rend(),
                                        decision_id, &AIDecisionRecord::id);
  if (record == ai_decision_trace_.rend()) {
    return;
  }
  record->applied_tick = applied_tick;
  record->command_status = result.ok ? AICommandStatus::Accepted : AICommandStatus::Rejected;
  record->command_error = result.error;
}

void Simulation::update_ruin_tide() {
  const auto phase = tick_ % kRuinTidePeriodTicks;
  const auto half_period = kRuinTidePeriodTicks / 2;
  const auto rising = phase <= half_period ? phase : kRuinTidePeriodTicks - phase;
  const auto normalized = static_cast<std::int64_t>(rising) * 10'000 / static_cast<std::int64_t>(half_period);
  const auto smooth = normalized * normalized * (30'000 - 2 * normalized) / 100'000'000;
  ruin_tide_ = 4 + static_cast<std::int32_t>(96 * smooth / 10'000);
}

void Simulation::update_research() {
  for (auto& player_state : players_) {
    if (player_state.research_queue.empty()) {
      continue;
    }
    auto& task = player_state.research_queue.front();
    if (task.remaining_ticks > 0) {
      --task.remaining_ticks;
    }
    if (task.remaining_ticks > 0) {
      continue;
    }

    const auto completed = task.id;
    player_state.research_queue.erase(player_state.research_queue.begin());
    player_state.researched[research_index(completed)] = true;
    if (completed == ResearchId::TierTwo) {
      player_state.tech_tier = 2;
    }
    for (auto& entity : entities_) {
      if (entity.owner == player_state.id && entity.alive()) {
        apply_research_bonuses(entity, true);
      }
    }
  }
}

void Simulation::update_production() {
  struct SpawnRequest {
    PlayerId owner;
    EntityType type;
    Vec2 spawn_position;
    Vec2 rally_point;
  };
  std::vector<SpawnRequest> spawns;

  for (auto& building : entities_) {
    if (!building.alive() || building.under_construction || building.production_queue.empty()) {
      continue;
    }
    auto& task = building.production_queue.front();
    if (task.remaining_ticks > 0) {
      --task.remaining_ticks;
    }
    if (task.remaining_ticks == 0) {
      const auto definition = entity_definition(player(building.owner).faction, task.type);
      const auto direction = building.owner == PlayerId::One ? 1 : -1;
      const auto spawn_offset = building.radius + definition.radius + 8'000;
      const auto spawn_position = nearest_navigable(
          {building.position.x + direction * spawn_offset, building.position.y}, definition.radius);
      spawns.push_back({building.owner, task.type, spawn_position, building.rally_point});
      building.production_queue.erase(building.production_queue.begin());
    }
  }

  for (const auto& spawn : spawns) {
    const auto id = spawn_entity(spawn.owner, spawn.type, spawn.spawn_position);
    if (auto* entity = find_entity_mutable(id); entity != nullptr && entity->position != spawn.rally_point) {
      Order order{};
      order.type = OrderType::Move;
      order.target = nearest_navigable(spawn.rally_point, entity->radius);
      set_order(*entity, std::move(order), false);
    }
  }
}

void Simulation::update_control_points() {
  for (auto& point : control_points_) {
    const auto previous_owner = point.owner;
    const auto was_contested = point.contested;
    std::array<std::int32_t, 2> presence{};
    for (const auto& entity : entities_) {
      if (!entity.alive() || entity.kind != EntityKind::Unit ||
          !within_reach(entity, point.position, point.radius)) {
        continue;
      }
      presence[player_index(entity.owner)] += entity.type == EntityType::Worker ? 1 : 2;
    }
    point.contested = presence[0] > 0 && presence[1] > 0;
    if (point.contested && !was_contested) {
      emit_event(ObjectiveContestedEvent{point.id});
    }

    if (presence[0] > 0 && presence[1] == 0) {
      const auto delta = (1'300 + presence[0] * 150) / kTicksPerSecond;
      point.influence = std::min(kCaptureMaximum, point.influence + delta);
    } else if (presence[1] > 0 && presence[0] == 0) {
      const auto delta = (1'300 + presence[1] * 150) / kTicksPerSecond;
      point.influence = std::max(-kCaptureMaximum, point.influence - delta);
    }

    if (point.owner == PlayerId::One && point.influence <= 0) {
      point.owner.reset();
    } else if (point.owner == PlayerId::Two && point.influence >= 0) {
      point.owner.reset();
    }
    if (point.influence >= kCaptureMaximum) {
      point.owner = PlayerId::One;
    } else if (point.influence <= -kCaptureMaximum) {
      point.owner = PlayerId::Two;
    }
    if (point.owner.has_value() && point.owner != previous_owner) {
      emit_event(
          ObjectiveCapturedEvent{point.id, previous_owner, *point.owner});
    }

    if (point.owner.has_value()) {
      point.income_progress += kControlIncomePerTick;
      while (point.income_progress >= kControlIncomeThreshold) {
        ++mutable_player(*point.owner).ore;
        point.income_progress -= kControlIncomeThreshold;
      }
    } else {
      point.income_progress = 0;
    }
  }
}

void Simulation::update_resolve() {
  // Rebuild timing is explicit: Resolve queries the immutable positions produced by
  // the preceding control-point phase and before this tick's movement.
  spatial_grid_.rebuild(entities_);
  const auto output = evaluate_resolve(ruin_tide_, players_, entities_,
                                       control_points_, spatial_grid_);
  for (const auto& update : output.entities) {
    auto* entity = find_entity_mutable(update.entity);
    if (entity == nullptr) {
      continue;
    }
    const auto previous = entity->resolve_state;
    entity->resolve = update.resolve;
    entity->resolve_state = update.state;
    if (previous != update.state) {
      emit_event(ResolveThresholdChangedEvent{
          entity->id, previous, update.state, update.resolve});
    }
  }
  for (const auto player_id : {PlayerId::One, PlayerId::Two}) {
    mutable_player(player_id).resolve =
        output.player_resolve[player_index(player_id)];
  }
}

void Simulation::update_orders() {
  for (auto& entity : entities_) {
    if (!entity.alive() || entity.kind != EntityKind::Unit) {
      continue;
    }

    switch (entity.order.type) {
      case OrderType::Idle:
        break;
      case OrderType::Move:
        if (within_reach(entity, entity.order.target, 0)) {
          entity.position = entity.order.target;
          complete_order(entity);
        } else {
          static_cast<void>(move_along_route(entity, entity.order.target));
        }
        break;
      case OrderType::Attack:
        if (!attack_target(entity, true)) {
          complete_order(entity);
        }
        break;
      case OrderType::AttackMove:
        update_attack_move(entity);
        break;
      case OrderType::Gather:
        update_gather(entity);
        break;
      case OrderType::Build:
        update_build(entity);
        break;
      case OrderType::Patrol:
        update_patrol(entity);
        break;
      case OrderType::Hold:
        update_hold(entity);
        break;
    }
  }

  resolve_unit_separation();
}

void Simulation::update_defenses() {
  for (auto& building : entities_) {
    if (!building.alive() || building.kind != EntityKind::Building || building.under_construction ||
        building.damage <= 0 || building.cooldown_ticks > 0) {
      continue;
    }
    const auto target_id = nearest_enemy(building.owner, building.position,
                                         building.radius + building.attack_range);
    auto* target = find_entity_mutable(target_id);
    if (target == nullptr ||
        !within_reach(building, target->position, target->radius, building.attack_range)) {
      continue;
    }
    apply_damage(*target, building.id,
                 damage_against(building, *target, 10'000));
    building.cooldown_ticks = building.attack_cooldown_ticks;
  }
}

void Simulation::update_auto_aggro() {
  for (auto& entity : entities_) {
    if (!entity.alive() || entity.kind != EntityKind::Unit || entity.damage <= 0 ||
        entity.order.type != OrderType::Idle) {
      continue;
    }
    auto acquisition_range = entity.sight;
    if (entity.stance == UnitStance::Defensive) {
      acquisition_range = entity.sight * 72 / 100;
    } else if (entity.stance == UnitStance::Hold) {
      acquisition_range = entity.radius + entity.attack_range + 42'000;
    }
    const auto target = nearest_enemy(entity.owner, entity.position, acquisition_range);
    if (!target) {
      continue;
    }
    Order order{};
    order.type = entity.stance == UnitStance::Hold ? OrderType::Hold : OrderType::Attack;
    order.target_entity = target;
    set_order(entity, std::move(order), false);
  }
}

void Simulation::update_gather(Entity& entity) {
  auto* resource = find_resource_mutable(entity.order.resource);
  if (resource == nullptr || (resource->amount <= 0 && entity.carrying == 0)) {
    complete_order(entity);
    return;
  }

  switch (entity.order.gather_phase) {
    case GatherPhase::ToResource:
      if (within_reach(entity, resource->position, resource->radius)) {
        entity.order.gather_phase = GatherPhase::Harvest;
        entity.order.phase_ticks = kHarvestTicks;
        clear_route(entity.order);
      } else {
        static_cast<void>(move_along_route(entity, resource->position));
      }
      break;
    case GatherPhase::Harvest:
      if (entity.order.phase_ticks > 0) {
        --entity.order.phase_ticks;
      }
      if (entity.order.phase_ticks == 0) {
        const auto gathered = std::min(kOrePerTrip, resource->amount);
        resource->amount -= gathered;
        entity.carrying = gathered;
        entity.order.gather_phase = GatherPhase::Return;
      }
      break;
    case GatherPhase::Return: {
      const auto* command = nearest_command(entity.owner, entity.position);
      if (command == nullptr) {
        complete_order(entity);
        return;
      }
      if (within_reach(entity, command->position, command->radius)) {
        const auto income_rate = faction_definition(player(entity.owner).faction).income_basis_points;
        mutable_player(entity.owner).ore += entity.carrying * income_rate / 10'000;
        entity.carrying = 0;
        entity.order.gather_phase = GatherPhase::ToResource;
        clear_route(entity.order);
      } else {
        static_cast<void>(move_along_route(entity, command->position));
      }
      break;
    }
  }
}

void Simulation::update_build(Entity& entity) {
  auto* building = find_entity_mutable(entity.order.target_entity);
  if (building == nullptr || building->owner != entity.owner) {
    complete_order(entity);
    return;
  }
  if (!building->under_construction) {
    complete_order(entity);
    return;
  }
  if (!within_reach(entity, building->position, building->radius, kConstructionReach)) {
    static_cast<void>(move_along_route(entity, building->position));
    return;
  }

  clear_route(entity.order);
  building->construction_ticks = std::min(building->construction_total_ticks,
                                          building->construction_ticks + 1);
  const auto total = std::max<Tick>(1, building->construction_total_ticks);
  const auto progress_basis = static_cast<std::int32_t>(building->construction_ticks * 10'000 / total);
  const auto target_health = building->max_hit_points * (2'400 + progress_basis * 76 / 100) / 10'000;
  building->hit_points = std::max(building->hit_points, std::max(1, target_health));
  if (building->construction_ticks < building->construction_total_ticks) {
    return;
  }

  building->under_construction = false;
  building->construction_ticks = building->construction_total_ticks;
  building->hit_points = building->max_hit_points;
  mutable_player(building->owner).supply_cap += building->supply_provided;
  complete_order(entity);
}

void Simulation::update_attack_move(Entity& entity) {
  if (entity.order.target_entity && attack_target(entity, true)) {
    return;
  }

  entity.order.target_entity = nearest_enemy(
      entity.owner, entity.position, std::max(kAttackMoveAcquisitionRange, entity.radius + entity.attack_range));
  if (entity.order.target_entity && attack_target(entity, true)) {
    return;
  }

  entity.order.target_entity = {};
  if (within_reach(entity, entity.order.target, 0)) {
    entity.position = entity.order.target;
    complete_order(entity);
    return;
  }
  static_cast<void>(move_along_route(entity, entity.order.target));
}

void Simulation::update_patrol(Entity& entity) {
  if (entity.order.target_entity && attack_target(entity, true)) {
    return;
  }

  entity.order.target_entity = nearest_enemy(
      entity.owner, entity.position, std::max(kAttackMoveAcquisitionRange, entity.radius + entity.attack_range));
  if (entity.order.target_entity && attack_target(entity, true)) {
    return;
  }

  entity.order.target_entity = {};
  if (within_reach(entity, entity.order.target, 0)) {
    entity.position = entity.order.target;
    std::swap(entity.order.target, entity.order.secondary_target);
    clear_route(entity.order);
    return;
  }
  static_cast<void>(move_along_route(entity, entity.order.target));
}

void Simulation::update_hold(Entity& entity) {
  if (entity.order.target_entity && attack_target(entity, false)) {
    return;
  }

  entity.order.target_entity = nearest_enemy(entity.owner, entity.position, entity.radius + entity.attack_range);
  if (entity.order.target_entity) {
    static_cast<void>(attack_target(entity, false));
  }
}

void Simulation::set_order(Entity& entity, Order order, const bool queue) {
  if (!queue) {
    entity.order_queue.clear();
    entity.order = std::move(order);
    return;
  }

  if (entity.order.type == OrderType::Idle) {
    entity.order = std::move(order);
  } else if (entity.order_queue.size() < 32) {
    entity.order_queue.push_back(std::move(order));
  }
}

void Simulation::complete_order(Entity& entity) {
  if (entity.order_queue.empty()) {
    entity.order = {};
    return;
  }

  entity.order = std::move(entity.order_queue.front());
  entity.order_queue.erase(entity.order_queue.begin());
}

void Simulation::clear_route(Order& order) const noexcept {
  order.route.clear();
  order.route_index = 0;
  order.route_goal = {};
}

bool Simulation::move_along_route(Entity& entity, Vec2 target) {
  target = nearest_navigable(target, entity.radius);
  auto& order = entity.order;
  const auto repath_distance = std::max(1, config_.navigation_cell_size / 2);
  const auto repath_distance_squared = static_cast<std::uint64_t>(repath_distance) * repath_distance;
  const bool target_moved = squared_distance(order.route_goal, target) > repath_distance_squared;
  if (order.route.empty() || order.route_index >= order.route.size() || target_moved) {
    order.route = find_path(entity.position, target, entity.radius);
    order.route_index = 0;
    order.route_goal = target;
  }

  while (order.route_index < order.route.size() &&
         within_reach(entity, order.route[order.route_index], 0)) {
    entity.position = order.route[order.route_index];
    ++order.route_index;
  }
  if (order.route_index >= order.route.size()) {
    return within_reach(entity, target, 0);
  }

  move_toward(entity, order.route[order.route_index]);
  return false;
}

bool Simulation::attack_target(Entity& entity, const bool chase) {
  auto* target = find_entity_mutable(entity.order.target_entity);
  if (target == nullptr || target->owner == entity.owner || !target->alive() ||
      !is_entity_visible_to(*target, entity.owner)) {
    entity.order.target_entity = {};
    clear_route(entity.order);
    return false;
  }

  if (!within_reach(entity, target->position, target->radius, entity.attack_range)) {
    if (!chase) {
      entity.order.target_entity = {};
      clear_route(entity.order);
      return false;
    }
    static_cast<void>(move_along_route(entity, target->position));
    return true;
  }

  clear_route(entity.order);
  if (entity.cooldown_ticks == 0) {
    apply_damage(
        *target, entity.id,
        damage_against(entity, *target, resolve_multiplier_basis(entity)));
    entity.cooldown_ticks = entity.attack_cooldown_ticks;
  }
  return true;
}

EntityId Simulation::nearest_enemy(const PlayerId owner, const Vec2 position,
                                   const std::int32_t acquisition_range) const noexcept {
  EntityId result{};
  auto best_distance = std::numeric_limits<std::uint64_t>::max();
  for (const auto& candidate : entities_) {
    if (!candidate.alive() || candidate.owner == owner || !is_entity_visible_to(candidate, owner)) {
      continue;
    }
    const auto reach = static_cast<std::int64_t>(acquisition_range) + candidate.radius;
    const auto distance = squared_distance(position, candidate.position);
    if (distance <= static_cast<std::uint64_t>(reach * reach) &&
        (distance < best_distance || (distance == best_distance && candidate.id.value < result.value))) {
      result = candidate.id;
      best_distance = distance;
    }
  }
  return result;
}

void Simulation::resolve_unit_separation() {
  for (int pass = 0; pass < 2; ++pass) {
    for (std::size_t left_index = 0; left_index < entities_.size(); ++left_index) {
      auto& left = entities_[left_index];
      if (!left.alive() || left.kind != EntityKind::Unit) {
        continue;
      }
      for (std::size_t right_index = left_index + 1; right_index < entities_.size(); ++right_index) {
        auto& right = entities_[right_index];
        if (!right.alive() || right.kind != EntityKind::Unit) {
          continue;
        }

        auto dx = static_cast<std::int64_t>(right.position.x) - left.position.x;
        auto dy = static_cast<std::int64_t>(right.position.y) - left.position.y;
        auto distance = integer_sqrt(static_cast<std::uint64_t>(dx * dx + dy * dy));
        const auto required = static_cast<std::uint64_t>(left.radius + right.radius + kSeparationPadding);
        if (distance >= required) {
          continue;
        }
        if (distance == 0) {
          dx = left.id.value < right.id.value ? 1 : -1;
          dy = 0;
          distance = 1;
        }

        const auto overlap = static_cast<std::int64_t>(required - distance);
        const auto left_push = overlap / 2;
        const auto right_push = overlap - left_push;
        Vec2 left_candidate{
            left.position.x - static_cast<std::int32_t>(dx * left_push / static_cast<std::int64_t>(distance)),
            left.position.y - static_cast<std::int32_t>(dy * left_push / static_cast<std::int64_t>(distance)),
        };
        Vec2 right_candidate{
            right.position.x + static_cast<std::int32_t>(dx * right_push / static_cast<std::int64_t>(distance)),
            right.position.y + static_cast<std::int32_t>(dy * right_push / static_cast<std::int64_t>(distance)),
        };
        left_candidate.x = std::clamp(left_candidate.x, left.radius, config_.map_size.x - left.radius);
        left_candidate.y = std::clamp(left_candidate.y, left.radius, config_.map_size.y - left.radius);
        right_candidate.x = std::clamp(right_candidate.x, right.radius, config_.map_size.x - right.radius);
        right_candidate.y = std::clamp(right_candidate.y, right.radius, config_.map_size.y - right.radius);
        if (is_navigable(left_candidate, left.radius)) {
          left.position = left_candidate;
        }
        if (is_navigable(right_candidate, right.radius)) {
          right.position = right_candidate;
        }
      }
    }
  }
}

std::vector<Vec2> Simulation::formation_targets(const std::vector<EntityId>& entities, const Vec2 target) const {
  if (entities.empty()) {
    return {};
  }

  std::int64_t center_x = 0;
  std::int64_t center_y = 0;
  std::int32_t max_radius = 0;
  for (const auto id : entities) {
    if (const auto* entity = find_entity(id)) {
      center_x += entity->position.x;
      center_y += entity->position.y;
      max_radius = std::max(max_radius, entity->radius);
    }
  }
  center_x /= static_cast<std::int64_t>(entities.size());
  center_y /= static_cast<std::int64_t>(entities.size());

  auto direction_x = static_cast<std::int64_t>(target.x) - center_x;
  auto direction_y = static_cast<std::int64_t>(target.y) - center_y;
  auto direction_length = integer_sqrt(
      static_cast<std::uint64_t>(direction_x * direction_x + direction_y * direction_y));
  if (direction_length == 0) {
    direction_x = 1;
    direction_y = 0;
    direction_length = 1;
  }

  const auto columns = static_cast<std::size_t>(integer_sqrt(entities.size() - 1) + 1);
  const auto rows = (entities.size() + columns - 1) / columns;
  const auto spacing = static_cast<std::int64_t>(std::max(max_radius * 2 + 8'000, 36'000));
  std::vector<Vec2> result;
  result.reserve(entities.size());
  for (std::size_t index = 0; index < entities.size(); ++index) {
    const auto column = index % columns;
    const auto row = index / columns;
    const auto right_offset =
        (static_cast<std::int64_t>(column * 2) - static_cast<std::int64_t>(columns - 1)) * spacing / 2;
    const auto forward_offset =
        (static_cast<std::int64_t>(rows - 1) - static_cast<std::int64_t>(row * 2)) * spacing / 2;
    Vec2 slot{
        target.x + static_cast<std::int32_t>((-direction_y * right_offset + direction_x * forward_offset) /
                                             static_cast<std::int64_t>(direction_length)),
        target.y + static_cast<std::int32_t>((direction_x * right_offset + direction_y * forward_offset) /
                                             static_cast<std::int64_t>(direction_length)),
    };
    const auto* entity = find_entity(entities[index]);
    result.push_back(nearest_navigable(slot, entity == nullptr ? max_radius : entity->radius));
  }
  return result;
}

std::vector<Vec2> Simulation::find_path(Vec2 start, Vec2 goal, const std::int32_t radius) const {
  start = nearest_navigable(start, radius);
  goal = nearest_navigable(goal, radius);
  if (segment_is_navigable(start, goal, radius)) {
    return {goal};
  }

  const auto cell_size = std::max(1, config_.navigation_cell_size);
  const auto width = std::max(1, (config_.map_size.x + cell_size - 1) / cell_size);
  const auto height = std::max(1, (config_.map_size.y + cell_size - 1) / cell_size);
  const auto node_count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
  const auto index_of = [width](const std::int32_t x, const std::int32_t y) {
    return y * width + x;
  };
  const auto point_of = [this, cell_size, radius](const std::int32_t x, const std::int32_t y) {
    return Vec2{
        std::clamp(x * cell_size + cell_size / 2, radius, config_.map_size.x - radius),
        std::clamp(y * cell_size + cell_size / 2, radius, config_.map_size.y - radius),
    };
  };

  const auto nearest_cell = [&](const Vec2 point) {
    auto best_index = -1;
    auto best_distance = std::numeric_limits<std::uint64_t>::max();
    for (std::int32_t y = 0; y < height; ++y) {
      for (std::int32_t x = 0; x < width; ++x) {
        const auto candidate = point_of(x, y);
        if (!is_navigable(candidate, radius) || !segment_is_navigable(point, candidate, radius)) {
          continue;
        }
        const auto distance = squared_distance(point, candidate);
        const auto index = index_of(x, y);
        if (distance < best_distance || (distance == best_distance && index < best_index)) {
          best_index = index;
          best_distance = distance;
        }
      }
    }
    return best_index;
  };

  const auto start_index = nearest_cell(start);
  const auto goal_index = nearest_cell(goal);
  if (start_index < 0 || goal_index < 0) {
    return {};
  }

  constexpr auto unreachable = std::numeric_limits<std::int32_t>::max() / 4;
  std::vector<std::int32_t> cost(node_count, unreachable);
  std::vector<std::int32_t> parent(node_count, -1);
  std::vector<bool> open(node_count, false);
  std::vector<bool> closed(node_count, false);
  cost[static_cast<std::size_t>(start_index)] = 0;
  open[static_cast<std::size_t>(start_index)] = true;

  const auto goal_x = goal_index % width;
  const auto goal_y = goal_index / width;
  const auto heuristic = [goal_x, goal_y](const std::int32_t x, const std::int32_t y) {
    const auto dx = std::abs(goal_x - x);
    const auto dy = std::abs(goal_y - y);
    const auto diagonal = std::min(dx, dy);
    return diagonal * 14 + (std::max(dx, dy) - diagonal) * 10;
  };
  constexpr std::array<std::pair<std::int32_t, std::int32_t>, 8> directions{
      std::pair{1, 0}, std::pair{0, 1}, std::pair{-1, 0}, std::pair{0, -1},
      std::pair{1, 1}, std::pair{-1, 1}, std::pair{-1, -1}, std::pair{1, -1},
  };

  while (true) {
    auto current = -1;
    auto current_score = unreachable;
    auto current_heuristic = unreachable;
    for (std::size_t index = 0; index < node_count; ++index) {
      if (!open[index] || closed[index]) {
        continue;
      }
      const auto x = static_cast<std::int32_t>(index) % width;
      const auto y = static_cast<std::int32_t>(index) / width;
      const auto estimate = heuristic(x, y);
      const auto score = cost[index] + estimate;
      if (score < current_score ||
          (score == current_score && (estimate < current_heuristic ||
                                      (estimate == current_heuristic && static_cast<int>(index) < current)))) {
        current = static_cast<int>(index);
        current_score = score;
        current_heuristic = estimate;
      }
    }
    if (current < 0 || current == goal_index) {
      break;
    }

    open[static_cast<std::size_t>(current)] = false;
    closed[static_cast<std::size_t>(current)] = true;
    const auto current_x = current % width;
    const auto current_y = current / width;
    for (const auto& [step_x, step_y] : directions) {
      const auto next_x = current_x + step_x;
      const auto next_y = current_y + step_y;
      if (next_x < 0 || next_y < 0 || next_x >= width || next_y >= height) {
        continue;
      }
      const auto next = index_of(next_x, next_y);
      if (closed[static_cast<std::size_t>(next)] || !is_navigable(point_of(next_x, next_y), radius)) {
        continue;
      }
      if (step_x != 0 && step_y != 0 &&
          (!is_navigable(point_of(current_x + step_x, current_y), radius) ||
           !is_navigable(point_of(current_x, current_y + step_y), radius))) {
        continue;
      }

      const auto next_cost = cost[static_cast<std::size_t>(current)] + (step_x == 0 || step_y == 0 ? 10 : 14);
      if (next_cost < cost[static_cast<std::size_t>(next)]) {
        cost[static_cast<std::size_t>(next)] = next_cost;
        parent[static_cast<std::size_t>(next)] = current;
        open[static_cast<std::size_t>(next)] = true;
      }
    }
  }

  if (start_index != goal_index && parent[static_cast<std::size_t>(goal_index)] < 0) {
    return {};
  }

  std::vector<Vec2> raw_path;
  for (auto current = goal_index; current != start_index && current >= 0;
       current = parent[static_cast<std::size_t>(current)]) {
    raw_path.push_back(point_of(current % width, current / width));
  }
  std::ranges::reverse(raw_path);
  if (raw_path.empty() || raw_path.back() != goal) {
    raw_path.push_back(goal);
  }

  std::vector<Vec2> smoothed;
  auto anchor = start;
  std::size_t cursor = 0;
  while (cursor < raw_path.size()) {
    auto best = cursor;
    auto found = false;
    for (auto candidate = raw_path.size(); candidate-- > cursor;) {
      if (segment_is_navigable(anchor, raw_path[candidate], radius)) {
        best = candidate;
        found = true;
        break;
      }
    }
    if (!found) {
      return {};
    }
    smoothed.push_back(raw_path[best]);
    anchor = raw_path[best];
    cursor = best + 1;
  }
  return smoothed;
}

bool Simulation::is_navigable(const Vec2 position, const std::int32_t radius) const noexcept {
  if (position.x < radius || position.y < radius || position.x > config_.map_size.x - radius ||
      position.y > config_.map_size.y - radius) {
    return false;
  }

  for (const auto& obstacle : config_.navigation_obstacles) {
    if (position.x >= obstacle.minimum.x - radius && position.x <= obstacle.maximum.x + radius &&
        position.y >= obstacle.minimum.y - radius && position.y <= obstacle.maximum.y + radius) {
      return false;
    }
  }
  return true;
}

bool Simulation::segment_is_navigable(const Vec2 start, const Vec2 end, const std::int32_t radius) const noexcept {
  const auto dx = static_cast<std::int64_t>(end.x) - start.x;
  const auto dy = static_cast<std::int64_t>(end.y) - start.y;
  const auto distance = integer_sqrt(static_cast<std::uint64_t>(dx * dx + dy * dy));
  const auto sample_stride = static_cast<std::uint64_t>(std::max(1, config_.navigation_cell_size / 3));
  const auto samples = std::max<std::uint64_t>(1, (distance + sample_stride - 1) / sample_stride);
  for (std::uint64_t index = 0; index <= samples; ++index) {
    const Vec2 point{
        start.x + static_cast<std::int32_t>(dx * static_cast<std::int64_t>(index) /
                                            static_cast<std::int64_t>(samples)),
        start.y + static_cast<std::int32_t>(dy * static_cast<std::int64_t>(index) /
                                            static_cast<std::int64_t>(samples)),
    };
    if (!is_navigable(point, radius)) {
      return false;
    }
  }
  return true;
}

Vec2 Simulation::nearest_navigable(Vec2 position, const std::int32_t radius) const noexcept {
  position.x = std::clamp(position.x, radius, config_.map_size.x - radius);
  position.y = std::clamp(position.y, radius, config_.map_size.y - radius);
  if (is_navigable(position, radius)) {
    return position;
  }

  const auto cell_size = std::max(1, config_.navigation_cell_size);
  const auto width = std::max(1, (config_.map_size.x + cell_size - 1) / cell_size);
  const auto height = std::max(1, (config_.map_size.y + cell_size - 1) / cell_size);
  auto result = position;
  auto best_distance = std::numeric_limits<std::uint64_t>::max();
  for (std::int32_t y = 0; y < height; ++y) {
    for (std::int32_t x = 0; x < width; ++x) {
      const Vec2 candidate{
          std::clamp(x * cell_size + cell_size / 2, radius, config_.map_size.x - radius),
          std::clamp(y * cell_size + cell_size / 2, radius, config_.map_size.y - radius),
      };
      if (!is_navigable(candidate, radius)) {
        continue;
      }
      const auto distance = squared_distance(position, candidate);
      if (distance < best_distance || (distance == best_distance && candidate < result)) {
        result = candidate;
        best_distance = distance;
      }
    }
  }
  return result;
}

void Simulation::apply_damage(Entity& target, const EntityId source,
                              const std::int32_t amount) {
  if (!target.alive() || amount <= 0) {
    return;
  }
  const auto previous_hit_points = target.hit_points;
  target.hit_points -= amount;
  if (target.kind != EntityKind::Unit) {
    return;
  }
  emit_event(UnitDamagedEvent{source, target.id, amount,
                              std::max(0, target.hit_points)});
  const auto wound_threshold = std::max(1, target.max_hit_points / 2);
  if (previous_hit_points > wound_threshold && target.hit_points > 0 &&
      target.hit_points <= wound_threshold) {
    emit_event(UnitWoundedEvent{target.id, source, target.hit_points});
  }
  if (target.hit_points <= 0) {
    emit_event(UnitKilledEvent{target.id, source});
  }
}

void Simulation::remove_dead_entities() {
  for (const auto& entity : entities_) {
    if (entity.alive()) {
      continue;
    }
    emit_event(EntityDestroyedEvent{entity.id, entity.owner, entity.faction,
                                    entity.type});
    auto& owner = mutable_player(entity.owner);
    owner.supply_used = std::max(0, owner.supply_used - entity.supply_cost);
    if (!entity.under_construction) {
      owner.supply_cap = std::max(0, owner.supply_cap - entity.supply_provided);
    }
  }
  std::erase_if(entities_, [](const Entity& entity) { return !entity.alive(); });
  rebuild_entity_index();
}

void Simulation::update_match_status() {
  if (!command_seen_[0] || !command_seen_[1]) {
    return;
  }
  std::array<bool, 2> command_alive{};
  for (const auto& entity : entities_) {
    if (entity.type == EntityType::Command && entity.alive()) {
      command_alive[player_index(entity.owner)] = true;
    }
  }
  if (command_alive[0] == command_alive[1]) {
    return;
  }
  winner_ = command_alive[0] ? PlayerId::One : PlayerId::Two;
  status_ = MatchStatus::Won;
}

void Simulation::move_toward(Entity& entity, const Vec2 target) const noexcept {
  const auto dx = static_cast<std::int64_t>(target.x) - entity.position.x;
  const auto dy = static_cast<std::int64_t>(target.y) - entity.position.y;
  const auto distance = integer_sqrt(static_cast<std::uint64_t>(dx * dx + dy * dy));
  const auto speed = std::max(1, entity.speed_per_tick * resolve_multiplier_basis(entity) / 10'000);
  if (distance == 0 || distance <= static_cast<std::uint64_t>(speed)) {
    entity.position = target;
    return;
  }

  entity.position.x += static_cast<std::int32_t>(dx * speed / static_cast<std::int64_t>(distance));
  entity.position.y += static_cast<std::int32_t>(dy * speed / static_cast<std::int64_t>(distance));
  entity.position.x = std::clamp(entity.position.x, 0, config_.map_size.x);
  entity.position.y = std::clamp(entity.position.y, 0, config_.map_size.y);
}

const Entity* Simulation::nearest_command(const PlayerId owner, const Vec2 position) const noexcept {
  const Entity* nearest = nullptr;
  auto nearest_distance = std::numeric_limits<std::uint64_t>::max();
  for (auto& entity : entities_) {
    if (entity.owner != owner || entity.type != EntityType::Command || !entity.alive()) {
      continue;
    }
    const auto distance = squared_distance(position, entity.position);
    if (distance < nearest_distance) {
      nearest = &entity;
      nearest_distance = distance;
    }
  }
  return nearest;
}

std::int32_t Simulation::queued_supply(const PlayerId owner) const noexcept {
  std::int32_t total = 0;
  for (const auto& entity : entities_) {
    if (entity.owner != owner) {
      continue;
    }
    total = std::accumulate(entity.production_queue.begin(), entity.production_queue.end(), total,
                            [this, owner](const std::int32_t subtotal, const ProductionTask& task) {
                              return subtotal + entity_definition(player(owner).faction, task.type).supply_cost;
                            });
  }
  return total;
}

PlayerObservation Simulation::observe(const PlayerId observer) const {
  const auto observer_index = player_index(observer);
  std::vector<Entity> owned_entities;
  owned_entities.reserve(entities_.size());
  for (const auto& entity : entities_) {
    if (entity.owner == observer && entity.alive()) {
      owned_entities.push_back(entity);
    }
  }
  std::ranges::sort(owned_entities, {}, [](const Entity& entity) { return entity.id.value; });

  std::vector<ObservedResource> known_resources;
  known_resources.reserve(resources_.size());
  for (std::size_t index = 0; index < resources_.size(); ++index) {
    if (index >= resource_memory_[observer_index].size() ||
        !resource_memory_[observer_index][index].discovered) {
      continue;
    }
    const auto& resource = resources_[index];
    const auto& memory = resource_memory_[observer_index][index];
    known_resources.push_back({resource.id, resource.position, resource.radius, memory.amount,
                               visibility_state_at(resource.position, observer), memory.observed_tick});
  }

  std::vector<ObservedControlPoint> objectives;
  objectives.reserve(control_points_.size());
  for (std::size_t index = 0; index < control_points_.size(); ++index) {
    const auto& point = control_points_[index];
    const auto memory = index < control_point_memory_[observer_index].size()
                            ? control_point_memory_[observer_index][index]
                            : ControlPointMemory{};
    objectives.push_back({point.id,
                          point.position,
                          point.radius,
                          visibility_state_at(point.position, observer),
                          memory.observed,
                          memory.owner,
                          memory.influence,
                          memory.observed_tick});
  }

  return PlayerObservation{tick_,
                           config_.match_seed,
                           observer,
                           player(enemy_of(observer)).faction,
                           status_,
                           player(observer),
                           ruin_tide_,
                           config_.map_size,
                           config_.navigation_cell_size,
                           config_.navigation_obstacles,
                           visibility(observer),
                           std::move(owned_entities),
                           enemy_memory_[observer_index],
                           std::move(known_resources),
                           std::move(objectives),
                           command_capabilities(observer)};
}

std::vector<CommandCapability> Simulation::command_capabilities(const PlayerId owner) const {
  const auto owner_index = player_index(owner);
  const auto& state = player(owner);
  std::vector<CommandCapability> result;
  const auto has_command = std::ranges::any_of(entities_, [owner](const Entity& entity) {
    return entity.owner == owner && entity.alive() && entity.type == EntityType::Command &&
           !entity.under_construction;
  });
  const auto has_known_resource = std::ranges::any_of(resource_memory_[owner_index],
                                                       [](const ResourceMemory& memory) {
                                                         return memory.discovered && memory.amount > 0;
                                                       });
  const auto has_visible_enemy = std::ranges::any_of(enemy_memory_[owner_index],
                                                      [](const ObservedEnemy& enemy) {
                                                        return enemy.currently_visible;
                                                      });
  const auto has_orphaned_site = [this, owner](const EntityType type) {
    return std::ranges::any_of(entities_, [this, owner, type](const Entity& site) {
      if (site.owner != owner || site.type != type || !site.alive() || !site.under_construction) {
        return false;
      }
      return std::ranges::none_of(entities_, [site_id = site.id](const Entity& worker) {
        return worker.type == EntityType::Worker && worker.alive() &&
               worker.order.type == OrderType::Build && worker.order.target_entity == site_id;
      });
    });
  };

  const auto add = [&result](const CommandType type, const EntityId actor = {},
                             const std::optional<EntityType> entity_type = std::nullopt,
                             const std::optional<ResearchId> research = std::nullopt) {
    result.push_back({type, actor, entity_type, research});
  };

  for (const auto& entity : entities_) {
    if (entity.owner != owner || !entity.alive()) {
      continue;
    }
    if (entity.kind == EntityKind::Unit && !entity.under_construction) {
      add(CommandType::Move, entity.id);
      add(CommandType::AttackMove, entity.id);
      add(CommandType::Stop, entity.id);
      add(CommandType::Hold, entity.id);
      add(CommandType::Patrol, entity.id);
      add(CommandType::SetStance, entity.id);
      if (has_visible_enemy) {
        add(CommandType::Attack, entity.id);
      }
      if (has_command) {
        add(CommandType::Retreat, entity.id);
      }
      if (entity.type == EntityType::Worker) {
        if (has_known_resource) {
          add(CommandType::Gather, entity.id);
        }
        for (const auto building : {EntityType::Barracks, EntityType::Turret}) {
          if (state.ore >= entity_definition(state.faction, building).cost ||
              has_orphaned_site(building)) {
            add(CommandType::Build, entity.id, building);
          }
        }
      }
      continue;
    }

    if (entity.kind != EntityKind::Building || entity.under_construction) {
      continue;
    }
    add(CommandType::SetRallyPoint, entity.id);
    if (entity.production_queue.size() < 5) {
      for (const auto unit : {EntityType::Worker, EntityType::Vanguard, EntityType::Skirmisher}) {
        const auto definition = entity_definition(state.faction, unit);
        const auto supply_available = state.supply_used + queued_supply(owner) + definition.supply_cost <=
                                      state.supply_cap;
        const auto prerequisite_met = unit != EntityType::Skirmisher || has_research(owner, ResearchId::TierTwo);
        if (can_train(entity.type, unit) && state.ore >= production_cost(owner, unit) && supply_available &&
            prerequisite_met) {
          add(CommandType::Train, entity.id, unit);
        }
      }
    }

    if (!state.research_queue.empty()) {
      continue;
    }
    constexpr std::array<ResearchId, kResearchCount> research_ids = {
        ResearchId::TierTwo,       ResearchId::TemperedOaths, ResearchId::Wardcraft,
        ResearchId::ChorusOfKnives, ResearchId::PitBroods,     ResearchId::VaultPlate,
        ResearchId::SiegeLiturgy,
    };
    for (const auto research : research_ids) {
      const auto definition = research_definition(research);
      const auto faction_matches = !definition.faction.has_value() || *definition.faction == state.faction;
      const auto prerequisite_met = !definition.prerequisite.has_value() ||
                                    has_research(owner, *definition.prerequisite);
      if (entity.type == definition.producer && faction_matches && prerequisite_met &&
          !has_research(owner, research) && state.ore >= definition.cost) {
        add(CommandType::Research, entity.id, std::nullopt, research);
      }
    }
  }

  const auto power = power_definition(state.faction);
  auto power_legal = state.power_cooldown_ticks == 0 && state.ore >= power.cost;
  if (state.faction == FactionId::Ascendancy) {
    const auto unit = entity_definition(state.faction, EntityType::Vanguard);
    power_legal = power_legal && has_command &&
                  state.supply_used + queued_supply(owner) + unit.supply_cost <= state.supply_cap;
  }
  if (power_legal) {
    add(CommandType::ActivatePower);
  }

  std::ranges::sort(result);
  return result;
}

const VisibilityGrid& Simulation::visibility(const PlayerId owner) const noexcept {
  return visibility_[player_index(owner)];
}

VisibilityState Simulation::visibility_state_at(const Vec2 position, const PlayerId owner) const noexcept {
  return visibility(owner).state_at(position);
}

std::vector<EntityId> Simulation::visible_enemy_ids(const PlayerId observer) const {
  std::vector<EntityId> result;
  result.reserve(entities_.size());
  for (const auto& entity : entities_) {
    if (entity.alive() && entity.owner != observer && is_entity_visible_to(entity, observer)) {
      result.push_back(entity.id);
    }
  }
  return result;
}

bool Simulation::is_position_visible_to(const Vec2 position, const PlayerId owner,
                                         const std::int32_t buffer) const noexcept {
  return visibility(owner).overlaps_visible(position, std::max(0, buffer));
}

bool Simulation::is_entity_visible_to(const Entity& entity, const PlayerId owner) const noexcept {
  return entity.owner == owner || is_position_visible_to(entity.position, owner, entity.radius);
}

void Simulation::refresh_visibility() noexcept {
  for (auto& grid : visibility_) {
    grid.begin_update();
  }
  for (const auto& entity : entities_) {
    if (!entity.alive() || entity.under_construction) {
      continue;
    }
    visibility_[player_index(entity.owner)].reveal(entity.position, entity.sight);
  }
}

void Simulation::refresh_observation_memory() {
  for (const auto observer : {PlayerId::One, PlayerId::Two}) {
    const auto index = player_index(observer);
    resource_memory_[index].resize(resources_.size());
    for (std::size_t resource_index = 0; resource_index < resources_.size(); ++resource_index) {
      const auto& resource = resources_[resource_index];
      if (!is_position_visible_to(resource.position, observer, resource.radius)) {
        continue;
      }
      auto& memory = resource_memory_[index][resource_index];
      memory.discovered = true;
      memory.amount = resource.amount;
      memory.observed_tick = tick_;
    }

    control_point_memory_[index].resize(control_points_.size());
    for (std::size_t point_index = 0; point_index < control_points_.size(); ++point_index) {
      const auto& point = control_points_[point_index];
      if (!is_position_visible_to(point.position, observer, point.radius)) {
        continue;
      }
      auto& memory = control_point_memory_[index][point_index];
      memory.observed = true;
      memory.owner = point.owner;
      memory.influence = point.influence;
      memory.observed_tick = tick_;
    }

    for (auto& enemy : enemy_memory_[index]) {
      enemy.currently_visible = false;
    }
    for (const auto& entity : entities_) {
      if (entity.owner == observer || !entity.alive() || !is_entity_visible_to(entity, observer)) {
        continue;
      }
      const auto found = std::ranges::find(enemy_memory_[index], entity.id, &ObservedEnemy::id);
      ObservedEnemy sighting{entity.id,
                            entity.owner,
                            entity.faction,
                            entity.type,
                            entity.kind,
                            entity.position,
                            entity.radius,
                            entity.hit_points,
                            entity.max_hit_points,
                            entity.resolve,
                            entity.under_construction,
                            true,
                            tick_};
      if (found == enemy_memory_[index].end()) {
        enemy_memory_[index].push_back(sighting);
      } else {
        *found = sighting;
      }
    }
    std::erase_if(enemy_memory_[index], [this, observer](const ObservedEnemy& enemy) {
      if (enemy.currently_visible) {
        return false;
      }
      if (is_position_visible_to(enemy.position, observer, enemy.radius)) {
        return true;
      }
      return enemy.kind == EntityKind::Unit &&
             tick_ >= enemy.last_observed_tick &&
             tick_ - enemy.last_observed_tick >= kMobileObservationMemoryTicks;
    });
    std::ranges::sort(enemy_memory_[index], {}, [](const ObservedEnemy& enemy) { return enemy.id.value; });
  }
}

void Simulation::update_commanders() {
  if (status_ != MatchStatus::Playing) {
    return;
  }

  std::array<CommanderPlan, 2> plans;
  std::array<std::uint64_t, 2> observation_hashes{};
  std::array<Tick, 2> observation_ticks{};
  for (const auto player_id : {PlayerId::One, PlayerId::Two}) {
    const auto index = player_index(player_id);
    if (config_.commander_players[index]) {
      const auto observation = observe(player_id);
      observation_hashes[index] = observation.hash();
      observation_ticks[index] = observation.tick();
      plans[index] = commanders_[index].update(observation);
    }
  }
  for (const auto player_id : {PlayerId::One, PlayerId::Two}) {
    const auto index = player_index(player_id);
    for (auto& decision : plans[index].decisions) {
      auto command = decision.command;
      command.player = player_id;
      command.execute_tick = tick_ + decision.command_latency_ticks;
      command.sequence = 0;
      auto recorded_command = command;
      const auto decision_id = next_ai_decision_id_++;
      const auto sequence = enqueue_with_context(std::move(command), CommandSource::CommanderAI,
                                                 observation_hashes[index], decision_id);
      recorded_command.sequence = sequence;

      AIDecisionRecord record{};
      record.id = decision_id;
      record.observation_tick = observation_ticks[index];
      record.observation_hash = observation_hashes[index];
      record.player = player_id;
      record.layer = decision.layer;
      record.cadence_ticks = decision.cadence_ticks;
      record.difficulty = decision.difficulty;
      record.difficulty_hash = decision.difficulty_hash;
      record.knowledge_tick = decision.knowledge_tick;
      record.doctrine_faction = decision.doctrine_faction;
      record.temperament = decision.temperament;
      record.doctrine_hash = decision.doctrine_hash;
      record.strategy_state_hash = decision.strategy_state_hash;
      record.candidates = std::move(decision.candidates);
      record.selected_candidate = decision.selected_candidate;
      record.evaluated_candidates = decision.evaluated_candidates;
      record.selected_quality_basis_points =
          decision.selected_quality_basis_points;
      record.mistake_applied = decision.mistake_applied;
      record.selected_action = decision.selected_action;
      record.winning_reason = decision.winning_reason;
      record.command_precision_offset = decision.command_precision_offset;
      record.command_latency_ticks = decision.command_latency_ticks;
      record.command = std::move(recorded_command);
      record.command_sequence = sequence;
      ai_decision_trace_.push_back(std::move(record));
    }
  }
}

bool Simulation::can_place_building(const Vec2 position, const EntityType type) const noexcept {
  if (!is_building(type) || type == EntityType::Command) {
    return false;
  }
  const auto radius = entity_definition(FactionId::Compact, type).radius;
  if (!is_navigable(position, radius + 12'000)) {
    return false;
  }
  for (const auto& entity : entities_) {
    const auto reach = static_cast<std::int64_t>(radius) + entity.radius + 18'000;
    if (entity.alive() && squared_distance(position, entity.position) < static_cast<std::uint64_t>(reach * reach)) {
      return false;
    }
  }
  for (const auto& resource : resources_) {
    const auto reach = static_cast<std::int64_t>(radius) + resource.radius + 22'000;
    if (resource.amount > 0 && squared_distance(position, resource.position) < static_cast<std::uint64_t>(reach * reach)) {
      return false;
    }
  }
  for (const auto& point : control_points_) {
    const auto reach = static_cast<std::int64_t>(radius) + point.radius + 24'000;
    if (squared_distance(position, point.position) < static_cast<std::uint64_t>(reach * reach)) {
      return false;
    }
  }
  return true;
}

bool Simulation::has_research(const PlayerId owner, const ResearchId research) const noexcept {
  return player(owner).researched[research_index(research)];
}

std::int32_t Simulation::production_cost(const PlayerId owner, const EntityType type) const noexcept {
  const auto definition = entity_definition(player(owner).faction, type);
  const auto shaped = player(owner).faction == FactionId::Ascendancy && type == EntityType::Vanguard &&
                      has_research(owner, ResearchId::PitBroods);
  return shaped ? (definition.cost * 82 + 50) / 100 : definition.cost;
}

Tick Simulation::production_ticks(const PlayerId owner, const EntityType type) const noexcept {
  const auto definition = entity_definition(player(owner).faction, type);
  const auto shaped = player(owner).faction == FactionId::Ascendancy && type == EntityType::Vanguard &&
                      has_research(owner, ResearchId::PitBroods);
  return shaped ? (definition.build_ticks * 82 + 50) / 100 : definition.build_ticks;
}

std::int32_t Simulation::resolve_multiplier_basis(const Entity& entity) const noexcept {
  if (entity.kind == EntityKind::Building) {
    return 10'000;
  }
  return std::clamp(6'800 + entity.resolve * 32, 6'800, 10'000);
}

void Simulation::apply_research_bonuses(Entity& entity, const bool preserve_health) {
  const auto& owner = player(entity.owner);
  const auto definition = entity_definition(owner.faction, entity.type);
  const auto health_basis = preserve_health && entity.max_hit_points > 0
                                ? static_cast<std::int64_t>(entity.hit_points) * 10'000 / entity.max_hit_points
                                : 10'000;

  auto maximum_health = definition.hit_points;
  auto speed = definition.speed_per_tick;
  auto damage = definition.damage;
  auto bonus_damage = definition.bonus_damage;
  auto terror = definition.terror;
  auto ward = definition.ward;

  if (has_research(entity.owner, ResearchId::TemperedOaths) && entity.type == EntityType::Vanguard) {
    maximum_health = maximum_health * 115 / 100;
    damage = damage * 115 / 100;
  }
  if (has_research(entity.owner, ResearchId::Wardcraft)) {
    ward = ward * 135 / 100;
  }
  if (has_research(entity.owner, ResearchId::ChorusOfKnives) && entity.kind == EntityKind::Unit &&
      entity.type != EntityType::Worker) {
    speed = speed * 110 / 100;
    terror += 4;
  }
  if (has_research(entity.owner, ResearchId::VaultPlate) &&
      (entity.kind == EntityKind::Building || entity.type != EntityType::Worker)) {
    maximum_health = maximum_health * 120 / 100;
  }
  if (has_research(entity.owner, ResearchId::SiegeLiturgy) && entity.type == EntityType::Vanguard) {
    bonus_damage += 8;
  }

  entity.max_hit_points = maximum_health;
  entity.hit_points = std::clamp(static_cast<std::int32_t>(maximum_health * health_basis / 10'000), 1,
                                 maximum_health);
  entity.speed_per_tick = speed;
  entity.attack_range = definition.attack_range;
  entity.damage = damage;
  entity.attack_cooldown_ticks = definition.attack_cooldown_ticks;
  entity.sight = definition.sight;
  entity.armor = definition.armor;
  entity.bonus_against = definition.bonus_against;
  entity.has_damage_bonus = definition.has_damage_bonus;
  entity.bonus_damage = bonus_damage;
  entity.terror = terror;
  entity.ward = ward;
}

void Simulation::emit_event(SimulationEventPayload payload) {
  SimulationEvent event{EventId{next_event_id_++}, tick_,
                        std::move(payload)};
  hash_integral(event_digest_, simulation_event_hash(event));
  events_.push_back(std::move(event));
}

void Simulation::rebuild_entity_index() noexcept {
  entity_index_by_id_.assign(static_cast<std::size_t>(next_entity_id_),
                             kInvalidEntityIndex);
  for (std::size_t index = 0; index < entities_.size(); ++index) {
    const auto id = entities_[index].id;
    if (id && id.value < entity_index_by_id_.size()) {
      entity_index_by_id_[id.value] = index;
    }
  }
}

std::uint64_t Simulation::state_hash() const noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, tick_);
  hash_integral(hash, static_cast<std::uint8_t>(config_.mode));
  hash_integral(hash, static_cast<std::uint8_t>(config_.story_mission));
  hash_integral(hash, static_cast<std::uint8_t>(config_.player_one_faction));
  hash_integral(hash, static_cast<std::uint8_t>(config_.player_two_faction));
  for (const auto enabled : config_.commander_players) {
    hash_integral(hash, enabled);
  }
  for (const auto difficulty : config_.commander_difficulties) {
    hash_integral(hash, static_cast<std::uint8_t>(difficulty));
  }
  for (const auto ore : config_.starting_ore) {
    hash_integral(hash, ore);
  }
  hash_integral(hash, config_.match_seed);
  hash_integral(hash, config_.seed_starting_forces);
  hash_vec(hash, config_.map_size);
  hash_integral(hash, config_.visibility_cell_size);
  hash_integral(hash, config_.navigation_cell_size);
  hash_integral(hash, config_.spatial_cell_size);
  hash_integral(hash, config_.navigation_obstacles.size());
  for (const auto& obstacle : config_.navigation_obstacles) {
    hash_vec(hash, obstacle.minimum);
    hash_vec(hash, obstacle.maximum);
  }
  hash_integral(hash, static_cast<std::uint8_t>(status_));
  hash_integral(hash, winner_.has_value() ? static_cast<std::uint8_t>(*winner_) + 1U : 0U);
  hash_integral(hash, next_entity_id_);
  hash_integral(hash, next_resource_id_);
  hash_integral(hash, next_control_point_id_);
  hash_integral(hash, next_sequence_);
  hash_integral(hash, next_ai_decision_id_);
  hash_integral(hash, next_event_id_);
  hash_integral(hash, event_digest_);
  hash_integral(hash, ruin_tide_);
  for (const auto seen : command_seen_) {
    hash_integral(hash, seen);
  }
  for (const auto& grid : visibility_) {
    hash_integral(hash, grid.cell_size());
    hash_integral(hash, grid.columns());
    hash_integral(hash, grid.rows());
    for (const auto state : grid.cells()) {
      hash_integral(hash, static_cast<std::uint8_t>(state));
    }
  }
  for (const auto& memories : resource_memory_) {
    hash_integral(hash, memories.size());
    for (const auto& memory : memories) {
      hash_integral(hash, memory.discovered);
      hash_integral(hash, memory.amount);
      hash_integral(hash, memory.observed_tick);
    }
  }
  for (const auto& memories : control_point_memory_) {
    hash_integral(hash, memories.size());
    for (const auto& memory : memories) {
      hash_integral(hash, memory.observed);
      hash_integral(hash, memory.owner.has_value() ? static_cast<std::uint8_t>(*memory.owner) + 1U : 0U);
      hash_integral(hash, memory.influence);
      hash_integral(hash, memory.observed_tick);
    }
  }
  for (const auto& memories : enemy_memory_) {
    hash_integral(hash, memories.size());
    for (const auto& memory : memories) {
      hash_integral(hash, memory.id.value);
      hash_integral(hash, static_cast<std::uint8_t>(memory.owner));
      hash_integral(hash, static_cast<std::uint8_t>(memory.faction));
      hash_integral(hash, static_cast<std::uint8_t>(memory.type));
      hash_integral(hash, static_cast<std::uint8_t>(memory.kind));
      hash_vec(hash, memory.position);
      hash_integral(hash, memory.radius);
      hash_integral(hash, memory.hit_points);
      hash_integral(hash, memory.max_hit_points);
      hash_integral(hash, memory.resolve);
      hash_integral(hash, memory.under_construction);
      hash_integral(hash, memory.currently_visible);
      hash_integral(hash, memory.last_observed_tick);
    }
  }
  for (const auto& commander : commanders_) {
    hash_integral(hash, commander.state_hash());
  }

  for (const auto& player_state : players_) {
    hash_integral(hash, static_cast<std::uint8_t>(player_state.id));
    hash_integral(hash, static_cast<std::uint8_t>(player_state.faction));
    hash_integral(hash, player_state.ore);
    hash_integral(hash, player_state.supply_used);
    hash_integral(hash, player_state.supply_cap);
    hash_integral(hash, player_state.resolve);
    hash_integral(hash, player_state.power_cooldown_ticks);
    hash_integral(hash, player_state.tech_tier);
    for (const auto researched : player_state.researched) {
      hash_integral(hash, researched);
    }
    for (const auto& task : player_state.research_queue) {
      hash_integral(hash, static_cast<std::uint8_t>(task.id));
      hash_integral(hash, task.remaining_ticks);
      hash_integral(hash, task.total_ticks);
    }
  }

  for (const auto& entity : entities_) {
    hash_integral(hash, entity.id.value);
    hash_integral(hash, static_cast<std::uint8_t>(entity.owner));
    hash_integral(hash, static_cast<std::uint8_t>(entity.faction));
    hash_integral(hash, static_cast<std::uint8_t>(entity.type));
    hash_integral(hash, static_cast<std::uint8_t>(entity.kind));
    hash_vec(hash, entity.position);
    hash_integral(hash, entity.radius);
    hash_integral(hash, entity.hit_points);
    hash_integral(hash, entity.max_hit_points);
    hash_integral(hash, entity.speed_per_tick);
    hash_integral(hash, entity.attack_range);
    hash_integral(hash, entity.damage);
    hash_integral(hash, entity.attack_cooldown_ticks);
    hash_integral(hash, entity.cooldown_ticks);
    hash_integral(hash, entity.sight);
    hash_integral(hash, static_cast<std::uint8_t>(entity.armor));
    hash_integral(hash, static_cast<std::uint8_t>(entity.bonus_against));
    hash_integral(hash, entity.has_damage_bonus);
    hash_integral(hash, entity.bonus_damage);
    hash_integral(hash, entity.terror);
    hash_integral(hash, entity.ward);
    hash_integral(hash, entity.resolve);
    hash_integral(hash, static_cast<std::uint8_t>(entity.resolve_state));
    hash_integral(hash, entity.supply_cost);
    hash_integral(hash, entity.supply_provided);
    hash_integral(hash, entity.carrying);
    hash_order(hash, entity.order);
    hash_integral(hash, entity.order_queue.size());
    for (const auto& order : entity.order_queue) {
      hash_order(hash, order);
    }
    hash_vec(hash, entity.rally_point);
    for (const auto& task : entity.production_queue) {
      hash_integral(hash, static_cast<std::uint8_t>(task.type));
      hash_integral(hash, task.remaining_ticks);
      hash_integral(hash, task.total_ticks);
    }
    hash_integral(hash, static_cast<std::uint8_t>(entity.stance));
    hash_vec(hash, entity.guard_position);
    hash_integral(hash, entity.under_construction);
    hash_integral(hash, entity.construction_ticks);
    hash_integral(hash, entity.construction_total_ticks);
  }

  for (const auto& resource : resources_) {
    hash_integral(hash, resource.id.value);
    hash_vec(hash, resource.position);
    hash_integral(hash, resource.radius);
    hash_integral(hash, resource.amount);
  }

  for (const auto& point : control_points_) {
    hash_integral(hash, point.id.value);
    hash_vec(hash, point.position);
    hash_integral(hash, point.radius);
    hash_integral(hash, point.owner.has_value() ? static_cast<std::uint8_t>(*point.owner) + 1U : 0U);
    hash_integral(hash, point.influence);
    hash_integral(hash, point.income_progress);
    hash_integral(hash, point.contested);
  }

  hash_integral(hash, vows_.size());
  for (const auto& vow : vows_) {
    hash_integral(hash, vow.id.value);
    hash_integral(hash, static_cast<std::uint8_t>(vow.maker));
    hash_integral(hash, static_cast<std::uint8_t>(vow.resolution));
    hash_integral(hash, vow.made_tick);
    hash_integral(hash, vow.resolved_tick);
    hash_integral(hash, vow.revision);
    hash_integral(
        hash, vow.participating_affected_player.has_value()
                  ? static_cast<std::uint8_t>(
                        *vow.participating_affected_player) +
                        1U
                  : 0U);
  }

  hash_integral(hash, static_cast<std::uint64_t>(command_queue_.size()));
  for (const auto& pending : command_queue_) {
    const auto& command = pending.command;
    hash_integral(hash, pending.issued_tick);
    hash_integral(hash, static_cast<std::uint8_t>(pending.source));
    hash_integral(hash, pending.observation_hash);
    hash_integral(hash, pending.ai_decision_id);
    hash_integral(hash, command.execute_tick);
    hash_integral(hash, command.sequence);
    hash_integral(hash, static_cast<std::uint8_t>(command.player));
    hash_integral(hash, static_cast<std::uint8_t>(command.type));
    hash_integral(hash, static_cast<std::uint64_t>(command.entities.size()));
    for (const auto entity : command.entities) {
      hash_integral(hash, entity.value);
    }
    hash_vec(hash, command.target);
    hash_integral(hash, command.target_entity.value);
    hash_integral(hash, command.resource.value);
    hash_integral(hash, command.producer.value);
    hash_integral(hash, static_cast<std::uint8_t>(command.train_type));
    hash_integral(hash, static_cast<std::uint8_t>(command.building_type));
    hash_integral(hash, static_cast<std::uint8_t>(command.research));
    hash_integral(hash, static_cast<std::uint8_t>(command.stance));
    hash_integral(hash, command.vow.value);
    hash_integral(hash, command.queue);
  }
  return hash;
}

}  // namespace ashen::core
