#include "ashen/core/ObjectiveSystem.hpp"

#include <algorithm>
#include <cstddef>
#include <limits>

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

bool condition_met(const MissionObjectiveCondition condition,
                   const MissionObjectiveState& state,
                   const MissionObjectiveContext& context) noexcept {
  switch (condition.trigger) {
    case MissionObjectiveTrigger::None:
      return false;
    case MissionObjectiveTrigger::PlayerOneCommandDestroyed:
      return context.command_seen[0] && !context.command_alive[0];
    case MissionObjectiveTrigger::PlayerTwoCommandDestroyed:
      return context.command_seen[1] && !context.command_alive[1];
    case MissionObjectiveTrigger::PlayerOneControlsAllObjectives:
      return context.objective_count > 0 &&
             context.player_one_controlled_objectives ==
                 context.objective_count;
    case MissionObjectiveTrigger::ElapsedTicks:
      return context.tick >= state.activated_tick &&
             context.tick - state.activated_tick >= condition.target_tick;
  }
  return false;
}

}  // namespace

void ObjectiveSystem::reset(const SimulationConfig& config) {
  scenario_ = find_scenario(config);
  initialize_states();
}

void ObjectiveSystem::initialize_states() {
  states_.clear();
  if (scenario_ == nullptr) {
    return;
  }
  states_.reserve(scenario_->objectives.size());
  for (const auto& objective : scenario_->objectives) {
    states_.push_back({objective.content_id,
                       objective.prerequisite.has_value()
                           ? MissionObjectiveStatus::Inactive
                           : MissionObjectiveStatus::Active,
                       0, 0});
  }
}

std::vector<MissionObjectiveTransition> ObjectiveSystem::evaluate(
    const MissionObjectiveContext& context) {
  std::vector<MissionObjectiveTransition> transitions;
  if (scenario_ == nullptr) {
    return transitions;
  }
  for (std::size_t index = 0; index < states_.size(); ++index) {
    auto& state = states_[index];
    if (state.status != MissionObjectiveStatus::Active) {
      continue;
    }
    const auto& definition = scenario_->objectives[index];
    auto current = state.status;
    if (condition_met(definition.failure, state, context)) {
      current = MissionObjectiveStatus::Failed;
    } else if (condition_met(definition.success, state, context)) {
      current = MissionObjectiveStatus::Succeeded;
    }
    if (current != state.status) {
      transitions.push_back(
          {state.content_id, state.status, current, definition.primary,
           definition.required});
      state.status = current;
      state.resolved_tick = context.tick;
    }
  }
  for (std::size_t index = 0; index < states_.size(); ++index) {
    auto& state = states_[index];
    const auto& definition = scenario_->objectives[index];
    if (state.status != MissionObjectiveStatus::Inactive ||
        !definition.prerequisite.has_value()) {
      continue;
    }
    const auto* prerequisite = find_state(*definition.prerequisite);
    if (prerequisite == nullptr ||
        prerequisite->status != MissionObjectiveStatus::Succeeded) {
      continue;
    }
    transitions.push_back({state.content_id, state.status,
                           MissionObjectiveStatus::Active,
                           definition.primary, definition.required});
    state.status = MissionObjectiveStatus::Active;
    state.activated_tick = context.tick;
    state.resolved_tick = 0;
  }
  return transitions;
}

bool ObjectiveSystem::rebuild(
    const std::span<const SimulationEvent> events) {
  initialize_states();
  for (const auto& event : events) {
    if (event_type(event) != SimulationEventType::MissionObjectiveChanged) {
      continue;
    }
    const auto& transition =
        std::get<MissionObjectiveChangedEvent>(event.payload);
    if (scenario_ == nullptr) {
      return false;
    }
    auto* state = find_state_mutable(transition.objective);
    const auto definition =
        std::ranges::find(scenario_->objectives, transition.objective,
                          &MissionObjectiveDefinition::content_id);
    if (state == nullptr || state->status != transition.previous ||
        definition == scenario_->objectives.end() ||
        event.tick == std::numeric_limits<Tick>::max()) {
      return false;
    }
    const auto valid_transition =
        (transition.previous == MissionObjectiveStatus::Inactive &&
         transition.current == MissionObjectiveStatus::Active) ||
        (transition.previous == MissionObjectiveStatus::Active &&
         (transition.current == MissionObjectiveStatus::Succeeded ||
          transition.current == MissionObjectiveStatus::Failed));
    if (!valid_transition) {
      return false;
    }
    const auto transition_tick = event.tick + 1;
    if (transition.previous == MissionObjectiveStatus::Active &&
        transition_tick < state->activated_tick) {
      return false;
    }
    if (transition.current == MissionObjectiveStatus::Active) {
      const auto* prerequisite =
          definition->prerequisite.has_value()
              ? find_state(*definition->prerequisite)
              : nullptr;
      if (prerequisite == nullptr ||
          prerequisite->status != MissionObjectiveStatus::Succeeded ||
          transition_tick < prerequisite->resolved_tick) {
        return false;
      }
    }
    state->status = transition.current;
    if (transition.current == MissionObjectiveStatus::Active) {
      state->activated_tick = transition_tick;
      state->resolved_tick = 0;
    } else {
      state->resolved_tick = transition_tick;
    }
  }
  return true;
}

bool ObjectiveSystem::event_projection_matches(
    const SimulationConfig& config,
    const std::span<const SimulationEvent> events) const {
  ObjectiveSystem projection;
  projection.reset(config);
  return projection.rebuild(events) && projection.states_ == states_;
}

std::vector<MissionObjectiveView> ObjectiveSystem::views(
    const Tick tick) const {
  std::vector<MissionObjectiveView> result;
  if (scenario_ == nullptr) {
    return result;
  }
  result.reserve(states_.size());
  for (std::size_t index = 0; index < states_.size(); ++index) {
    const auto& definition = scenario_->objectives[index];
    auto target_tick = Tick{};
    if (definition.success.trigger == MissionObjectiveTrigger::ElapsedTicks &&
        definition.success.target_tick <=
            std::numeric_limits<Tick>::max() - states_[index].activated_tick) {
      target_tick =
          states_[index].activated_tick + definition.success.target_tick;
    }
    result.push_back({states_[index].content_id, definition.label,
                      states_[index].status, definition.primary,
                      definition.required,
                      static_cast<std::uint32_t>(index + 1U),
                      static_cast<std::uint32_t>(states_.size()), tick,
                      target_tick});
  }
  return result;
}

std::optional<MissionObjectiveView> ObjectiveSystem::primary_view(
    const Tick tick) const {
  const auto objective_views = views(tick);
  auto found = std::ranges::find_if(
      objective_views, [](const auto& objective) {
        return objective.required &&
               objective.status == MissionObjectiveStatus::Active;
      });
  if (found != objective_views.end()) {
    return *found;
  }
  found = std::ranges::find_if(
      objective_views, [](const auto& objective) {
        return objective.required &&
               objective.status == MissionObjectiveStatus::Failed;
      });
  if (found != objective_views.end()) {
    return *found;
  }
  found = std::ranges::find_if(
      objective_views, [](const auto& objective) { return objective.primary; });
  return found == objective_views.end() ? std::nullopt
                                        : std::optional{*found};
}

std::uint64_t ObjectiveSystem::state_hash() const noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, states_.size());
  for (const auto& state : states_) {
    hash_integral(hash, state.content_id);
    hash_integral(hash, static_cast<std::uint8_t>(state.status));
    hash_integral(hash, state.activated_tick);
    hash_integral(hash, state.resolved_tick);
  }
  return hash;
}

bool ObjectiveSystem::all_required_succeeded() const noexcept {
  if (scenario_ == nullptr) {
    return false;
  }
  for (std::size_t index = 0; index < states_.size(); ++index) {
    if (scenario_->objectives[index].required &&
        states_[index].status != MissionObjectiveStatus::Succeeded) {
      return false;
    }
  }
  return !states_.empty();
}

bool ObjectiveSystem::outcome_matches(
    const MatchMode mode, const MatchStatus status,
    const std::optional<PlayerId> winner) const noexcept {
  if (scenario_ == nullptr) {
    return true;
  }
  auto required_failed = false;
  for (std::size_t index = 0; index < states_.size(); ++index) {
    required_failed = required_failed ||
                      (scenario_->objectives[index].required &&
                       states_[index].status == MissionObjectiveStatus::Failed);
  }
  if (required_failed) {
    const auto expected_status =
        mode == MatchMode::Story ? MatchStatus::Lost : MatchStatus::Won;
    return status == expected_status && winner == PlayerId::Two;
  }
  if (all_required_succeeded()) {
    return status == MatchStatus::Won && winner == PlayerId::One;
  }
  return status == MatchStatus::Playing && !winner.has_value();
}

MissionObjectiveState* ObjectiveSystem::find_state_mutable(
    const StableContentId content_id) noexcept {
  const auto found = std::ranges::find(states_, content_id,
                                       &MissionObjectiveState::content_id);
  return found == states_.end() ? nullptr : &*found;
}

const MissionObjectiveState* ObjectiveSystem::find_state(
    const StableContentId content_id) const noexcept {
  const auto found = std::ranges::find(states_, content_id,
                                       &MissionObjectiveState::content_id);
  return found == states_.end() ? nullptr : &*found;
}

}  // namespace ashen::core
