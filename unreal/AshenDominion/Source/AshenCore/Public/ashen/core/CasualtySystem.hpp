#pragma once

#include "ashen/core/Types.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace ashen::core {

class SnapshotCodec;

inline constexpr Tick kCasualtyStabilizationTicks = 40;
inline constexpr Tick kBaseRecoveryWindowTicks = 400;

struct CasualtyRecord {
  UnitIdentityId identity{};
  EntityId last_entity{};
  PlayerId owner{PlayerId::One};
  FactionId faction{FactionId::Compact};
  EntityType archetype{EntityType::Worker};
  FormationId formation{};
  CasualtyState state{CasualtyState::Active};
  std::uint32_t experience{};
  std::uint32_t injuries{};
  Vec2 last_transition_position{};
  Tick state_since{};
  Tick state_deadline{};
  EntityId last_source{};

  auto operator<=>(const CasualtyRecord&) const = default;
};

struct CasualtyTransition {
  UnitIdentityId identity{};
  EntityId entity{};
  CasualtyState previous{CasualtyState::Active};
  CasualtyState current{CasualtyState::Active};
  Tick tick{};
  Tick state_deadline{};
  EntityId source{};
  Vec2 position{};

  auto operator<=>(const CasualtyTransition&) const = default;
};

class ASHENCORE_API CasualtySystem final {
 public:
  void reset() noexcept;
  [[nodiscard]] bool register_unit(const Entity& entity, Tick tick);
  [[nodiscard]] bool mark_wounded(Entity& entity, EntityId source, Tick tick);
  [[nodiscard]] bool mark_incapacitated(Entity& entity, EntityId source,
                                        Tick tick);
  [[nodiscard]] bool recover(Entity& entity, EntityId source, Tick tick);
  [[nodiscard]] bool mark_dead(Entity& entity, EntityId source, Tick tick);
  void advance(Tick tick);

  [[nodiscard]] const CasualtyRecord* find(UnitIdentityId identity) const noexcept;
  [[nodiscard]] bool is_recoverable(UnitIdentityId identity,
                                    Tick tick) const noexcept;
  [[nodiscard]] std::span<const CasualtyRecord> records() const noexcept {
    return records_;
  }
  [[nodiscard]] std::span<const CasualtyTransition> transitions() const noexcept {
    return transitions_;
  }
  [[nodiscard]] std::uint64_t state_hash() const noexcept;
  [[nodiscard]] bool derivation_matches(std::span<const Entity> live_entities,
                                        std::uint32_t next_identity,
                                        Tick simulation_tick) const noexcept;

 private:
  friend class SnapshotCodec;

  [[nodiscard]] CasualtyRecord* find_mutable(UnitIdentityId identity) noexcept;
  [[nodiscard]] bool transition(Entity& entity, CasualtyState current,
                                EntityId source, Tick tick);
  void transition(CasualtyRecord& record, CasualtyState current, Tick tick,
                  Tick state_deadline);

  std::vector<CasualtyRecord> records_{};
  std::vector<CasualtyTransition> transitions_{};
};

}  // namespace ashen::core
