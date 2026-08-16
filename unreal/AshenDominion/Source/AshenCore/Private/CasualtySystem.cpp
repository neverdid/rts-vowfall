#include "ashen/core/CasualtySystem.hpp"

#include <cstddef>
#include <limits>
#include <vector>

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

void hash_vec(std::uint64_t& hash, const Vec2 value) noexcept {
  hash_integral(hash, value.x);
  hash_integral(hash, value.y);
}

[[nodiscard]] Tick deadline_after(const Tick tick,
                                  const Tick duration) noexcept {
  return tick > std::numeric_limits<Tick>::max() - duration
             ? std::numeric_limits<Tick>::max()
             : tick + duration;
}

[[nodiscard]] bool requires_live_entity(const CasualtyState state) noexcept {
  return state == CasualtyState::Active || state == CasualtyState::Wounded ||
         state == CasualtyState::Recovered;
}

}  // namespace

void CasualtySystem::reset() noexcept {
  records_.clear();
  transitions_.clear();
}

bool CasualtySystem::register_unit(const Entity& entity, const Tick tick) {
  if (entity.kind != EntityKind::Unit || !entity.identity ||
      entity.identity.value != records_.size() + 1U || !entity.alive() ||
      entity.casualty_state != CasualtyState::Active) {
    return false;
  }
  records_.push_back(CasualtyRecord{
      entity.identity,
      entity.id,
      entity.owner,
      entity.faction,
      entity.type,
      {},
      CasualtyState::Active,
      0,
      0,
      entity.position,
      tick,
      0,
      {},
  });
  return true;
}

const CasualtyRecord* CasualtySystem::find(const UnitIdentityId identity) const noexcept {
  if (!identity || identity.value > records_.size()) {
    return nullptr;
  }
  const auto& record = records_[identity.value - 1U];
  return record.identity == identity ? &record : nullptr;
}

CasualtyRecord* CasualtySystem::find_mutable(const UnitIdentityId identity) noexcept {
  return const_cast<CasualtyRecord*>(
      static_cast<const CasualtySystem&>(*this).find(identity));
}

bool CasualtySystem::mark_wounded(Entity& entity, const EntityId source,
                                  const Tick tick) {
  if (entity.casualty_state != CasualtyState::Active &&
      entity.casualty_state != CasualtyState::Recovered) {
    return false;
  }
  return transition(entity, CasualtyState::Wounded, source, tick);
}

bool CasualtySystem::mark_incapacitated(Entity& entity, const EntityId source,
                                        const Tick tick) {
  if (entity.alive() ||
      (entity.casualty_state != CasualtyState::Active &&
       entity.casualty_state != CasualtyState::Wounded &&
       entity.casualty_state != CasualtyState::Recovered)) {
    return false;
  }
  return transition(entity, CasualtyState::Incapacitated, source, tick);
}

bool CasualtySystem::recover(Entity& entity, const EntityId source,
                             const Tick tick) {
  auto* record = find_mutable(entity.identity);
  if (record == nullptr || entity.kind != EntityKind::Unit || !entity.alive() ||
      !entity.id || entity.id == record->last_entity ||
      entity.owner != record->owner || entity.faction != record->faction ||
      entity.type != record->archetype ||
      entity.casualty_state != CasualtyState::Recoverable ||
      record->state != CasualtyState::Recoverable ||
      record->state_deadline <= tick) {
    return false;
  }

  record->last_entity = entity.id;
  record->state = CasualtyState::Recovered;
  record->last_transition_position = entity.position;
  record->state_since = tick;
  record->state_deadline = 0;
  record->last_source = source;
  entity.casualty_state = CasualtyState::Recovered;
  transitions_.push_back(CasualtyTransition{
      entity.identity, entity.id, CasualtyState::Recoverable,
      CasualtyState::Recovered, tick, 0, source, entity.position});
  return true;
}

bool CasualtySystem::mark_dead(Entity& entity, const EntityId source,
                               const Tick tick) {
  if (entity.casualty_state == CasualtyState::Dead) {
    return false;
  }
  return transition(entity, CasualtyState::Dead, source, tick);
}

void CasualtySystem::advance(const Tick tick) {
  for (auto& record : records_) {
    if (record.state_deadline == 0 || tick < record.state_deadline) {
      continue;
    }
    if (record.state == CasualtyState::Incapacitated) {
      transition(record, CasualtyState::Recoverable, tick,
                 deadline_after(tick, kBaseRecoveryWindowTicks));
    } else if (record.state == CasualtyState::Recoverable) {
      transition(record, CasualtyState::Dead, tick, 0);
    }
  }
}

bool CasualtySystem::is_recoverable(const UnitIdentityId identity,
                                    const Tick tick) const noexcept {
  const auto* record = find(identity);
  return record != nullptr && record->state == CasualtyState::Recoverable &&
         record->state_deadline > tick;
}

bool CasualtySystem::transition(Entity& entity, const CasualtyState current,
                                const EntityId source, const Tick tick) {
  auto* record = find_mutable(entity.identity);
  if (record == nullptr || record->last_entity != entity.id ||
      record->state != entity.casualty_state || record->state == current) {
    return false;
  }

  const auto previous = record->state;
  record->state = current;
  record->last_transition_position = entity.position;
  record->state_since = tick;
  record->state_deadline =
      current == CasualtyState::Incapacitated
          ? deadline_after(tick, kCasualtyStabilizationTicks)
          : 0;
  record->last_source = source;
  if (current == CasualtyState::Wounded) {
    ++record->injuries;
  }
  entity.casualty_state = current;
  transitions_.push_back(CasualtyTransition{
      entity.identity, entity.id, previous, current, tick,
      record->state_deadline, source, entity.position});
  return true;
}

void CasualtySystem::transition(CasualtyRecord& record,
                                const CasualtyState current, const Tick tick,
                                const Tick state_deadline) {
  const auto previous = record.state;
  record.state = current;
  record.state_since = tick;
  record.state_deadline = state_deadline;
  transitions_.push_back(CasualtyTransition{
      record.identity, record.last_entity, previous, current, tick,
      state_deadline, record.last_source, record.last_transition_position});
}

std::uint64_t CasualtySystem::state_hash() const noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, records_.size());
  for (const auto& record : records_) {
    hash_integral(hash, record.identity.value);
    hash_integral(hash, record.last_entity.value);
    hash_integral(hash, static_cast<std::uint8_t>(record.owner));
    hash_integral(hash, static_cast<std::uint8_t>(record.faction));
    hash_integral(hash, static_cast<std::uint8_t>(record.archetype));
    hash_integral(hash, record.formation.value);
    hash_integral(hash, static_cast<std::uint8_t>(record.state));
    hash_integral(hash, record.experience);
    hash_integral(hash, record.injuries);
    hash_vec(hash, record.last_transition_position);
    hash_integral(hash, record.state_since);
    hash_integral(hash, record.state_deadline);
    hash_integral(hash, record.last_source.value);
  }
  hash_integral(hash, transitions_.size());
  for (const auto& transition : transitions_) {
    hash_integral(hash, transition.identity.value);
    hash_integral(hash, transition.entity.value);
    hash_integral(hash, static_cast<std::uint8_t>(transition.previous));
    hash_integral(hash, static_cast<std::uint8_t>(transition.current));
    hash_integral(hash, transition.tick);
    hash_integral(hash, transition.state_deadline);
    hash_integral(hash, transition.source.value);
    hash_vec(hash, transition.position);
  }
  return hash;
}

bool CasualtySystem::derivation_matches(
    const std::span<const Entity> live_entities,
    const std::uint32_t next_identity,
    const Tick simulation_tick) const noexcept {
  if (next_identity == 0 ||
      static_cast<std::uint64_t>(next_identity) != records_.size() + 1ULL) {
    return false;
  }

  try {
    std::vector<CasualtyState> states(records_.size(), CasualtyState::Active);
    std::vector<std::uint32_t> injuries(records_.size(), 0);
    std::vector<Tick> state_since(records_.size(), 0);
    std::vector<Tick> state_deadlines(records_.size(), 0);
    std::vector<EntityId> sources(records_.size());
    std::vector<EntityId> current_entities(records_.size());
    std::vector<bool> live_seen(records_.size(), false);
    std::vector<Vec2> positions;
    positions.reserve(records_.size());

    for (std::size_t index = 0; index < records_.size(); ++index) {
      const auto& record = records_[index];
      if (record.identity.value != index + 1U || !record.last_entity ||
          record.state_since > simulation_tick ||
          ((record.state == CasualtyState::Incapacitated ||
            record.state == CasualtyState::Recoverable) !=
           (record.state_deadline != 0)) ||
          record.formation || record.experience != 0 ||
          record.archetype == EntityType::Command ||
          record.archetype == EntityType::Barracks ||
          record.archetype == EntityType::Turret) {
        return false;
      }
      state_since[index] = record.state_since;
      state_deadlines[index] = record.state_deadline;
      positions.push_back(record.last_transition_position);
    }

    Tick previous_tick{};
    for (const auto& transition : transitions_) {
      if (!transition.identity ||
          transition.identity.value > records_.size() ||
          !transition.entity || transition.tick < previous_tick ||
          transition.tick > simulation_tick ||
          transition.previous == transition.current) {
        return false;
      }
      const auto index = static_cast<std::size_t>(transition.identity.value - 1U);
      const auto allowed =
          (transition.previous == CasualtyState::Active &&
           (transition.current == CasualtyState::Wounded ||
            transition.current == CasualtyState::Incapacitated ||
            transition.current == CasualtyState::Dead)) ||
          (transition.previous == CasualtyState::Wounded &&
           (transition.current == CasualtyState::Incapacitated ||
            transition.current == CasualtyState::Dead)) ||
          (transition.previous == CasualtyState::Incapacitated &&
           transition.current == CasualtyState::Recoverable) ||
          (transition.previous == CasualtyState::Recoverable &&
           (transition.current == CasualtyState::Recovered ||
            transition.current == CasualtyState::Dead)) ||
          (transition.previous == CasualtyState::Recovered &&
           (transition.current == CasualtyState::Wounded ||
            transition.current == CasualtyState::Incapacitated ||
            transition.current == CasualtyState::Dead));
      if (!allowed || states[index] != transition.previous) {
        return false;
      }
      if (!current_entities[index]) {
        if (transition.previous != CasualtyState::Active) {
          return false;
        }
        current_entities[index] = transition.entity;
      } else if (transition.previous == CasualtyState::Recoverable &&
                 transition.current == CasualtyState::Recovered) {
        if (transition.entity.value <= current_entities[index].value) {
          return false;
        }
        current_entities[index] = transition.entity;
      } else if (current_entities[index] != transition.entity) {
        return false;
      }
      const auto expected_deadline =
          transition.current == CasualtyState::Incapacitated
              ? deadline_after(transition.tick, kCasualtyStabilizationTicks)
          : transition.current == CasualtyState::Recoverable
              ? deadline_after(transition.tick, kBaseRecoveryWindowTicks)
              : 0;
      if (transition.state_deadline != expected_deadline) {
        return false;
      }
      states[index] = transition.current;
      injuries[index] += transition.current == CasualtyState::Wounded ? 1U : 0U;
      state_since[index] = transition.tick;
      state_deadlines[index] = transition.state_deadline;
      sources[index] = transition.source;
      positions[index] = transition.position;
      previous_tick = transition.tick;
    }

    for (std::size_t index = 0; index < records_.size(); ++index) {
      const auto& record = records_[index];
      if (!current_entities[index]) {
        current_entities[index] = record.last_entity;
      }
      if (record.state != states[index] || record.injuries != injuries[index] ||
          record.state_since != state_since[index] ||
          record.state_deadline != state_deadlines[index] ||
          record.last_source != sources[index] ||
          record.last_transition_position != positions[index] ||
          record.last_entity != current_entities[index]) {
        return false;
      }
    }

    for (const auto& entity : live_entities) {
      if (entity.kind == EntityKind::Building) {
        if (entity.identity || entity.casualty_state != CasualtyState::Active) {
          return false;
        }
        continue;
      }
      const auto* record = find(entity.identity);
      if (record == nullptr || record->last_entity != entity.id ||
          record->owner != entity.owner || record->faction != entity.faction ||
          record->archetype != entity.type ||
          record->state != entity.casualty_state ||
          !requires_live_entity(record->state)) {
        return false;
      }
      const auto index = static_cast<std::size_t>(entity.identity.value - 1U);
      if (live_seen[index]) {
        return false;
      }
      live_seen[index] = true;
    }
    for (std::size_t index = 0; index < records_.size(); ++index) {
      if (live_seen[index] != requires_live_entity(records_[index].state)) {
        return false;
      }
    }
  } catch (...) {
    return false;
  }
  return true;
}

}  // namespace ashen::core
