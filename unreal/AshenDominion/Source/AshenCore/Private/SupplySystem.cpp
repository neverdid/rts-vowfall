#include "ashen/core/SupplySystem.hpp"

#include <algorithm>
#include <cstddef>
#include <ranges>
#include <tuple>
#include <utility>

namespace ashen::core {
namespace {

constexpr std::uint64_t kFnvOffset = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t kFnvPrime = 1'099'511'628'211ULL;

template <typename Value>
void hash_integral(std::uint64_t& hash, const Value value) noexcept {
  auto bits = static_cast<std::uint64_t>(value);
  for (std::size_t byte = 0; byte < sizeof(Value); ++byte) {
    hash ^= bits & 0xffU;
    hash *= kFnvPrime;
    bits >>= 8U;
  }
}

struct RuntimeNode {
  const Entity* entity{};
  const SupplyNodeContentDefinition* definition{};
};

struct SourceCapacity {
  EntityId source{};
  std::int32_t remaining{};
};

struct Candidate {
  std::uint32_t hops{};
  EntityId target{};
  EntityId source{};
  EntityId predecessor{};
};

[[nodiscard]] auto candidate_key(const Candidate& candidate) noexcept {
  return std::tuple{candidate.hops, candidate.target.value,
                    candidate.source.value, candidate.predecessor.value};
}

[[nodiscard]] const RuntimeNode* find_runtime_node(
    const std::vector<RuntimeNode>& nodes, const EntityId entity) noexcept {
  const auto found = std::ranges::lower_bound(
      nodes, entity.value, {},
      [](const RuntimeNode& node) { return node.entity->id.value; });
  return found != nodes.end() && found->entity->id == entity ? &*found
                                                             : nullptr;
}

[[nodiscard]] SupplyNodeState* find_state(
    std::vector<SupplyNodeState>& states, const EntityId entity) noexcept {
  const auto found = std::ranges::lower_bound(
      states, entity.value, {},
      [](const SupplyNodeState& state) { return state.entity.value; });
  return found != states.end() && found->entity == entity ? &*found : nullptr;
}

[[nodiscard]] SourceCapacity* find_capacity(
    std::vector<SourceCapacity>& capacities,
    const EntityId source) noexcept {
  const auto found = std::ranges::lower_bound(
      capacities, source.value, {},
      [](const SourceCapacity& capacity) { return capacity.source.value; });
  return found != capacities.end() && found->source == source ? &*found
                                                              : nullptr;
}

}  // namespace

void SupplySystem::reset() noexcept {
  states_.clear();
}

std::vector<SupplyNodeState> SupplySystem::solve(
    const std::span<const Entity> entities, const SpatialGrid& spatial_grid,
    const ContentRegistry& content) {
  std::vector<RuntimeNode> nodes;
  nodes.reserve(entities.size());
  for (const auto& entity : entities) {
    if (!entity.alive() || entity.under_construction) {
      continue;
    }
    const auto* definition =
        find_supply_node_content(content, entity.faction, entity.type);
    if (definition != nullptr) {
      nodes.push_back({&entity, definition});
    }
  }
  std::ranges::sort(nodes, {}, [](const RuntimeNode& node) {
    return node.entity->id.value;
  });

  std::vector<SupplyNodeState> states;
  states.reserve(nodes.size());
  std::vector<SourceCapacity> capacities;
  std::vector<Candidate> frontier;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> attempted;
  std::vector<SpatialQueryHit> nearby;

  for (const auto& node : nodes) {
    const auto source = node.definition->source ? node.entity->id : EntityId{};
    states.push_back({node.entity->id, source, {}, 0,
                      node.definition->demand,
                      node.definition->source});
    if (node.definition->source) {
      capacities.push_back({node.entity->id, node.definition->capacity});
    }
  }

  const auto enqueue_neighbors = [&](const RuntimeNode& transmitter,
                                     const EntityId source,
                                     const std::uint32_t hops) {
    spatial_grid.query_radius(transmitter.entity->position,
                              transmitter.definition->link_range, nearby);
    for (const auto& hit : nearby) {
      if (hit.id == transmitter.entity->id) {
        continue;
      }
      const auto* target = find_runtime_node(nodes, hit.id);
      if (target == nullptr ||
          target->entity->owner != transmitter.entity->owner ||
          target->definition->source) {
        continue;
      }
      frontier.push_back(
          {hops + 1U, target->entity->id, source,
           transmitter.entity->id});
    }
  };

  for (const auto& node : nodes) {
    if (node.definition->source) {
      enqueue_neighbors(node, node.entity->id, 0);
    }
  }

  while (!frontier.empty()) {
    const auto best = std::ranges::min_element(
        frontier, {}, [](const Candidate& value) {
          return candidate_key(value);
        });
    const auto candidate = *best;
    frontier.erase(best);

    auto* target_state = find_state(states, candidate.target);
    const auto* target_node = find_runtime_node(nodes, candidate.target);
    if (target_state == nullptr || target_node == nullptr ||
        target_state->connected) {
      continue;
    }
    const auto attempt =
        std::pair{candidate.source.value, candidate.target.value};
    if (std::ranges::find(attempted, attempt) != attempted.end()) {
      continue;
    }
    attempted.push_back(attempt);

    auto* capacity = find_capacity(capacities, candidate.source);
    if (capacity == nullptr || capacity->remaining < target_state->demand) {
      continue;
    }
    capacity->remaining -= target_state->demand;
    target_state->connected = true;
    target_state->source = candidate.source;
    target_state->predecessor = candidate.predecessor;
    target_state->hops = candidate.hops;

    if (target_node->definition->relay) {
      enqueue_neighbors(*target_node, candidate.source, candidate.hops);
    }
  }
  return states;
}

std::vector<SupplyTransition> SupplySystem::evaluate(
    const std::span<const Entity> entities, const SpatialGrid& spatial_grid,
    const ContentRegistry& content) {
  auto next = solve(entities, spatial_grid, content);
  std::vector<SupplyTransition> transitions;
  std::size_t previous_index{};
  std::size_t next_index{};
  while (previous_index < states_.size() || next_index < next.size()) {
    if (next_index == next.size() ||
        (previous_index < states_.size() &&
         states_[previous_index].entity < next[next_index].entity)) {
      if (states_[previous_index].connected) {
        transitions.push_back({states_[previous_index].entity, false});
      }
      ++previous_index;
      continue;
    }
    if (previous_index == states_.size() ||
        next[next_index].entity < states_[previous_index].entity) {
      if (next[next_index].connected) {
        transitions.push_back({next[next_index].entity, true});
      }
      ++next_index;
      continue;
    }
    if (states_[previous_index].connected != next[next_index].connected) {
      transitions.push_back(
          {next[next_index].entity, next[next_index].connected});
    }
    ++previous_index;
    ++next_index;
  }
  states_ = std::move(next);
  return transitions;
}

void SupplySystem::rebuild(const std::span<const Entity> entities,
                           const SpatialGrid& spatial_grid,
                           const ContentRegistry& content) {
  states_ = solve(entities, spatial_grid, content);
}

bool SupplySystem::derivation_matches(
    const std::span<const Entity> entities, const SpatialGrid& spatial_grid,
    const ContentRegistry& content) const {
  return states_ == solve(entities, spatial_grid, content);
}

const SupplyNodeState* SupplySystem::find(const EntityId entity) const noexcept {
  const auto found = std::ranges::lower_bound(
      states_, entity.value, {},
      [](const SupplyNodeState& state) { return state.entity.value; });
  return found != states_.end() && found->entity == entity ? &*found : nullptr;
}

bool SupplySystem::has_node(const EntityId entity) const noexcept {
  return find(entity) != nullptr;
}

bool SupplySystem::connected(const EntityId entity) const noexcept {
  const auto* state = find(entity);
  return state != nullptr && state->connected;
}

std::uint64_t SupplySystem::state_hash() const noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, states_.size());
  for (const auto& state : states_) {
    hash_integral(hash, state.entity.value);
    hash_integral(hash, state.source.value);
    hash_integral(hash, state.predecessor.value);
    hash_integral(hash, state.hops);
    hash_integral(hash, state.demand);
    hash_integral(hash, state.connected);
  }
  return hash;
}

}  // namespace ashen::core
