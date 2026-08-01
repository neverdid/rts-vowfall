#pragma once

#include "ashen/core/SimulationEvent.hpp"
#include "ashen/core/Types.hpp"

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace ashen::core {

namespace content_id {
inline constexpr StableContentId CompactFaction = 1'001;
inline constexpr StableContentId AscendancyFaction = 1'002;
inline constexpr StableContentId ConcordFaction = 1'003;
inline constexpr StableContentId CompactPowerAbility = 4'001;
inline constexpr StableContentId AscendancyPowerAbility = 4'002;
inline constexpr StableContentId ConcordPowerAbility = 4'003;
inline constexpr StableContentId CinderBoltProjectile = 5'001;
inline constexpr StableContentId CompactShieldLine = 6'001;
inline constexpr StableContentId AscendancyHuntingPack = 6'002;
inline constexpr StableContentId ConcordWardWeb = 6'003;
inline constexpr StableContentId PreparedAbsolution = 8'001;
inline constexpr StableContentId BridgeOpenVow = 9'001;
inline constexpr StableContentId CompactDoctrine = 10'001;
inline constexpr StableContentId AscendancyDoctrine = 10'002;
inline constexpr StableContentId ConcordDoctrine = 10'003;
inline constexpr StableContentId CompactOpening = 11'001;
inline constexpr StableContentId AscendancyOpening = 11'002;
inline constexpr StableContentId ConcordOpening = 11'003;
inline constexpr StableContentId NorthRelicObjective = 12'001;
inline constexpr StableContentId SouthRelicObjective = 12'002;
inline constexpr StableContentId BridgeObjective = 12'003;
inline constexpr StableContentId SkirmishVictoryObjective = 12'004;
inline constexpr StableContentId BridgeApproachesObjective = 12'005;
}  // namespace content_id

inline constexpr VowId kBridgeOpenVow{content_id::BridgeOpenVow};

using CommandCapabilityMask = std::uint64_t;

[[nodiscard]] constexpr CommandCapabilityMask command_capability(
    const CommandType command) noexcept {
  return CommandCapabilityMask{1}
         << static_cast<std::uint8_t>(command);
}

struct DefinitionMetadata {
  StableContentId stable_id{};
  std::uint32_t version{1};
  std::string_view development_name{};
  std::string_view localization_key{};
  std::string_view presentation_key{};

  auto operator<=>(const DefinitionMetadata&) const = default;
};

struct FactionContentDefinition {
  DefinitionMetadata metadata{};
  FactionId faction{FactionId::Compact};
};

struct UnitContentDefinition {
  DefinitionMetadata metadata{};
  FactionId faction{FactionId::Compact};
  EntityType archetype{EntityType::Worker};
  std::int32_t cost{};
  std::int64_t build_ticks{};
  CommandCapabilityMask capabilities{};
};

struct StructureContentDefinition {
  DefinitionMetadata metadata{};
  FactionId faction{FactionId::Compact};
  EntityType archetype{EntityType::Command};
  std::int32_t cost{};
  std::int64_t build_ticks{};
  CommandCapabilityMask capabilities{};
};

struct AbilityContentDefinition {
  DefinitionMetadata metadata{};
  std::optional<FactionId> faction{};
  std::int64_t windup_ticks{};
  std::int64_t recovery_ticks{};
  std::optional<StableContentId> projectile{};
  CommandCapabilityMask required_capability{};
};

struct ProjectileContentDefinition {
  DefinitionMetadata metadata{};
  std::int32_t speed_per_tick{};
  std::int32_t damage{};
  std::int32_t radius{};
};

struct FormationContentDefinition {
  DefinitionMetadata metadata{};
  std::optional<FactionId> faction{};
  std::int32_t minimum_members{};
  std::int32_t maximum_members{};
};

struct ResearchContentDefinition {
  DefinitionMetadata metadata{};
  ResearchId research{ResearchId::TierTwo};
  std::optional<FactionId> faction{};
  std::int32_t cost{};
  std::int64_t duration_ticks{};
  std::optional<ResearchId> prerequisite{};
};

struct PowerContentDefinition {
  DefinitionMetadata metadata{};
  FactionId faction{FactionId::Compact};
  StableContentId ability{};
  std::int32_t cost{};
  std::int64_t cooldown_ticks{};
};

struct TransformationContentDefinition {
  DefinitionMetadata metadata{};
  FactionId faction{FactionId::Ascendancy};
  EntityType source{EntityType::Worker};
  EntityType result{EntityType::Vanguard};
  std::int32_t permanent_cost{};
  std::int64_t duration_ticks{};
  bool reversible{};
};

struct VowContentDefinition {
  DefinitionMetadata metadata{};
  VowId vow{};
  bool amendment_requires_affected_party{};
  std::string_view consequence_key{};
};

struct AIDoctrineContentDefinition {
  DefinitionMetadata metadata{};
  FactionId faction{FactionId::Compact};
};

struct AIStrategyContentDefinition {
  DefinitionMetadata metadata{};
  FactionId faction{FactionId::Compact};
  std::int32_t desired_workers{};
  std::int32_t desired_vanguards{};
  std::int32_t desired_skirmishers{};
};

struct ObjectiveContentDefinition {
  DefinitionMetadata metadata{};
  std::optional<VowId> related_vow{};
  std::int32_t capture_radius{};
};

struct ContentRegistry {
  std::vector<FactionContentDefinition> factions{};
  std::vector<UnitContentDefinition> units{};
  std::vector<StructureContentDefinition> structures{};
  std::vector<AbilityContentDefinition> abilities{};
  std::vector<ProjectileContentDefinition> projectiles{};
  std::vector<FormationContentDefinition> formations{};
  std::vector<ResearchContentDefinition> research{};
  std::vector<PowerContentDefinition> powers{};
  std::vector<TransformationContentDefinition> transformations{};
  std::vector<VowContentDefinition> vows{};
  std::vector<AIDoctrineContentDefinition> ai_doctrines{};
  std::vector<AIStrategyContentDefinition> ai_strategies{};
  std::vector<ObjectiveContentDefinition> objectives{};
};

enum class ContentValidationError : std::uint8_t {
  DuplicateStableId,
  InvalidVersion,
  MissingDevelopmentName,
  MissingLocalizationKey,
  InvalidPresentationKey,
  MissingFactionReference,
  MissingContentReference,
  InvalidPrerequisite,
  NegativeCost,
  InvalidDuration,
  InvalidDeterministicValue,
  UnsupportedCommandCapability,
  CyclicResearchPrerequisite,
};

struct ContentValidationIssue {
  ContentValidationError error{ContentValidationError::DuplicateStableId};
  StableContentId definition{};
  StableContentId reference{};
};

[[nodiscard]] ASHENCORE_API const ContentRegistry& builtin_content() noexcept;
[[nodiscard]] ASHENCORE_API std::vector<ContentValidationIssue> validate_content(
    const ContentRegistry& registry);
[[nodiscard]] ASHENCORE_API const VowContentDefinition* find_vow_content(
    const ContentRegistry& registry, VowId vow) noexcept;
[[nodiscard]] ASHENCORE_API const AbilityContentDefinition*
find_faction_power_ability(const ContentRegistry& registry,
                           FactionId faction) noexcept;
[[nodiscard]] ASHENCORE_API std::string_view faction_presentation_key(
    FactionId faction) noexcept;

}  // namespace ashen::core
