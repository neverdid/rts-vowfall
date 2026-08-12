#pragma once

#include "ashen/core/CasualtySystem.hpp"
#include "ashen/core/CommanderAI.hpp"
#include "ashen/core/ObjectiveSystem.hpp"
#include "ashen/core/PlayerObservation.hpp"
#include "ashen/core/SimulationEvent.hpp"
#include "ashen/core/SpatialGrid.hpp"
#include "ashen/core/SupplySystem.hpp"
#include "ashen/core/Types.hpp"
#include "ashen/core/VisibilityGrid.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace ashen::core {

class SnapshotCodec;

class ASHENCORE_API Simulation final {
 public:
  explicit Simulation(const SimulationConfig& config = {});

  void reset(const SimulationConfig& config = {});
  std::uint64_t enqueue(Command command);
  [[nodiscard]] CommandResult execute_now(Command command);
  void step();
  void run(Tick ticks);

  [[nodiscard]] EntityId spawn_entity(PlayerId owner, EntityType type, Vec2 position,
                                      bool under_construction = false);
  [[nodiscard]] ResourceId add_resource(Vec2 position, std::int32_t amount, std::int32_t radius = 24'000);
  [[nodiscard]] ControlPointId add_control_point(Vec2 position, std::int32_t radius = 92'000);

  [[nodiscard]] Tick tick() const noexcept { return tick_; }
  [[nodiscard]] MatchMode mode() const noexcept { return config_.mode; }
  [[nodiscard]] MatchStatus status() const noexcept { return status_; }
  [[nodiscard]] std::optional<PlayerId> winner() const noexcept { return winner_; }
  [[nodiscard]] const SimulationConfig& config() const noexcept { return config_; }
  [[nodiscard]] const std::array<PlayerState, 2>& players() const noexcept { return players_; }
  [[nodiscard]] const PlayerState& player(PlayerId id) const noexcept;
  [[nodiscard]] const std::vector<Entity>& entities() const noexcept { return entities_; }
  [[nodiscard]] const std::vector<ResourceNode>& resources() const noexcept { return resources_; }
  [[nodiscard]] const std::vector<ControlPoint>& control_points() const noexcept { return control_points_; }
  [[nodiscard]] const std::vector<CommandTraceEntry>& command_trace() const noexcept { return command_trace_; }
  [[nodiscard]] const std::vector<AIDecisionRecord>& ai_decision_trace() const noexcept {
    return ai_decision_trace_;
  }
  [[nodiscard]] const std::vector<SimulationEvent>& events() const noexcept {
    return events_;
  }
  [[nodiscard]] const std::vector<VowState>& vows() const noexcept {
    return vows_;
  }
  [[nodiscard]] std::span<const CasualtyRecord> casualties() const noexcept {
    return casualty_system_.records();
  }
  [[nodiscard]] std::span<const CasualtyTransition> casualty_history() const noexcept {
    return casualty_system_.transitions();
  }
  [[nodiscard]] const CasualtyRecord* find_casualty(
      UnitIdentityId identity) const noexcept {
    return casualty_system_.find(identity);
  }
  [[nodiscard]] std::span<const MissionObjectiveState> mission_objectives()
      const noexcept {
    return objective_system_.states();
  }
  [[nodiscard]] std::optional<MissionObjectiveView> primary_mission_objective()
      const {
    return objective_system_.primary_view(tick_);
  }
  [[nodiscard]] const SpatialGrid& spatial_grid() const noexcept {
    return spatial_grid_;
  }
  [[nodiscard]] std::span<const SupplyNodeState> supply_nodes() const noexcept {
    return supply_system_.states();
  }
  [[nodiscard]] bool is_supply_connected(EntityId entity) const noexcept {
    return supply_system_.connected(entity);
  }
  [[nodiscard]] std::uint64_t event_digest() const noexcept {
    return event_digest_;
  }
  [[nodiscard]] const Entity* find_entity(EntityId id) const noexcept;
  [[nodiscard]] const ResourceNode* find_resource(ResourceId id) const noexcept;
  [[nodiscard]] const ControlPoint* find_control_point(ControlPointId id) const noexcept;
  [[nodiscard]] std::int32_t ruin_tide() const noexcept { return ruin_tide_; }
  [[nodiscard]] PlayerObservation observe(PlayerId player) const;
  [[nodiscard]] const VisibilityGrid& visibility(PlayerId owner) const noexcept;
  [[nodiscard]] VisibilityState visibility_state_at(Vec2 position, PlayerId owner) const noexcept;
  [[nodiscard]] std::vector<EntityId> visible_enemy_ids(PlayerId observer) const;
  [[nodiscard]] bool is_position_visible_to(Vec2 position, PlayerId owner,
                                             std::int32_t buffer = 0) const noexcept;
  [[nodiscard]] bool is_entity_visible_to(const Entity& entity, PlayerId owner) const noexcept;
  [[nodiscard]] bool can_place_building(Vec2 position, EntityType type) const noexcept;
  [[nodiscard]] bool has_research(PlayerId owner, ResearchId research) const noexcept;
  [[nodiscard]] std::int32_t production_cost(PlayerId owner, EntityType type) const noexcept;
  [[nodiscard]] Tick production_ticks(PlayerId owner, EntityType type) const noexcept;
  [[nodiscard]] std::uint64_t state_hash() const noexcept;

 private:
  friend class SnapshotCodec;

  [[nodiscard]] PlayerState& mutable_player(PlayerId id) noexcept;
  [[nodiscard]] Entity* find_entity_mutable(EntityId id) noexcept;
  [[nodiscard]] ResourceNode* find_resource_mutable(ResourceId id) noexcept;
  [[nodiscard]] ControlPoint* find_control_point_mutable(ControlPointId id) noexcept;
  [[nodiscard]] VowState* find_vow_mutable(VowId id) noexcept;
  [[nodiscard]] std::uint64_t enqueue_with_context(Command command, CommandSource source,
                                                   std::uint64_t observation_hash,
                                                   std::uint64_t ai_decision_id = 0);
  void resolve_ai_decision(std::uint64_t decision_id, Tick applied_tick,
                           const CommandResult& result) noexcept;
  [[nodiscard]] CommandResult apply_command(const Command& command);
  [[nodiscard]] CommandResult apply_move(const Command& command);
  [[nodiscard]] CommandResult apply_attack(const Command& command);
  [[nodiscard]] CommandResult apply_attack_move(const Command& command);
  [[nodiscard]] CommandResult apply_gather(const Command& command);
  [[nodiscard]] CommandResult apply_train(const Command& command);
  [[nodiscard]] CommandResult apply_stop(const Command& command);
  [[nodiscard]] CommandResult apply_hold(const Command& command);
  [[nodiscard]] CommandResult apply_patrol(const Command& command);
  [[nodiscard]] CommandResult apply_set_rally_point(const Command& command);
  [[nodiscard]] CommandResult apply_build(const Command& command);
  [[nodiscard]] CommandResult apply_research(const Command& command);
  [[nodiscard]] CommandResult apply_activate_power(const Command& command);
  [[nodiscard]] CommandResult apply_retreat(const Command& command);
  [[nodiscard]] CommandResult apply_set_stance(const Command& command);
  [[nodiscard]] CommandResult apply_make_vow(const Command& command);
  [[nodiscard]] CommandResult apply_keep_vow(const Command& command);
  [[nodiscard]] CommandResult apply_break_vow(const Command& command);
  [[nodiscard]] CommandResult apply_amend_vow(const Command& command);
  void apply_due_commands();
  void update_ruin_tide();
  void update_research();
  void update_production();
  void update_control_points();
  void update_supply();
  void update_resolve();
  void update_orders();
  void update_auto_aggro();
  void update_defenses();
  void update_commanders();
  void update_gather(Entity& entity);
  void update_build(Entity& entity);
  void update_attack_move(Entity& entity);
  void update_patrol(Entity& entity);
  void update_hold(Entity& entity);
  void refresh_visibility() noexcept;
  void refresh_observation_memory();
  void resolve_unit_separation();
  void apply_damage(Entity& target, EntityId source, std::int32_t amount);
  void remove_dead_entities();
  void update_match_status();
  void set_order(Entity& entity, Order order, bool queue);
  void complete_order(Entity& entity);
  void clear_route(Order& order) const noexcept;
  [[nodiscard]] bool move_along_route(Entity& entity, Vec2 target);
  void move_toward(Entity& entity, Vec2 target) const noexcept;
  [[nodiscard]] bool attack_target(Entity& entity, bool chase);
  [[nodiscard]] EntityId nearest_enemy(PlayerId owner, Vec2 position, std::int32_t acquisition_range) const noexcept;
  [[nodiscard]] std::vector<Vec2> formation_targets(const std::vector<EntityId>& entities, Vec2 target) const;
  [[nodiscard]] std::vector<Vec2> find_path(Vec2 start, Vec2 goal, std::int32_t radius) const;
  [[nodiscard]] bool is_navigable(Vec2 position, std::int32_t radius) const noexcept;
  [[nodiscard]] bool segment_is_navigable(Vec2 start, Vec2 end, std::int32_t radius) const noexcept;
  [[nodiscard]] Vec2 nearest_navigable(Vec2 position, std::int32_t radius) const noexcept;
  [[nodiscard]] const Entity* nearest_command(PlayerId owner, Vec2 position) const noexcept;
  [[nodiscard]] std::int32_t queued_supply(PlayerId owner) const noexcept;
  [[nodiscard]] bool requires_supply_connection(
      const Entity& entity) const noexcept;
  [[nodiscard]] std::vector<CommandCapability> command_capabilities(PlayerId owner) const;
  [[nodiscard]] std::int32_t resolve_multiplier_basis(const Entity& entity) const noexcept;
  void apply_research_bonuses(Entity& entity, bool preserve_health);
  void emit_event(SimulationEventPayload payload);
  void rebuild_entity_index() noexcept;

  struct ResourceMemory {
    bool discovered{};
    std::int32_t amount{};
    Tick observed_tick{};
  };

  struct ControlPointMemory {
    bool observed{};
    std::optional<PlayerId> owner{};
    std::int32_t influence{};
    Tick observed_tick{};
  };

  struct QueuedCommand {
    Command command{};
    Tick issued_tick{};
    CommandSource source{CommandSource::External};
    std::uint64_t observation_hash{};
    std::uint64_t ai_decision_id{};
  };

  SimulationConfig config_{};
  Tick tick_{};
  MatchStatus status_{MatchStatus::Playing};
  std::optional<PlayerId> winner_{};
  std::array<PlayerState, 2> players_{};
  std::array<bool, 2> command_seen_{};
  std::array<VisibilityGrid, 2> visibility_{};
  std::array<std::vector<ResourceMemory>, 2> resource_memory_{};
  std::array<std::vector<ControlPointMemory>, 2> control_point_memory_{};
  std::array<std::vector<ObservedEnemy>, 2> enemy_memory_{};
  std::array<CommanderAI, 2> commanders_{CommanderAI{PlayerId::One}, CommanderAI{PlayerId::Two}};
  std::vector<Entity> entities_{};
  std::vector<std::size_t> entity_index_by_id_{};
  std::vector<ResourceNode> resources_{};
  std::vector<ControlPoint> control_points_{};
  std::vector<VowState> vows_{};
  ObjectiveSystem objective_system_{};
  CasualtySystem casualty_system_{};
  SupplySystem supply_system_{};
  SpatialGrid spatial_grid_{};
  std::vector<QueuedCommand> command_queue_{};
  std::vector<CommandTraceEntry> command_trace_{};
  std::vector<AIDecisionRecord> ai_decision_trace_{};
  std::vector<SimulationEvent> events_{};
  std::int32_t ruin_tide_{4};
  std::uint32_t next_entity_id_{1};
  std::uint32_t next_unit_identity_id_{1};
  std::uint32_t next_resource_id_{1};
  std::uint32_t next_control_point_id_{1};
  std::uint64_t next_sequence_{1};
  std::uint64_t next_ai_decision_id_{1};
  std::uint64_t next_event_id_{1};
  std::uint64_t event_digest_{14'695'981'039'346'656'037ULL};
};

}  // namespace ashen::core
