#include "ashen/core/SimulationEvent.hpp"

#include <cstddef>
#include <type_traits>
#include <variant>

namespace ashen::core {
namespace {

inline constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
inline constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

template <typename Value>
void hash_integral(std::uint64_t& hash, const Value value) noexcept {
  auto bits = static_cast<std::uint64_t>(value);
  for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
    hash ^= bits & 0xffU;
    hash *= kFnvPrime;
    bits >>= 8U;
  }
}

void hash_entity(std::uint64_t& hash, const EntityId value) noexcept {
  hash_integral(hash, value.value);
}

void hash_optional_player(std::uint64_t& hash,
                          const std::optional<PlayerId> value) noexcept {
  hash_integral(
      hash, value.has_value() ? static_cast<std::uint8_t>(*value) + 1U : 0U);
}

}  // namespace

SimulationEventType event_type(const SimulationEvent& event) noexcept {
  return static_cast<SimulationEventType>(event.payload.index());
}

std::uint64_t simulation_event_hash(const SimulationEvent& event) noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, event.id.value);
  hash_integral(hash, event.tick);
  hash_integral(hash, static_cast<std::uint8_t>(event_type(event)));
  std::visit(
      [&](const auto& payload) {
        using Payload = std::decay_t<decltype(payload)>;
        if constexpr (std::is_same_v<Payload, EntitySpawnedEvent> ||
                      std::is_same_v<Payload, EntityDestroyedEvent>) {
          hash_entity(hash, payload.entity);
          hash_integral(hash, static_cast<std::uint8_t>(payload.owner));
          hash_integral(hash, static_cast<std::uint8_t>(payload.faction));
          hash_integral(hash, static_cast<std::uint8_t>(payload.archetype));
          hash_integral(hash, payload.identity.value);
        } else if constexpr (std::is_same_v<Payload, UnitDamagedEvent>) {
          hash_entity(hash, payload.source);
          hash_entity(hash, payload.target);
          hash_integral(hash, payload.amount);
          hash_integral(hash, payload.remaining_hit_points);
          hash_integral(hash, payload.identity.value);
        } else if constexpr (std::is_same_v<Payload, UnitWoundedEvent>) {
          hash_entity(hash, payload.entity);
          hash_entity(hash, payload.source);
          hash_integral(hash, payload.remaining_hit_points);
          hash_integral(hash, payload.identity.value);
          hash_integral(hash, static_cast<std::uint8_t>(payload.previous));
          hash_integral(hash, static_cast<std::uint8_t>(payload.current));
        } else if constexpr (std::is_same_v<Payload, UnitKilledEvent>) {
          hash_entity(hash, payload.entity);
          hash_entity(hash, payload.killer);
          hash_integral(hash, payload.identity.value);
          hash_integral(hash, static_cast<std::uint8_t>(payload.previous));
          hash_integral(hash, static_cast<std::uint8_t>(payload.current));
        } else if constexpr (std::is_same_v<Payload, UnitRecoveredEvent>) {
          hash_entity(hash, payload.entity);
          hash_entity(hash, payload.recovery_source);
          hash_integral(hash, payload.identity.value);
          hash_integral(hash, static_cast<std::uint8_t>(payload.previous));
          hash_integral(hash, static_cast<std::uint8_t>(payload.current));
        } else if constexpr (std::is_same_v<Payload, FormationCreatedEvent>) {
          hash_integral(hash, payload.formation.value);
          hash_integral(hash, static_cast<std::uint8_t>(payload.owner));
        } else if constexpr (std::is_same_v<Payload, FormationBrokenEvent>) {
          hash_integral(hash, payload.formation.value);
        } else if constexpr (
            std::is_same_v<Payload, ResolveThresholdChangedEvent>) {
          hash_entity(hash, payload.entity);
          hash_integral(hash, static_cast<std::uint8_t>(payload.previous));
          hash_integral(hash, static_cast<std::uint8_t>(payload.current));
          hash_integral(hash, payload.resolve);
        } else if constexpr (std::is_same_v<Payload, SupplyConnectedEvent> ||
                             std::is_same_v<Payload,
                                            SupplyDisconnectedEvent>) {
          hash_entity(hash, payload.entity);
        } else if constexpr (std::is_same_v<Payload, VowMadeEvent> ||
                             std::is_same_v<Payload, VowKeptEvent> ||
                             std::is_same_v<Payload, VowBrokenEvent>) {
          hash_integral(hash, payload.vow.value);
          hash_integral(hash, static_cast<std::uint8_t>(payload.maker));
        } else if constexpr (std::is_same_v<Payload, VowAmendedEvent>) {
          hash_integral(hash, payload.vow.value);
          hash_integral(hash, static_cast<std::uint8_t>(payload.maker));
          hash_integral(
              hash,
              static_cast<std::uint8_t>(payload.participating_affected_player));
          hash_integral(hash, payload.revision);
        } else if constexpr (
            std::is_same_v<Payload, TransformationStartedEvent> ||
            std::is_same_v<Payload, TransformationCompletedEvent>) {
          hash_integral(hash, payload.transformation.value);
          hash_entity(hash, payload.entity);
          hash_integral(hash, payload.definition);
        } else if constexpr (
            std::is_same_v<Payload, TestimonyDiscoveredEvent>) {
          hash_integral(hash, payload.testimony);
          hash_integral(hash, static_cast<std::uint8_t>(payload.discoverer));
        } else if constexpr (
            std::is_same_v<Payload, ObjectiveContestedEvent>) {
          hash_integral(hash, payload.objective.value);
        } else if constexpr (
            std::is_same_v<Payload, ObjectiveCapturedEvent>) {
          hash_integral(hash, payload.objective.value);
          hash_optional_player(hash, payload.previous_owner);
          hash_integral(hash, static_cast<std::uint8_t>(payload.owner));
        } else if constexpr (
            std::is_same_v<Payload, ProjectileLaunchedEvent>) {
          hash_integral(hash, payload.projectile);
          hash_entity(hash, payload.source);
          hash_entity(hash, payload.target);
        } else if constexpr (std::is_same_v<Payload, AbilityStartedEvent>) {
          hash_integral(hash, payload.ability);
          hash_integral(hash, static_cast<std::uint8_t>(payload.owner));
          hash_entity(hash, payload.source);
        } else if constexpr (
            std::is_same_v<Payload, AbilityInterruptedEvent>) {
          hash_integral(hash, payload.ability);
          hash_entity(hash, payload.source);
          hash_entity(hash, payload.interrupter);
        } else if constexpr (
            std::is_same_v<Payload, MissionObjectiveChangedEvent>) {
          hash_integral(hash, payload.objective);
          hash_integral(hash, static_cast<std::uint8_t>(payload.previous));
          hash_integral(hash, static_cast<std::uint8_t>(payload.current));
        }
      },
      event.payload);
  return hash;
}

}  // namespace ashen::core
