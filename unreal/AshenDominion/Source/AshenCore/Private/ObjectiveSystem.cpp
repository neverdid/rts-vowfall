#include "ashen/core/ObjectiveSystem.hpp"

#include <algorithm>
#include <cstddef>

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
                   const MissionObjectiveContext& context) noexcept {
  switch (condition.trigger) {
    case MissionObjectiveTrigger::None:
      return false;
    case MissionObjectiveTrigger::PlayerOneCommandDestroyed:
      return context.command_seen[0] && !context.command_alive[0];
    case MissionObjectiveTrigger::PlayerTwoCommandDestroyed:
      return context.command_seen[1] && !context.command_alive[1];
    case MissionObjectiveTrigger::TickReached:
      return context.tick >= condition.target_tick;
  }
  return false;
}

}  // namespace

void ObjectiveSystem::reset(const SimulationConfig& config) {
  scenario_ = find_scenario(config);
  states_.clear();
  if (scenario_ == nullptr) {
    return;
  }
  states_.reserve(scenario_->objectives.size());
  for (const auto& objective : scenario_->objectives) {
    states_.push_back(
        {objective.content_id, MissionObjectiveStatus::Active});
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
    if (condition_met(definition.failure, context)) {
      current = MissionObjectiveStatus::Failed;
    } else if (condition_met(definition.success, context)) {
      current = MissionObjectiveStatus::Succeeded;
    }
    if (current != state.status) {
      transitions.push_back(
          {state.content_id, state.status, current, definition.primary});
      state.status = current;
    }
  }
  return transitions;
}

void ObjectiveSystem::rebuild(const MatchStatus match_status,
                              const std::optional<PlayerId> winner) {
  for (auto& state : states_) {
    state.status = MissionObjectiveStatus::Active;
  }
  if (match_status == MatchStatus::Playing || !winner.has_value()) {
    return;
  }
  const auto resolved = *winner == PlayerId::One
                            ? MissionObjectiveStatus::Succeeded
                            : MissionObjectiveStatus::Failed;
  for (std::size_t index = 0; index < states_.size(); ++index) {
    if (scenario_->objectives[index].primary) {
      states_[index].status = resolved;
    }
  }
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
    const auto target_tick =
        definition.success.trigger == MissionObjectiveTrigger::TickReached
            ? definition.success.target_tick
            : Tick{};
    result.push_back({states_[index].content_id, definition.label,
                      states_[index].status, definition.primary, tick,
                      target_tick});
  }
  return result;
}

std::optional<MissionObjectiveView> ObjectiveSystem::primary_view(
    const Tick tick) const {
  const auto objective_views = views(tick);
  const auto found = std::ranges::find_if(
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
  }
  return hash;
}

}  // namespace ashen::core
