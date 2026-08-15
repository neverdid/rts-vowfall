#pragma once

#include "ashen/core/Types.hpp"

#include <compare>
#include <cstdint>
#include <optional>
#include <variant>

namespace ashen::core {

using StableContentId = std::uint32_t;

enum class SimulationEventType : std::uint8_t {
  EntitySpawned,
  EntityDestroyed,
  UnitDamaged,
  UnitWounded,
  UnitKilled,
  UnitRecovered,
  FormationCreated,
  FormationBroken,
  ResolveThresholdChanged,
  SupplyConnected,
  SupplyDisconnected,
  VowMade,
  VowKept,
  VowAmended,
  VowBroken,
  TransformationStarted,
  TransformationCompleted,
  TestimonyDiscovered,
  ObjectiveContested,
  ObjectiveCaptured,
  ProjectileLaunched,
  AbilityStarted,
  AbilityInterrupted,
  MissionObjectiveChanged,
};

enum class MissionObjectiveStatus : std::uint8_t {
  Inactive,
  Active,
  Succeeded,
  Failed,
};

struct EntitySpawnedEvent {
  EntityId entity{};
  PlayerId owner{PlayerId::One};
  FactionId faction{FactionId::Compact};
  EntityType archetype{EntityType::Worker};
  UnitIdentityId identity{};

  auto operator<=>(const EntitySpawnedEvent&) const = default;
};

struct EntityDestroyedEvent {
  EntityId entity{};
  PlayerId owner{PlayerId::One};
  FactionId faction{FactionId::Compact};
  EntityType archetype{EntityType::Worker};
  UnitIdentityId identity{};

  auto operator<=>(const EntityDestroyedEvent&) const = default;
};

struct UnitDamagedEvent {
  EntityId source{};
  EntityId target{};
  std::int32_t amount{};
  std::int32_t remaining_hit_points{};
  UnitIdentityId identity{};

  auto operator<=>(const UnitDamagedEvent&) const = default;
};

struct UnitWoundedEvent {
  EntityId entity{};
  EntityId source{};
  std::int32_t remaining_hit_points{};
  UnitIdentityId identity{};
  CasualtyState previous{CasualtyState::Active};
  CasualtyState current{CasualtyState::Wounded};

  auto operator<=>(const UnitWoundedEvent&) const = default;
};

struct UnitKilledEvent {
  EntityId entity{};
  EntityId killer{};
  UnitIdentityId identity{};
  CasualtyState previous{CasualtyState::Active};
  CasualtyState current{CasualtyState::Dead};

  auto operator<=>(const UnitKilledEvent&) const = default;
};

struct UnitRecoveredEvent {
  EntityId entity{};
  EntityId recovery_source{};
  UnitIdentityId identity{};
  CasualtyState previous{CasualtyState::Recoverable};
  CasualtyState current{CasualtyState::Recovered};

  auto operator<=>(const UnitRecoveredEvent&) const = default;
};

struct FormationCreatedEvent {
  FormationId formation{};
  PlayerId owner{PlayerId::One};

  auto operator<=>(const FormationCreatedEvent&) const = default;
};

struct FormationBrokenEvent {
  FormationId formation{};

  auto operator<=>(const FormationBrokenEvent&) const = default;
};

struct ResolveThresholdChangedEvent {
  EntityId entity{};
  ResolveState previous{ResolveState::Steady};
  ResolveState current{ResolveState::Steady};
  std::int32_t resolve{};

  auto operator<=>(const ResolveThresholdChangedEvent&) const = default;
};

struct SupplyConnectedEvent {
  EntityId entity{};

  auto operator<=>(const SupplyConnectedEvent&) const = default;
};

struct SupplyDisconnectedEvent {
  EntityId entity{};

  auto operator<=>(const SupplyDisconnectedEvent&) const = default;
};

struct VowMadeEvent {
  VowId vow{};
  PlayerId maker{PlayerId::One};

  auto operator<=>(const VowMadeEvent&) const = default;
};

struct VowKeptEvent {
  VowId vow{};
  PlayerId maker{PlayerId::One};

  auto operator<=>(const VowKeptEvent&) const = default;
};

struct VowAmendedEvent {
  VowId vow{};
  PlayerId maker{PlayerId::One};
  PlayerId participating_affected_player{PlayerId::Two};
  std::uint32_t revision{};

  auto operator<=>(const VowAmendedEvent&) const = default;
};

struct VowBrokenEvent {
  VowId vow{};
  PlayerId maker{PlayerId::One};

  auto operator<=>(const VowBrokenEvent&) const = default;
};

struct TransformationStartedEvent {
  TransformationId transformation{};
  EntityId entity{};
  StableContentId definition{};

  auto operator<=>(const TransformationStartedEvent&) const = default;
};

struct TransformationCompletedEvent {
  TransformationId transformation{};
  EntityId entity{};
  StableContentId definition{};

  auto operator<=>(const TransformationCompletedEvent&) const = default;
};

struct TestimonyDiscoveredEvent {
  StableContentId testimony{};
  PlayerId discoverer{PlayerId::One};

  auto operator<=>(const TestimonyDiscoveredEvent&) const = default;
};

struct ObjectiveContestedEvent {
  ControlPointId objective{};

  auto operator<=>(const ObjectiveContestedEvent&) const = default;
};

struct ObjectiveCapturedEvent {
  ControlPointId objective{};
  std::optional<PlayerId> previous_owner{};
  PlayerId owner{PlayerId::One};

  auto operator<=>(const ObjectiveCapturedEvent&) const = default;
};

struct ProjectileLaunchedEvent {
  StableContentId projectile{};
  EntityId source{};
  EntityId target{};

  auto operator<=>(const ProjectileLaunchedEvent&) const = default;
};

struct AbilityStartedEvent {
  StableContentId ability{};
  PlayerId owner{PlayerId::One};
  EntityId source{};

  auto operator<=>(const AbilityStartedEvent&) const = default;
};

struct AbilityInterruptedEvent {
  StableContentId ability{};
  EntityId source{};
  EntityId interrupter{};

  auto operator<=>(const AbilityInterruptedEvent&) const = default;
};

struct MissionObjectiveChangedEvent {
  StableContentId objective{};
  MissionObjectiveStatus previous{MissionObjectiveStatus::Inactive};
  MissionObjectiveStatus current{MissionObjectiveStatus::Inactive};

  auto operator<=>(const MissionObjectiveChangedEvent&) const = default;
};

using SimulationEventPayload =
    std::variant<EntitySpawnedEvent, EntityDestroyedEvent, UnitDamagedEvent,
                 UnitWoundedEvent, UnitKilledEvent, UnitRecoveredEvent,
                 FormationCreatedEvent, FormationBrokenEvent,
                 ResolveThresholdChangedEvent, SupplyConnectedEvent,
                 SupplyDisconnectedEvent, VowMadeEvent, VowKeptEvent,
                 VowAmendedEvent, VowBrokenEvent, TransformationStartedEvent,
                 TransformationCompletedEvent, TestimonyDiscoveredEvent,
                 ObjectiveContestedEvent, ObjectiveCapturedEvent,
                 ProjectileLaunchedEvent, AbilityStartedEvent,
                 AbilityInterruptedEvent, MissionObjectiveChangedEvent>;

struct SimulationEvent {
  EventId id{};
  Tick tick{};
  SimulationEventPayload payload{};

  auto operator<=>(const SimulationEvent&) const = default;
};

[[nodiscard]] ASHENCORE_API SimulationEventType event_type(
    const SimulationEvent& event) noexcept;
[[nodiscard]] ASHENCORE_API std::uint64_t simulation_event_hash(
    const SimulationEvent& event) noexcept;

}  // namespace ashen::core
