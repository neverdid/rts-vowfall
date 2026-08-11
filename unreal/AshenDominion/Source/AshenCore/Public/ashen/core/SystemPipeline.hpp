#pragma once

#include "ashen/core/Types.hpp"

#include <array>
#include <cstdint>
#include <string_view>

namespace ashen::core {

// Increment whenever authoritative deterministic rules change without a
// Snapshot schema or content-catalog change. This revision prevents older
// checkpoints and replays from being interpreted under new simulation rules.
inline constexpr std::uint32_t kDeterministicRulesRevision = 2;

enum class SystemPhase : std::uint8_t {
  CommandValidation,
  CommandApplication,
  Scenario,
  Supply,
  Economy,
  Construction,
  Production,
  ResearchDoctrine,
  Navigation,
  Movement,
  FormationCohesion,
  CombatTargeting,
  ProjectileAbilityResolution,
  CasualtyProcessing,
  Resolve,
  TerritoryObjectives,
  VisibilityMemory,
  VowProcessing,
  VictoryDefeat,
  EventGeneration,
  StateHashing,
};

struct SystemPhaseDefinition {
  SystemPhase phase{SystemPhase::CommandValidation};
  std::string_view development_name{};

  auto operator<=>(const SystemPhaseDefinition&) const = default;
};

inline constexpr std::array<SystemPhaseDefinition, 21> kTargetSystemPipeline{{
    {SystemPhase::CommandValidation, "command_validation"},
    {SystemPhase::CommandApplication, "command_application"},
    {SystemPhase::Scenario, "scenario"},
    {SystemPhase::Supply, "supply"},
    {SystemPhase::Economy, "economy"},
    {SystemPhase::Construction, "construction"},
    {SystemPhase::Production, "production"},
    {SystemPhase::ResearchDoctrine, "research_doctrine"},
    {SystemPhase::Navigation, "navigation"},
    {SystemPhase::Movement, "movement"},
    {SystemPhase::FormationCohesion, "formation_cohesion"},
    {SystemPhase::CombatTargeting, "combat_targeting"},
    {SystemPhase::ProjectileAbilityResolution,
     "projectile_ability_resolution"},
    {SystemPhase::CasualtyProcessing, "casualty_processing"},
    {SystemPhase::Resolve, "resolve"},
    {SystemPhase::TerritoryObjectives, "territory_objectives"},
    {SystemPhase::VisibilityMemory, "visibility_memory"},
    {SystemPhase::VowProcessing, "vow_processing"},
    {SystemPhase::VictoryDefeat, "victory_defeat"},
    {SystemPhase::EventGeneration, "event_generation"},
    {SystemPhase::StateHashing, "state_hashing"},
}};

}  // namespace ashen::core
