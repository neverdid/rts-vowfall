#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

#ifndef ASHENCORE_API
#define ASHENCORE_API
#endif

namespace ashen::core {

using Tick = std::uint64_t;

inline constexpr std::int32_t kWorldScale = 1'000;
inline constexpr std::int32_t kTicksPerSecond = 20;

struct EntityId {
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const EntityId&) const = default;
};

struct UnitIdentityId {
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const UnitIdentityId&) const = default;
};

struct ResourceId {
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const ResourceId&) const = default;
};

struct ControlPointId {
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const ControlPointId&) const = default;
};

struct EventId {
  std::uint64_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const EventId&) const = default;
};

struct FormationId {
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const FormationId&) const = default;
};

struct VowId {
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const VowId&) const = default;
};

struct TransformationId {
  std::uint32_t value{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return value != 0; }
  auto operator<=>(const TransformationId&) const = default;
};

struct Vec2 {
  std::int32_t x{};
  std::int32_t y{};

  auto operator<=>(const Vec2&) const = default;
};

[[nodiscard]] constexpr Vec2 world(const std::int32_t x, const std::int32_t y) noexcept {
  return {x * kWorldScale, y * kWorldScale};
}

enum class PlayerId : std::uint8_t { One, Two };
enum class FactionId : std::uint8_t { Compact, Ascendancy, Concord };
enum class AIDifficulty : std::uint8_t { Story, Standard, Veteran, Competitive };
enum class MatchMode : std::uint8_t { Story, Skirmish, PvP };
enum class StoryMissionId : std::uint8_t {
  BridgeOfNames,
  OpenBowl,
  MercyForTheUncounted,
  SecondTelling,
  CityThatWasWrittenOff,
  LastContradiction,
  RiverWithTwoHistories,
  EmergencyWithoutEnd,
  KindestPrison,
  OldestVeto,
  FieldOfNails,
  CouncilUnderFire,
  NamesAtTheWater,
  Count,
};
enum class MatchStatus : std::uint8_t { Playing, Won, Lost };
enum class CommandSource : std::uint8_t { External, CommanderAI };
enum class VisibilityState : std::uint8_t { Hidden, Explored, Visible };
enum class EntityKind : std::uint8_t { Unit, Building };
enum class EntityType : std::uint8_t { Worker, Vanguard, Skirmisher, Command, Barracks, Turret };
enum class ArmorClass : std::uint8_t { Laborer, Armored, Light, Structure };
enum class CasualtyState : std::uint8_t {
  Active,
  Wounded,
  Incapacitated,
  Recoverable,
  Recovered,
  Missing,
  Dead,
};
enum class ResearchId : std::uint8_t {
  TierTwo,
  TemperedOaths,
  Wardcraft,
  ChorusOfKnives,
  PitBroods,
  VaultPlate,
  SiegeLiturgy,
};
enum class UnitStance : std::uint8_t { Aggressive, Defensive, Hold };
enum class ResolveState : std::uint8_t { Steady, Strained, Wavering, Broken, Rallied };
enum class VowResolution : std::uint8_t { Unresolved, Kept, Broken, Amended };
enum class OrderType : std::uint8_t { Idle, Move, Attack, AttackMove, Gather, Build, Patrol, Hold };
enum class GatherPhase : std::uint8_t { ToResource, Harvest, Return };
enum class CommandType : std::uint8_t {
  Move,
  Attack,
  AttackMove,
  Gather,
  Train,
  Stop,
  Hold,
  Patrol,
  SetRallyPoint,
  Build,
  Research,
  ActivatePower,
  Retreat,
  SetStance,
  MakeVow,
  KeepVow,
  BreakVow,
  AmendVow,
  RecoverCasualty,
};
enum class CommandError : std::uint8_t {
  None,
  InvalidOwner,
  InvalidEntity,
  InvalidTarget,
  InvalidProducer,
  InvalidUnitType,
  InsufficientOre,
  SupplyBlocked,
  PlacementBlocked,
  UnderConstruction,
  QueueFull,
  PrerequisiteMissing,
  AlreadyResearched,
  ResearchBusy,
  PowerCooldown,
  InvalidVow,
  VowAlreadyExists,
  VowAlreadyResolved,
  VowAuthorityRequired,
  CasualtyUnavailable,
};

inline constexpr std::size_t kResearchCount = 7;

[[nodiscard]] constexpr std::size_t research_index(const ResearchId research) noexcept {
  return static_cast<std::size_t>(research);
}

[[nodiscard]] constexpr std::size_t player_index(const PlayerId player) noexcept {
  return static_cast<std::size_t>(player);
}

[[nodiscard]] constexpr PlayerId enemy_of(const PlayerId player) noexcept {
  return player == PlayerId::One ? PlayerId::Two : PlayerId::One;
}

struct EntityDefinition {
  EntityType type{EntityType::Worker};
  EntityKind kind{EntityKind::Unit};
  std::string_view label{};
  std::int32_t cost{};
  Tick build_ticks{};
  std::int32_t hit_points{};
  std::int32_t radius{};
  std::int32_t speed_per_tick{};
  std::int32_t attack_range{};
  std::int32_t damage{};
  Tick attack_cooldown_ticks{};
  std::int32_t sight{};
  ArmorClass armor{ArmorClass::Laborer};
  ArmorClass bonus_against{ArmorClass::Laborer};
  bool has_damage_bonus{};
  std::int32_t bonus_damage{};
  std::int32_t terror{};
  std::int32_t ward{};
  std::int32_t supply_cost{};
  std::int32_t supply_provided{};
};

struct FactionDefinition {
  FactionId id{FactionId::Compact};
  std::string_view name{};
  std::int32_t income_basis_points{10'000};
  std::int32_t resolve_drift{};
};

struct ResearchDefinition {
  ResearchId id{ResearchId::TierTwo};
  std::optional<FactionId> faction{};
  std::string_view label{};
  std::int32_t cost{};
  Tick research_ticks{};
  EntityType producer{EntityType::Command};
  std::optional<ResearchId> prerequisite{};
};

struct PowerDefinition {
  FactionId faction{FactionId::Compact};
  std::string_view label{};
  std::int32_t cost{};
  Tick cooldown_ticks{};
};

struct Order {
  OrderType type{OrderType::Idle};
  Vec2 target{};
  Vec2 secondary_target{};
  EntityId target_entity{};
  ResourceId resource{};
  GatherPhase gather_phase{GatherPhase::ToResource};
  Tick phase_ticks{};
  Vec2 route_goal{};
  std::vector<Vec2> route{};
  std::size_t route_index{};
};

struct ProductionTask {
  EntityType type{EntityType::Worker};
  Tick remaining_ticks{};
  Tick total_ticks{};
};

struct ResearchTask {
  ResearchId id{ResearchId::TierTwo};
  Tick remaining_ticks{};
  Tick total_ticks{};
};

struct Entity {
  EntityId id{};
  UnitIdentityId identity{};
  CasualtyState casualty_state{CasualtyState::Active};
  PlayerId owner{PlayerId::One};
  FactionId faction{FactionId::Compact};
  EntityType type{EntityType::Worker};
  EntityKind kind{EntityKind::Unit};
  Vec2 position{};
  std::int32_t radius{};
  std::int32_t hit_points{};
  std::int32_t max_hit_points{};
  std::int32_t speed_per_tick{};
  std::int32_t attack_range{};
  std::int32_t damage{};
  Tick attack_cooldown_ticks{};
  Tick cooldown_ticks{};
  ArmorClass armor{ArmorClass::Laborer};
  ArmorClass bonus_against{ArmorClass::Laborer};
  bool has_damage_bonus{};
  std::int32_t bonus_damage{};
  std::int32_t sight{};
  std::int32_t terror{};
  std::int32_t ward{};
  std::int32_t resolve{100};
  ResolveState resolve_state{ResolveState::Steady};
  std::int32_t supply_cost{};
  std::int32_t supply_provided{};
  std::int32_t carrying{};
  Order order{};
  std::vector<Order> order_queue{};
  Vec2 rally_point{};
  std::vector<ProductionTask> production_queue{};
  UnitStance stance{UnitStance::Aggressive};
  Vec2 guard_position{};
  bool under_construction{};
  Tick construction_ticks{};
  Tick construction_total_ticks{};

  [[nodiscard]] bool alive() const noexcept { return hit_points > 0; }
};

struct ResourceNode {
  ResourceId id{};
  Vec2 position{};
  std::int32_t radius{};
  std::int32_t amount{};
};

struct PlayerState {
  PlayerId id{PlayerId::One};
  FactionId faction{FactionId::Compact};
  std::int32_t ore{260};
  std::int32_t supply_used{};
  std::int32_t supply_cap{};
  std::int32_t resolve{100};
  Tick power_cooldown_ticks{};
  std::uint8_t tech_tier{1};
  std::array<bool, kResearchCount> researched{};
  std::vector<ResearchTask> research_queue{};
};

struct ControlPoint {
  ControlPointId id{};
  Vec2 position{};
  std::int32_t radius{};
  std::optional<PlayerId> owner{};
  std::int32_t influence{};
  std::int32_t income_progress{};
  bool contested{};
};

struct NavigationObstacle {
  Vec2 minimum{};
  Vec2 maximum{};

  auto operator<=>(const NavigationObstacle&) const = default;
};

struct SimulationConfig {
  MatchMode mode{MatchMode::Skirmish};
  StoryMissionId story_mission{StoryMissionId::BridgeOfNames};
  FactionId player_one_faction{FactionId::Compact};
  FactionId player_two_faction{FactionId::Ascendancy};
  std::array<bool, 2> commander_players{false, false};
  std::array<AIDifficulty, 2> commander_difficulties{
      AIDifficulty::Competitive, AIDifficulty::Competitive};
  std::array<std::int32_t, 2> starting_ore{260, 260};
  std::uint64_t match_seed{1};
  Vec2 map_size{world(2'400, 1'400)};
  std::int32_t visibility_cell_size{world(24, 0).x};
  std::int32_t navigation_cell_size{world(36, 0).x};
  std::int32_t spatial_cell_size{world(160, 0).x};
  std::vector<NavigationObstacle> navigation_obstacles{
      // River banks leave three broad, readable crossings.
      {world(1'115, 0), world(1'285, 315)},
      {world(1'115, 445), world(1'285, 635)},
      {world(1'115, 765), world(1'285, 955)},
      {world(1'115, 1'085), world(1'285, 1'400)},
      // The northwest mountain and southeast Gravewood are rotationally balanced.
      {world(500, 250), world(820, 480)},
      {world(650, 370), world(930, 610)},
      {world(1'580, 920), world(1'900, 1'150)},
      {world(1'470, 790), world(1'750, 1'030)},
  };
  bool seed_starting_forces{true};
};

struct Command {
  Tick execute_tick{};
  std::uint64_t sequence{};
  PlayerId player{PlayerId::One};
  CommandType type{CommandType::Move};
  std::vector<EntityId> entities{};
  Vec2 target{};
  EntityId target_entity{};
  ResourceId resource{};
  EntityId producer{};
  EntityType train_type{EntityType::Worker};
  EntityType building_type{EntityType::Barracks};
  ResearchId research{ResearchId::TierTwo};
  UnitStance stance{UnitStance::Aggressive};
  VowId vow{};
  UnitIdentityId casualty{};
  bool queue{};

  auto operator<=>(const Command&) const = default;
};

struct CommandResult {
  bool ok{};
  CommandError error{CommandError::None};
  std::string_view reason{};

  [[nodiscard]] constexpr explicit operator bool() const noexcept { return ok; }
};

struct CommandTraceEntry {
  Tick issued_tick{};
  Tick applied_tick{};
  CommandSource source{CommandSource::External};
  std::uint64_t observation_hash{};
  std::uint64_t ai_decision_id{};
  Command command{};
  bool accepted{};
  CommandError error{CommandError::None};

  auto operator<=>(const CommandTraceEntry&) const = default;
};

struct VowState {
  VowId id{};
  PlayerId maker{PlayerId::One};
  VowResolution resolution{VowResolution::Unresolved};
  Tick made_tick{};
  Tick resolved_tick{};
  std::uint32_t revision{1};
  std::optional<PlayerId> participating_affected_player{};

  auto operator<=>(const VowState&) const = default;
};

}  // namespace ashen::core
