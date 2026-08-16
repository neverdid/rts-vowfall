#include "ashen/core/CommanderAI.hpp"

#include "AIPlanningInternal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
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

[[nodiscard]] std::uint64_t command_random(
    const PlayerObservation& observation, const AIDecisionLayer layer,
    const AIAction action) noexcept {
  auto value = observation.match_seed();
  value ^= observation.player() == PlayerId::One
               ? 0x9e3779b97f4a7c15ULL
               : 0xd1b54a32d192ed03ULL;
  value ^= observation.tick() * 0x94d049bb133111ebULL;
  value ^= (static_cast<std::uint64_t>(layer) + 1U) *
           0xbf58476d1ce4e5b9ULL;
  value ^= (static_cast<std::uint64_t>(action) + 1U) *
           0x369dea0f31a53f85ULL;
  value ^= value >> 30U;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27U;
  value *= 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

[[nodiscard]] bool uses_point_precision(const CommandType type) noexcept {
  switch (type) {
    case CommandType::Move:
    case CommandType::AttackMove:
    case CommandType::Patrol:
    case CommandType::SetRallyPoint:
    case CommandType::Retreat:
      return true;
    case CommandType::Attack:
    case CommandType::Gather:
    case CommandType::Train:
    case CommandType::Stop:
    case CommandType::Hold:
    case CommandType::Build:
    case CommandType::Research:
    case CommandType::ActivatePower:
    case CommandType::SetStance:
    case CommandType::MakeVow:
    case CommandType::KeepVow:
    case CommandType::BreakVow:
    case CommandType::AmendVow:
    case CommandType::RecoverCasualty:
      return false;
  }
  return false;
}

[[nodiscard]] std::uint64_t integer_sqrt(const std::uint64_t value) noexcept {
  auto remaining = value;
  std::uint64_t result = 0;
  std::uint64_t bit = std::uint64_t{1} << 62U;
  while (bit > remaining) {
    bit >>= 2U;
  }
  while (bit != 0) {
    if (remaining >= result + bit) {
      remaining -= result + bit;
      result = (result >> 1U) + bit;
    } else {
      result >>= 1U;
    }
    bit >>= 2U;
  }
  return result;
}

void apply_command_precision(AIPlannedDecision& decision,
                             const PlayerObservation& observation,
                             const AIDifficultyProfile& difficulty) noexcept {
  if (difficulty.command_precision_radius <= 0 ||
      !uses_point_precision(decision.command.type)) {
    return;
  }
  const auto span =
      static_cast<std::uint64_t>(difficulty.command_precision_radius) * 2U + 1U;
  const auto random =
      command_random(observation, decision.layer, decision.selected_action);
  auto offset_x =
      static_cast<std::int32_t>(random % span) -
      difficulty.command_precision_radius;
  auto offset_y =
      static_cast<std::int32_t>((random >> 32U) % span) -
      difficulty.command_precision_radius;
  const auto squared_offset =
      static_cast<std::uint64_t>(
          static_cast<std::int64_t>(offset_x) * offset_x +
          static_cast<std::int64_t>(offset_y) * offset_y);
  const auto squared_radius =
      static_cast<std::uint64_t>(difficulty.command_precision_radius) *
      difficulty.command_precision_radius;
  if (squared_offset > squared_radius) {
    const auto length = integer_sqrt(squared_offset);
    offset_x = static_cast<std::int32_t>(
        static_cast<std::int64_t>(offset_x) *
        difficulty.command_precision_radius /
        static_cast<std::int64_t>(length));
    offset_y = static_cast<std::int32_t>(
        static_cast<std::int64_t>(offset_y) *
        difficulty.command_precision_radius /
        static_cast<std::int64_t>(length));
  }
  const Vec2 perturbed{
      std::clamp(decision.command.target.x + offset_x, 0,
                 observation.map_size().x),
      std::clamp(decision.command.target.y + offset_y, 0,
                 observation.map_size().y),
  };
  decision.command_precision_offset = {
      perturbed.x - decision.command.target.x,
      perturbed.y - decision.command.target.y,
  };
  decision.command.target = perturbed;
}

}  // namespace

CommanderAI::CommanderAI(const PlayerId player,
                         const AIDifficulty difficulty) noexcept
    : player_(player),
      difficulty_(difficulty),
      strategy_state_(initial_ai_strategy_state(player)) {}

const AIDifficultyProfile& CommanderAI::difficulty_profile() const noexcept {
  return ai_difficulty_profile(difficulty_);
}

void CommanderAI::reset(const AIDifficulty difficulty) noexcept {
  difficulty_ = difficulty;
  strategy_state_ = initial_ai_strategy_state(player_);
  observation_history_.clear();
}

PlayerObservation CommanderAI::perceive(
    const PlayerObservation& observation) {
  if (observation.player() != player_) {
    observation_history_.clear();
    return observation.with_delayed_opponent_knowledge(
        nullptr, difficulty_profile().mobile_memory_ticks);
  }
  if (!observation_history_.empty() &&
      (observation_history_.back().match_seed() != observation.match_seed() ||
       observation.tick() < observation_history_.back().tick())) {
    observation_history_.clear();
  }
  if (!observation_history_.empty() &&
      observation_history_.back().tick() == observation.tick()) {
    observation_history_.back() = observation;
  } else {
    observation_history_.push_back(observation);
  }

  const auto& profile = difficulty_profile();
  const auto target_tick =
      observation.tick() >= profile.reaction_delay_ticks
          ? observation.tick() - profile.reaction_delay_ticks
          : Tick{};
  while (observation_history_.size() > 1 &&
         observation_history_[1].tick() <= target_tick) {
    observation_history_.pop_front();
  }
  const auto* delayed =
      observation.tick() >= profile.reaction_delay_ticks &&
              !observation_history_.empty() &&
              observation_history_.front().tick() <= target_tick
          ? &observation_history_.front()
          : nullptr;
  return observation.with_delayed_opponent_knowledge(
      delayed, profile.mobile_memory_ticks);
}

CommanderPlan CommanderAI::update(const PlayerObservation& observation) {
  const auto perceived = perceive(observation);
  strategy_state_ = update_ai_strategy_state(strategy_state_, perceived);
  return plan(perceived);
}

CommanderPlan CommanderAI::plan(const PlayerObservation& observation) const {
  CommanderPlan result{};
  if (observation.player() != player_ || observation.status() != MatchStatus::Playing) {
    return result;
  }

  const auto& difficulty = difficulty_profile();
  const auto strategic_due =
      ai_decision_due(AIDecisionLayer::Strategic, observation.tick(),
                      difficulty);
  const auto tactical_due =
      ai_decision_due(AIDecisionLayer::Tactical, observation.tick(),
                      difficulty);
  const auto micro_due =
      ai_decision_due(AIDecisionLayer::Micro, observation.tick(), difficulty);
  if (!strategic_due && !tactical_due && !micro_due) {
    return result;
  }

  const ai::PlanningContext context{observation, tactical_due, difficulty};
  if (strategic_due) {
    auto strategic = ai::evaluate_strategic_layer(context);
    for (auto& decision : strategic) {
      result.decisions.push_back(std::move(decision));
    }
  }
  if (tactical_due) {
    if (auto tactical = ai::evaluate_tactical_layer(context)) {
      result.decisions.push_back(std::move(*tactical));
    }
  }
  if (micro_due) {
    if (auto micro = ai::evaluate_micro_layer(context)) {
      result.decisions.push_back(std::move(*micro));
    }
  }
  const auto strategy_state_hash =
      ai_strategy_state_hash(strategy_state_);
  for (auto& decision : result.decisions) {
    decision.command_latency_ticks = difficulty.command_latency_ticks;
    decision.strategy_state_hash = strategy_state_hash;
    apply_command_precision(decision, observation, difficulty);
  }
  return result;
}

std::vector<Command> CommanderAI::decide(const PlayerObservation& observation) const {
  auto planned = plan(observation);
  std::vector<Command> commands;
  commands.reserve(planned.decisions.size());
  for (auto& decision : planned.decisions) {
    commands.push_back(std::move(decision.command));
  }
  return commands;
}

std::uint64_t CommanderAI::state_hash() const noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, static_cast<std::uint8_t>(player_));
  hash_integral(hash, static_cast<std::uint8_t>(difficulty_));
  hash_integral(hash, ai_difficulty_hash(difficulty_profile()));
  hash_integral(hash, ai_strategy_state_hash(strategy_state_));
  hash_integral(hash, observation_history_.size());
  for (const auto& observation : observation_history_) {
    hash_integral(hash, observation.hash());
  }
  return hash;
}

}  // namespace ashen::core
