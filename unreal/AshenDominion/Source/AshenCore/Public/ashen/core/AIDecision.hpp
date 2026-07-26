#pragma once

#include "ashen/core/AIDifficulty.hpp"
#include "ashen/core/AIDoctrine.hpp"
#include "ashen/core/AIInfluenceMap.hpp"
#include "ashen/core/Types.hpp"

#include <compare>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ashen::core {

enum class AIDecisionLayer : std::uint8_t { Strategic, Tactical, Micro };

enum class AIAction : std::uint8_t {
  AssignGatherers,
  ResumeBarracks,
  BuildBarracks,
  ResumeTurret,
  BuildTurret,
  TrainWorker,
  TrainVanguard,
  TrainSkirmisher,
  ResearchTierTwo,
  ResearchDoctrine,
  Scout,
  CaptureObjective,
  ReinforceFront,
  EngageForce,
  AssaultCommand,
  SearchEnemyCommand,
  ActivatePower,
  Retreat,
  Kite,
  FocusFire,
  ScreenRanged,
  RejoinFormation,
};

enum class AIUtilityReason : std::uint8_t {
  Baseline,
  IdleWorkers,
  RequiredOpening,
  OrphanedConstruction,
  SupplyPressure,
  EconomyTarget,
  ProductionCapacity,
  TechnologyTiming,
  FactionDoctrine,
  CounterArmored,
  PressureStructures,
  CompositionBalance,
  InformationNeed,
  ObjectiveAvailable,
  ReinforcementReady,
  FavorableEngagement,
  EnemyCommandExposed,
  LastKnownCommand,
  AbilityOpportunity,
  Outnumbered,
  CriticalHealth,
  LowResolve,
  ResolvePreservation,
  DreadExploitation,
  AcceptableLosses,
  WeaponCoolingDown,
  MeleePressure,
  VulnerableTarget,
  HighThreatTarget,
  RangedLineThreatened,
  FormationSpread,
  FormationDoctrine,
  WardSupport,
  ScoutingDoctrine,
  FlankSafety,
  DangerAvoidance,
  FriendlySupport,
  TravelEfficiency,
  TerrorAvoidance,
  UncertaintyReduction,
  CombatRecovery,
};

enum class AICommandStatus : std::uint8_t { Queued, Accepted, Rejected };

inline constexpr Tick kStrategicDecisionCadence = 80;
inline constexpr Tick kTacticalDecisionCadence = 120;
inline constexpr Tick kTacticalDecisionPhase = 30;
inline constexpr Tick kMicroDecisionCadence = 12;
inline constexpr Tick kAttritionCommitmentTick = 4'800;
inline constexpr Tick kLateSearchCommitmentTick = 7'200;

[[nodiscard]] constexpr Tick ai_decision_cadence(const AIDecisionLayer layer) noexcept {
  switch (layer) {
    case AIDecisionLayer::Strategic:
      return kStrategicDecisionCadence;
    case AIDecisionLayer::Tactical:
      return kTacticalDecisionCadence;
    case AIDecisionLayer::Micro:
      return kMicroDecisionCadence;
  }
  return 1;
}

[[nodiscard]] constexpr Tick ai_decision_cadence(
    const AIDecisionLayer layer,
    const AIDifficultyProfile& difficulty) noexcept {
  switch (layer) {
    case AIDecisionLayer::Strategic:
      return difficulty.strategic_cadence_ticks;
    case AIDecisionLayer::Tactical:
      return difficulty.tactical_cadence_ticks;
    case AIDecisionLayer::Micro:
      return difficulty.micro_cadence_ticks;
  }
  return 1;
}

[[nodiscard]] constexpr bool ai_decision_due(const AIDecisionLayer layer, const Tick tick) noexcept {
  switch (layer) {
    case AIDecisionLayer::Strategic:
      return tick == 1 || (tick > 0 && tick % kStrategicDecisionCadence == 0);
    case AIDecisionLayer::Tactical:
      return tick >= kTacticalDecisionPhase &&
             (tick - kTacticalDecisionPhase) % kTacticalDecisionCadence == 0;
    case AIDecisionLayer::Micro:
      return tick >= kMicroDecisionCadence && tick % kMicroDecisionCadence == 0;
  }
  return false;
}

[[nodiscard]] constexpr bool ai_decision_due(
    const AIDecisionLayer layer, const Tick tick,
    const AIDifficultyProfile& difficulty) noexcept {
  const auto cadence = ai_decision_cadence(layer, difficulty);
  if (cadence == 0) {
    return false;
  }
  switch (layer) {
    case AIDecisionLayer::Strategic:
      return tick == 1 || (tick > 0 && tick % cadence == 0);
    case AIDecisionLayer::Tactical:
      return tick >= difficulty.tactical_phase_ticks &&
             (tick - difficulty.tactical_phase_ticks) % cadence == 0;
    case AIDecisionLayer::Micro:
      return tick >= cadence && tick % cadence == 0;
  }
  return false;
}

struct AIUtilityComponent {
  AIUtilityReason reason{AIUtilityReason::Baseline};
  std::int32_t score{};

  auto operator<=>(const AIUtilityComponent&) const = default;
};

struct AICandidateScore {
  AIAction action{AIAction::AssignGatherers};
  EntityId target_entity{};
  ControlPointId target_objective{};
  Vec2 target_position{};
  std::optional<EntityType> entity_type{};
  std::optional<ResearchId> research{};
  std::uint64_t influence_map_hash{};
  std::optional<AIInfluenceSample> influence_sample{};
  std::int32_t total_score{};
  std::vector<AIUtilityComponent> components{};

  auto operator<=>(const AICandidateScore&) const = default;
};

struct AIPlannedDecision {
  AIDecisionLayer layer{AIDecisionLayer::Strategic};
  Tick cadence_ticks{kStrategicDecisionCadence};
  AIDifficulty difficulty{AIDifficulty::Competitive};
  std::uint64_t difficulty_hash{};
  Tick knowledge_tick{};
  FactionId doctrine_faction{FactionId::Compact};
  AITemperament temperament{AITemperament::Steady};
  std::uint64_t doctrine_hash{};
  std::vector<AICandidateScore> candidates{};
  std::size_t selected_candidate{};
  std::size_t evaluated_candidates{};
  std::int32_t selected_quality_basis_points{10'000};
  bool mistake_applied{};
  AIAction selected_action{AIAction::AssignGatherers};
  AIUtilityReason winning_reason{AIUtilityReason::Baseline};
  Vec2 command_precision_offset{};
  Tick command_latency_ticks{};
  Command command{};

  auto operator<=>(const AIPlannedDecision&) const = default;
};

struct CommanderPlan {
  std::vector<AIPlannedDecision> decisions{};

  auto operator<=>(const CommanderPlan&) const = default;
};

struct AIDecisionRecord {
  std::uint64_t id{};
  Tick observation_tick{};
  std::uint64_t observation_hash{};
  PlayerId player{PlayerId::One};
  AIDecisionLayer layer{AIDecisionLayer::Strategic};
  Tick cadence_ticks{kStrategicDecisionCadence};
  AIDifficulty difficulty{AIDifficulty::Competitive};
  std::uint64_t difficulty_hash{};
  Tick knowledge_tick{};
  FactionId doctrine_faction{FactionId::Compact};
  AITemperament temperament{AITemperament::Steady};
  std::uint64_t doctrine_hash{};
  std::vector<AICandidateScore> candidates{};
  std::size_t selected_candidate{};
  std::size_t evaluated_candidates{};
  std::int32_t selected_quality_basis_points{10'000};
  bool mistake_applied{};
  AIAction selected_action{AIAction::AssignGatherers};
  AIUtilityReason winning_reason{AIUtilityReason::Baseline};
  Vec2 command_precision_offset{};
  Tick command_latency_ticks{};
  Command command{};
  std::uint64_t command_sequence{};
  Tick applied_tick{};
  AICommandStatus command_status{AICommandStatus::Queued};
  CommandError command_error{CommandError::None};

  auto operator<=>(const AIDecisionRecord&) const = default;
};

[[nodiscard]] ASHENCORE_API std::string_view to_string(AIDecisionLayer layer) noexcept;
[[nodiscard]] ASHENCORE_API std::string_view to_string(AIAction action) noexcept;
[[nodiscard]] ASHENCORE_API std::string_view to_string(AIUtilityReason reason) noexcept;
[[nodiscard]] ASHENCORE_API std::string_view to_string(AICommandStatus status) noexcept;

}  // namespace ashen::core
