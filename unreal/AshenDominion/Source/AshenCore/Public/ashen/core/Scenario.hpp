#pragma once

#include "ashen/core/Content.hpp"
#include "ashen/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace ashen::core {

enum class MissionObjectiveTrigger : std::uint8_t {
  None,
  PlayerOneCommandDestroyed,
  PlayerTwoCommandDestroyed,
  TickReached,
};

struct MissionObjectiveCondition {
  MissionObjectiveTrigger trigger{MissionObjectiveTrigger::None};
  Tick target_tick{};

  auto operator<=>(const MissionObjectiveCondition&) const = default;
};

struct MissionObjectiveDefinition {
  StableContentId content_id{};
  std::string_view label{};
  bool primary{};
  MissionObjectiveCondition success{};
  MissionObjectiveCondition failure{};
};

struct ScenarioDefinition {
  std::string_view development_name{};
  MatchMode mode{MatchMode::Skirmish};
  std::optional<StoryMissionId> story_mission{};
  std::span<const MissionObjectiveDefinition> objectives{};
};

enum class ScenarioValidationError : std::uint8_t {
  DuplicateScenarioKey,
  MissingDevelopmentName,
  MissingObjectives,
  MissingPrimaryObjective,
  DuplicatePrimaryObjective,
  DuplicateObjective,
  MissingObjectiveContent,
  MissingObjectiveLabel,
  InvalidObjectiveCondition,
};

struct ScenarioValidationIssue {
  ScenarioValidationError error{ScenarioValidationError::DuplicateScenarioKey};
  std::string_view scenario{};
  StableContentId objective{};
};

[[nodiscard]] ASHENCORE_API std::span<const ScenarioDefinition>
builtin_scenarios() noexcept;
[[nodiscard]] ASHENCORE_API const ScenarioDefinition* find_scenario(
    const SimulationConfig& config) noexcept;
[[nodiscard]] ASHENCORE_API std::vector<ScenarioValidationIssue>
validate_scenarios(std::span<const ScenarioDefinition> scenarios,
                   const ContentRegistry& content);
[[nodiscard]] ASHENCORE_API std::uint64_t scenario_catalog_digest() noexcept;

}  // namespace ashen::core
