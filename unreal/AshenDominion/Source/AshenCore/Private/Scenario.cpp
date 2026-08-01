#include "ashen/core/Scenario.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace ashen::core {
namespace {

constexpr MissionObjectiveCondition player_one_command_destroyed{
    MissionObjectiveTrigger::PlayerOneCommandDestroyed, 0};
constexpr MissionObjectiveCondition player_two_command_destroyed{
    MissionObjectiveTrigger::PlayerTwoCommandDestroyed, 0};
constexpr Tick kBridgeHoldTicks = 60 * kTicksPerSecond;

constexpr std::array skirmish_objectives{
    MissionObjectiveDefinition{content_id::SkirmishVictoryObjective,
                               "Destroy the rival command keep", true,
                               true, std::nullopt,
                               player_two_command_destroyed,
                               player_one_command_destroyed}};
constexpr std::array bridge_objectives{
    MissionObjectiveDefinition{
        content_id::BridgeApproachesObjective,
        "Secure both approaches to the Bridge of Names", false, true,
        std::nullopt,
        {MissionObjectiveTrigger::PlayerOneControlsAllObjectives, 0},
        player_one_command_destroyed},
    MissionObjectiveDefinition{
        content_id::BridgeObjective,
        "Keep the crossing open while the refugee column reaches Greywake",
        true, true, content_id::BridgeApproachesObjective,
        {MissionObjectiveTrigger::ElapsedTicks, kBridgeHoldTicks},
        player_one_command_destroyed}};

const std::array scenarios{
    ScenarioDefinition{"skirmish", MatchMode::Skirmish, std::nullopt,
                       skirmish_objectives},
    ScenarioDefinition{"pvp", MatchMode::PvP, std::nullopt,
                       skirmish_objectives},
    ScenarioDefinition{"bridge_of_names", MatchMode::Story,
                       StoryMissionId::BridgeOfNames, bridge_objectives},
};

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

void hash_text(std::uint64_t& hash, const std::string_view text) noexcept {
  hash_integral(hash, text.size());
  for (const auto character : text) {
    hash_integral(hash, static_cast<std::uint8_t>(character));
  }
}

bool has_objective_content(const ContentRegistry& content,
                           const StableContentId id) {
  return std::ranges::any_of(content.objectives, [&](const auto& objective) {
    return objective.metadata.stable_id == id;
  });
}

bool valid_condition(const MissionObjectiveCondition condition) noexcept {
  if (condition.trigger == MissionObjectiveTrigger::None) {
    return false;
  }
  if (condition.trigger == MissionObjectiveTrigger::ElapsedTicks) {
    return condition.target_tick > 0;
  }
  return condition.target_tick == 0;
}

const MissionObjectiveDefinition* find_objective(
    const ScenarioDefinition& scenario,
    const StableContentId content_id) noexcept {
  const auto found = std::ranges::find_if(
      scenario.objectives, [&](const auto& objective) {
        return objective.content_id == content_id;
      });
  return found == scenario.objectives.end() ? nullptr : &*found;
}

}  // namespace

std::span<const ScenarioDefinition> builtin_scenarios() noexcept {
  return scenarios;
}

const ScenarioDefinition* find_scenario(
    const SimulationConfig& config) noexcept {
  const auto found = std::ranges::find_if(scenarios, [&](const auto& scenario) {
    return scenario.mode == config.mode &&
           (scenario.mode != MatchMode::Story ||
            scenario.story_mission == config.story_mission);
  });
  return found == scenarios.end() ? nullptr : &*found;
}

std::vector<ScenarioValidationIssue> validate_scenarios(
    const std::span<const ScenarioDefinition> definitions,
    const ContentRegistry& content) {
  std::vector<ScenarioValidationIssue> issues;
  for (std::size_t index = 0; index < definitions.size(); ++index) {
    const auto& scenario = definitions[index];
    if (scenario.development_name.empty()) {
      issues.push_back({ScenarioValidationError::MissingDevelopmentName,
                        scenario.development_name, 0});
    }
    if (scenario.objectives.empty()) {
      issues.push_back({ScenarioValidationError::MissingObjectives,
                        scenario.development_name, 0});
    }
    for (std::size_t other = index + 1; other < definitions.size(); ++other) {
      if (scenario.mode == definitions[other].mode &&
          scenario.story_mission == definitions[other].story_mission) {
        issues.push_back({ScenarioValidationError::DuplicateScenarioKey,
                          scenario.development_name, 0});
      }
    }

    std::size_t primary_count{};
    for (std::size_t objective_index = 0;
         objective_index < scenario.objectives.size(); ++objective_index) {
      const auto& objective = scenario.objectives[objective_index];
      primary_count += objective.primary ? 1U : 0U;
      if (!has_objective_content(content, objective.content_id)) {
        issues.push_back({ScenarioValidationError::MissingObjectiveContent,
                          scenario.development_name, objective.content_id});
      }
      if (objective.label.empty()) {
        issues.push_back({ScenarioValidationError::MissingObjectiveLabel,
                          scenario.development_name, objective.content_id});
      }
      if (objective.primary && !objective.required) {
        issues.push_back({ScenarioValidationError::OptionalPrimaryObjective,
                          scenario.development_name, objective.content_id});
      }
      if (objective.prerequisite.has_value() &&
          find_objective(scenario, *objective.prerequisite) == nullptr) {
        issues.push_back(
            {ScenarioValidationError::MissingObjectivePrerequisite,
             scenario.development_name, objective.content_id});
      }
      if (objective.required && objective.prerequisite.has_value()) {
        const auto* prerequisite =
            find_objective(scenario, *objective.prerequisite);
        if (prerequisite != nullptr && !prerequisite->required) {
          issues.push_back(
              {ScenarioValidationError::RequiredObjectiveDependsOnOptional,
               scenario.development_name, objective.content_id});
        }
      }
      if (!valid_condition(objective.success) ||
          !valid_condition(objective.failure) ||
          objective.success == objective.failure) {
        issues.push_back({ScenarioValidationError::InvalidObjectiveCondition,
                          scenario.development_name, objective.content_id});
      }
      for (std::size_t other = objective_index + 1;
           other < scenario.objectives.size(); ++other) {
        if (objective.content_id == scenario.objectives[other].content_id) {
          issues.push_back({ScenarioValidationError::DuplicateObjective,
                            scenario.development_name, objective.content_id});
        }
      }
    }
    for (const auto& objective : scenario.objectives) {
      auto prerequisite = objective.prerequisite;
      for (std::size_t depth = 0;
           prerequisite.has_value() && depth <= scenario.objectives.size();
           ++depth) {
        if (*prerequisite == objective.content_id) {
          issues.push_back(
              {ScenarioValidationError::CyclicObjectivePrerequisite,
               scenario.development_name, objective.content_id});
          break;
        }
        const auto* dependency = find_objective(scenario, *prerequisite);
        if (dependency == nullptr) {
          break;
        }
        prerequisite = dependency->prerequisite;
      }
    }
    if (primary_count == 0) {
      issues.push_back({ScenarioValidationError::MissingPrimaryObjective,
                        scenario.development_name, 0});
    } else if (primary_count > 1) {
      issues.push_back({ScenarioValidationError::DuplicatePrimaryObjective,
                        scenario.development_name, 0});
    }
  }
  return issues;
}

std::uint64_t scenario_catalog_digest() noexcept {
  auto hash = kFnvOffset;
  hash_integral(hash, scenarios.size());
  for (const auto& scenario : scenarios) {
    hash_text(hash, scenario.development_name);
    hash_integral(hash, static_cast<std::uint8_t>(scenario.mode));
    hash_integral(
        hash,
        scenario.story_mission.has_value()
            ? static_cast<std::uint8_t>(*scenario.story_mission) + 1U
            : 0U);
    hash_integral(hash, scenario.objectives.size());
    for (const auto& objective : scenario.objectives) {
      hash_integral(hash, objective.content_id);
      hash_text(hash, objective.label);
      hash_integral(hash, objective.primary);
      hash_integral(hash, objective.required);
      hash_integral(hash,
                    objective.prerequisite.has_value()
                        ? static_cast<std::uint64_t>(*objective.prerequisite) + 1U
                        : 0U);
      hash_integral(hash, static_cast<std::uint8_t>(objective.success.trigger));
      hash_integral(hash, objective.success.target_tick);
      hash_integral(hash, static_cast<std::uint8_t>(objective.failure.trigger));
      hash_integral(hash, objective.failure.target_tick);
    }
  }
  return hash;
}

}  // namespace ashen::core
