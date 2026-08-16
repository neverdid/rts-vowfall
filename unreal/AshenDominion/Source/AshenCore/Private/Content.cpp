#include "ashen/core/Content.hpp"

#include "ashen/core/Catalog.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <functional>
#include <ranges>
#include <utility>
#include <vector>

namespace ashen::core {
namespace {

[[nodiscard]] DefinitionMetadata metadata(
    const StableContentId id, const std::string_view name,
    const std::string_view localization,
    const std::string_view presentation) noexcept {
  return {id, 1, name, localization, presentation};
}

[[nodiscard]] UnitContentDefinition unit(
    const StableContentId id, const FactionId faction,
    const EntityType archetype, const std::string_view name,
    const std::string_view localization, const std::string_view presentation,
    const CommandCapabilityMask capabilities) {
  const auto gameplay = entity_definition(faction, archetype);
  return {metadata(id, name, localization, presentation),
          faction,
          archetype,
          gameplay.cost,
          static_cast<std::int64_t>(gameplay.build_ticks),
          capabilities};
}

[[nodiscard]] StructureContentDefinition structure(
    const StableContentId id, const FactionId faction,
    const EntityType archetype, const std::string_view name,
    const std::string_view localization, const std::string_view presentation,
    const CommandCapabilityMask capabilities) {
  const auto gameplay = entity_definition(faction, archetype);
  return {metadata(id, name, localization, presentation),
          faction,
          archetype,
          gameplay.cost,
          static_cast<std::int64_t>(gameplay.build_ticks),
          capabilities};
}

[[nodiscard]] bool valid_presentation_key(const std::string_view key) noexcept {
  constexpr std::string_view prefix = "vowfall.";
  if (!key.starts_with(prefix) || key.size() == prefix.size()) {
    return false;
  }
  return std::ranges::all_of(key, [](const char character) {
    return (character >= 'a' && character <= 'z') ||
           (character >= '0' && character <= '9') || character == '.' ||
           character == '_';
  });
}

template <typename Definition>
void validate_metadata(const Definition& definition,
                       std::vector<ContentValidationIssue>& issues) {
  const auto& value = definition.metadata;
  if (value.version == 0) {
    issues.push_back(
        {ContentValidationError::InvalidVersion, value.stable_id, 0});
  }
  if (value.development_name.empty()) {
    issues.push_back(
        {ContentValidationError::MissingDevelopmentName, value.stable_id, 0});
  }
  if (value.localization_key.empty()) {
    issues.push_back(
        {ContentValidationError::MissingLocalizationKey, value.stable_id, 0});
  }
  if (!valid_presentation_key(value.presentation_key)) {
    issues.push_back(
        {ContentValidationError::InvalidPresentationKey, value.stable_id, 0});
  }
}

[[nodiscard]] bool has_faction(const ContentRegistry& registry,
                               const FactionId faction) noexcept {
  return std::ranges::any_of(
      registry.factions,
      [faction](const FactionContentDefinition& definition) {
        return definition.faction == faction;
      });
}

[[nodiscard]] bool has_projectile(const ContentRegistry& registry,
                                  const StableContentId id) noexcept {
  return std::ranges::any_of(
      registry.projectiles,
      [id](const ProjectileContentDefinition& definition) {
        return definition.metadata.stable_id == id;
      });
}

[[nodiscard]] bool has_ability(const ContentRegistry& registry,
                               const StableContentId id) noexcept {
  return std::ranges::any_of(
      registry.abilities,
      [id](const AbilityContentDefinition& definition) {
        return definition.metadata.stable_id == id;
      });
}

[[nodiscard]] bool has_research(const ContentRegistry& registry,
                                const ResearchId research) noexcept {
  return std::ranges::any_of(
      registry.research,
      [research](const ResearchContentDefinition& definition) {
        return definition.research == research;
      });
}

[[nodiscard]] bool has_vow(const ContentRegistry& registry,
                           const VowId vow) noexcept {
  return find_vow_content(registry, vow) != nullptr;
}

[[nodiscard]] bool has_structure(const ContentRegistry& registry,
                                 const StableContentId id) noexcept {
  return std::ranges::any_of(
      registry.structures,
      [id](const StructureContentDefinition& definition) {
        return definition.metadata.stable_id == id;
      });
}

}  // namespace

const ContentRegistry& builtin_content() noexcept {
  static const ContentRegistry registry = [] {
    ContentRegistry result{};
    result.factions = {
        {metadata(content_id::CompactFaction, "cinder_compact",
                  "faction.cinder_compact", "vowfall.faction.compact"),
         FactionId::Compact},
        {metadata(content_id::AscendancyFaction, "gloam_ascendancy",
                  "faction.gloam_ascendancy",
                  "vowfall.faction.ascendancy"),
         FactionId::Ascendancy},
        {metadata(content_id::ConcordFaction, "elder_concord",
                  "faction.elder_concord", "vowfall.faction.concord"),
         FactionId::Concord},
    };

    const auto common_unit_capabilities =
        command_capability(CommandType::Move) |
        command_capability(CommandType::Attack) |
        command_capability(CommandType::AttackMove) |
        command_capability(CommandType::Stop) |
        command_capability(CommandType::Hold) |
        command_capability(CommandType::Patrol) |
        command_capability(CommandType::Retreat) |
        command_capability(CommandType::SetStance);
    const auto worker_capabilities =
        common_unit_capabilities | command_capability(CommandType::Gather) |
        command_capability(CommandType::Build);
    result.units = {
        unit(2'001, FactionId::Compact, EntityType::Worker, "compact_worker",
             "unit.compact.worker", "vowfall.unit.compact.worker",
             worker_capabilities),
        unit(2'002, FactionId::Compact, EntityType::Vanguard,
             "compact_vanguard", "unit.compact.vanguard",
             "vowfall.unit.compact.vanguard", common_unit_capabilities),
        unit(2'003, FactionId::Compact, EntityType::Skirmisher,
             "compact_skirmisher", "unit.compact.skirmisher",
             "vowfall.unit.compact.skirmisher", common_unit_capabilities),
        unit(2'101, FactionId::Ascendancy, EntityType::Worker,
             "ascendancy_worker", "unit.ascendancy.worker",
             "vowfall.unit.ascendancy.worker", worker_capabilities),
        unit(2'102, FactionId::Ascendancy, EntityType::Vanguard,
             "ascendancy_vanguard", "unit.ascendancy.vanguard",
             "vowfall.unit.ascendancy.vanguard", common_unit_capabilities),
        unit(2'103, FactionId::Ascendancy, EntityType::Skirmisher,
             "ascendancy_skirmisher", "unit.ascendancy.skirmisher",
             "vowfall.unit.ascendancy.skirmisher", common_unit_capabilities),
        unit(2'201, FactionId::Concord, EntityType::Worker, "concord_worker",
             "unit.concord.worker", "vowfall.unit.concord.worker",
             worker_capabilities),
        unit(2'202, FactionId::Concord, EntityType::Vanguard,
             "concord_vanguard", "unit.concord.vanguard",
             "vowfall.unit.concord.vanguard", common_unit_capabilities),
        unit(2'203, FactionId::Concord, EntityType::Skirmisher,
             "concord_skirmisher", "unit.concord.skirmisher",
             "vowfall.unit.concord.skirmisher", common_unit_capabilities),
    };

    const auto command_capabilities =
        command_capability(CommandType::Train) |
        command_capability(CommandType::Research) |
        command_capability(CommandType::SetRallyPoint) |
        command_capability(CommandType::ActivatePower);
    const auto producer_capabilities =
        command_capability(CommandType::Train) |
        command_capability(CommandType::Research) |
        command_capability(CommandType::SetRallyPoint);
    const auto turret_capabilities = command_capability(CommandType::Attack);
    result.structures = {
        structure(3'001, FactionId::Compact, EntityType::Command,
                  "compact_command", "structure.compact.command",
                  "vowfall.structure.compact.command", command_capabilities),
        structure(3'002, FactionId::Compact, EntityType::Barracks,
                  "compact_barracks", "structure.compact.barracks",
                  "vowfall.structure.compact.barracks",
                  producer_capabilities),
        structure(3'003, FactionId::Compact, EntityType::Turret,
                  "compact_turret", "structure.compact.turret",
                   "vowfall.structure.compact.turret", turret_capabilities),
        structure(3'004, FactionId::Compact, EntityType::Hospital,
                  "compact_field_hospital", "structure.compact.field_hospital",
                  "vowfall.structure.compact.field_hospital", 0),
        structure(3'101, FactionId::Ascendancy, EntityType::Command,
                  "ascendancy_command", "structure.ascendancy.command",
                  "vowfall.structure.ascendancy.command",
                  command_capabilities),
        structure(3'102, FactionId::Ascendancy, EntityType::Barracks,
                  "ascendancy_barracks", "structure.ascendancy.barracks",
                  "vowfall.structure.ascendancy.barracks",
                  producer_capabilities),
        structure(3'103, FactionId::Ascendancy, EntityType::Turret,
                  "ascendancy_turret", "structure.ascendancy.turret",
                  "vowfall.structure.ascendancy.turret",
                  turret_capabilities),
        structure(3'201, FactionId::Concord, EntityType::Command,
                  "concord_command", "structure.concord.command",
                  "vowfall.structure.concord.command", command_capabilities),
        structure(3'202, FactionId::Concord, EntityType::Barracks,
                  "concord_barracks", "structure.concord.barracks",
                  "vowfall.structure.concord.barracks",
                  producer_capabilities),
        structure(3'203, FactionId::Concord, EntityType::Turret,
                  "concord_turret", "structure.concord.turret",
                  "vowfall.structure.concord.turret", turret_capabilities),
    };
    result.supply_nodes = {
        {metadata(content_id::CompactLedgerKeep, "compact_ledger_keep",
                  "supply.compact.keep", "vowfall.supply.compact.keep"),
         3'001, true, true, 420'000, 6, 0},
        {metadata(content_id::CompactLedgerRelay, "compact_ledger_relay",
                  "supply.compact.relay", "vowfall.supply.compact.relay"),
         3'002, false, true, 360'000, 0, 2},
        {metadata(content_id::CompactLedgerBastion,
                  "compact_ledger_bastion", "supply.compact.bastion",
                  "vowfall.supply.compact.bastion"),
         3'003, false, false, 0, 0, 1},
        {metadata(content_id::CompactLedgerHospital,
                  "compact_ledger_hospital", "supply.compact.hospital",
                  "vowfall.supply.compact.hospital"),
         3'004, false, false, 0, 0, 2},
    };
    result.care_facilities = {
        {metadata(content_id::CompactFieldHospitalCare,
                  "compact_field_hospital_care", "care.compact.field_hospital",
                  "vowfall.care.compact.field_hospital"),
         3'004, 300'000, 2, 4, 120},
    };

    result.projectiles = {
        {metadata(content_id::CinderBoltProjectile, "cinder_bolt",
                  "projectile.cinder_bolt", "vowfall.projectile.cinder_bolt"),
         18'000,
         9,
         2'000},
    };
    result.abilities = {
        {metadata(content_id::CompactPowerAbility, "no_one_left_uncounted",
                  "ability.no_one_left_uncounted",
                  "vowfall.ability.no_one_left_uncounted"),
         FactionId::Compact,
         0,
         20,
         std::nullopt,
         command_capability(CommandType::ActivatePower)},
        {metadata(content_id::AscendancyPowerAbility, "completion",
                  "ability.completion", "vowfall.ability.completion"),
         FactionId::Ascendancy,
         20,
         40,
         std::nullopt,
         command_capability(CommandType::ActivatePower)},
        {metadata(content_id::ConcordPowerAbility, "wake_the_ground",
                  "ability.wake_the_ground",
                  "vowfall.ability.wake_the_ground"),
         FactionId::Concord,
         10,
         30,
         std::nullopt,
         command_capability(CommandType::ActivatePower)},
    };
    result.formations = {
        {metadata(content_id::CompactShieldLine, "compact_shield_line",
                  "formation.compact.shield_line",
                  "vowfall.formation.compact.shield_line"),
         FactionId::Compact,
         2,
         24},
        {metadata(content_id::AscendancyHuntingPack,
                  "ascendancy_hunting_pack",
                  "formation.ascendancy.hunting_pack",
                  "vowfall.formation.ascendancy.hunting_pack"),
         FactionId::Ascendancy,
         2,
         16},
        {metadata(content_id::ConcordWardWeb, "concord_ward_web",
                  "formation.concord.ward_web",
                  "vowfall.formation.concord.ward_web"),
         FactionId::Concord,
         2,
         20},
    };

    constexpr std::array<ResearchId, kResearchCount> research_ids{
        ResearchId::TierTwo,        ResearchId::TemperedOaths,
        ResearchId::Wardcraft,      ResearchId::ChorusOfKnives,
        ResearchId::PitBroods,      ResearchId::VaultPlate,
        ResearchId::SiegeLiturgy,
    };
    constexpr std::array<std::string_view, kResearchCount> research_names{
        "tier_two",       "tempered_oaths", "wardcraft",
        "chorus_knives",  "pit_broods",     "vault_plate",
        "siege_liturgy",
    };
    constexpr std::array<std::string_view, kResearchCount> research_loc{
        "research.tier_two",       "research.tempered_oaths",
        "research.wardcraft",      "research.chorus_knives",
        "research.pit_broods",     "research.vault_plate",
        "research.siege_liturgy",
    };
    constexpr std::array<std::string_view, kResearchCount> research_visual{
        "vowfall.research.tier_two",      "vowfall.research.tempered_oaths",
        "vowfall.research.wardcraft",     "vowfall.research.chorus_knives",
        "vowfall.research.pit_broods",    "vowfall.research.vault_plate",
        "vowfall.research.siege_liturgy",
    };
    for (std::size_t index = 0; index < research_ids.size(); ++index) {
      const auto gameplay = research_definition(research_ids[index]);
      result.research.push_back(
          {metadata(7'001 + static_cast<StableContentId>(index),
                    research_names[index], research_loc[index],
                    research_visual[index]),
           gameplay.id,
           gameplay.faction,
           gameplay.cost,
           static_cast<std::int64_t>(gameplay.research_ticks),
           gameplay.prerequisite});
    }

    result.powers = {
        {metadata(7'101, "compact_power", "power.compact",
                  "vowfall.power.compact"),
         FactionId::Compact,
         content_id::CompactPowerAbility,
         power_definition(FactionId::Compact).cost,
         static_cast<std::int64_t>(
             power_definition(FactionId::Compact).cooldown_ticks)},
        {metadata(7'102, "ascendancy_power", "power.ascendancy",
                  "vowfall.power.ascendancy"),
         FactionId::Ascendancy,
         content_id::AscendancyPowerAbility,
         power_definition(FactionId::Ascendancy).cost,
         static_cast<std::int64_t>(
             power_definition(FactionId::Ascendancy).cooldown_ticks)},
        {metadata(7'103, "concord_power", "power.concord",
                  "vowfall.power.concord"),
         FactionId::Concord,
         content_id::ConcordPowerAbility,
         power_definition(FactionId::Concord).cost,
         static_cast<std::int64_t>(
             power_definition(FactionId::Concord).cooldown_ticks)},
    };
    result.transformations = {
        {metadata(content_id::PreparedAbsolution, "prepared_absolution",
                  "transformation.prepared_absolution",
                  "vowfall.transformation.prepared_absolution"),
         FactionId::Ascendancy,
         EntityType::Worker,
         EntityType::Vanguard,
         72,
         160,
         false},
    };
    result.vows = {
        {metadata(content_id::BridgeOpenVow, "keep_bridge_open",
                  "vow.keep_bridge_open", "vowfall.vow.keep_bridge_open"),
         kBridgeOpenVow,
         true,
         "bridge_final_order"},
    };
    result.ai_doctrines = {
        {metadata(content_id::CompactDoctrine, "compact_road_ledger",
                  "ai.doctrine.compact", "vowfall.ai.doctrine.compact"),
         FactionId::Compact},
        {metadata(content_id::AscendancyDoctrine, "ascendancy_absolution",
                  "ai.doctrine.ascendancy",
                  "vowfall.ai.doctrine.ascendancy"),
         FactionId::Ascendancy},
        {metadata(content_id::ConcordDoctrine, "concord_treaty",
                  "ai.doctrine.concord", "vowfall.ai.doctrine.concord"),
         FactionId::Concord},
    };
    result.ai_strategies = {
        {metadata(content_id::CompactOpening, "compact_ledger_opening",
                  "ai.strategy.compact", "vowfall.ai.strategy.compact"),
         FactionId::Compact,
         6,
         4,
         3},
        {metadata(content_id::AscendancyOpening, "ascendancy_pressure_opening",
                  "ai.strategy.ascendancy",
                  "vowfall.ai.strategy.ascendancy"),
         FactionId::Ascendancy,
         4,
         5,
         2},
        {metadata(content_id::ConcordOpening, "concord_objective_opening",
                  "ai.strategy.concord", "vowfall.ai.strategy.concord"),
         FactionId::Concord,
         5,
         3,
         4},
    };
    result.objectives = {
        {metadata(content_id::NorthRelicObjective, "north_relic",
                  "objective.north_relic", "vowfall.objective.north_relic"),
         std::nullopt,
         92'000},
        {metadata(content_id::SouthRelicObjective, "south_relic",
                  "objective.south_relic", "vowfall.objective.south_relic"),
         std::nullopt,
         92'000},
        {metadata(content_id::BridgeObjective, "bridge_open",
                  "objective.bridge_open", "vowfall.objective.bridge_open"),
         kBridgeOpenVow,
         120'000},
        {metadata(content_id::SkirmishVictoryObjective, "skirmish_victory",
                  "objective.skirmish_victory",
                  "vowfall.objective.skirmish_victory"),
         std::nullopt,
         0},
        {metadata(content_id::BridgeApproachesObjective, "bridge_approaches",
                  "objective.bridge_approaches",
                  "vowfall.objective.bridge_approaches"),
         kBridgeOpenVow,
         0},
    };
    return result;
  }();
  return registry;
}

std::vector<ContentValidationIssue> validate_content(
    const ContentRegistry& registry) {
  std::vector<ContentValidationIssue> issues;
  std::vector<StableContentId> stable_ids;
  const auto collect = [&](const auto& definitions) {
    for (const auto& definition : definitions) {
      validate_metadata(definition, issues);
      stable_ids.push_back(definition.metadata.stable_id);
    }
  };
  collect(registry.factions);
  collect(registry.units);
  collect(registry.structures);
  collect(registry.supply_nodes);
  collect(registry.care_facilities);
  collect(registry.abilities);
  collect(registry.projectiles);
  collect(registry.formations);
  collect(registry.research);
  collect(registry.powers);
  collect(registry.transformations);
  collect(registry.vows);
  collect(registry.ai_doctrines);
  collect(registry.ai_strategies);
  collect(registry.objectives);
  std::ranges::sort(stable_ids);
  for (std::size_t index = 1; index < stable_ids.size(); ++index) {
    if (stable_ids[index] == stable_ids[index - 1]) {
      issues.push_back({ContentValidationError::DuplicateStableId,
                        stable_ids[index], stable_ids[index]});
    }
  }

  constexpr auto unit_capabilities =
      command_capability(CommandType::Move) |
      command_capability(CommandType::Attack) |
      command_capability(CommandType::AttackMove) |
      command_capability(CommandType::Gather) |
      command_capability(CommandType::Stop) |
      command_capability(CommandType::Hold) |
      command_capability(CommandType::Patrol) |
      command_capability(CommandType::Build) |
      command_capability(CommandType::Retreat) |
      command_capability(CommandType::SetStance);
  constexpr auto structure_capabilities =
      command_capability(CommandType::Attack) |
      command_capability(CommandType::Train) |
      command_capability(CommandType::SetRallyPoint) |
      command_capability(CommandType::Research) |
      command_capability(CommandType::ActivatePower);

  for (const auto& definition : registry.units) {
    if (!has_faction(registry, definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(definition.faction)});
    }
    if (definition.cost < 0) {
      issues.push_back({ContentValidationError::NegativeCost,
                        definition.metadata.stable_id, 0});
    }
    if (definition.build_ticks < 0) {
      issues.push_back({ContentValidationError::InvalidDuration,
                        definition.metadata.stable_id, 0});
    }
    if ((definition.capabilities & ~unit_capabilities) != 0 ||
        (definition.archetype != EntityType::Worker &&
         (definition.capabilities &
          (command_capability(CommandType::Gather) |
           command_capability(CommandType::Build))) != 0)) {
      issues.push_back({ContentValidationError::UnsupportedCommandCapability,
                        definition.metadata.stable_id, 0});
    }
  }
  for (const auto& definition : registry.structures) {
    if (!has_faction(registry, definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(definition.faction)});
    }
    if (definition.cost < 0) {
      issues.push_back({ContentValidationError::NegativeCost,
                        definition.metadata.stable_id, 0});
    }
    if (definition.build_ticks < 0) {
      issues.push_back({ContentValidationError::InvalidDuration,
                        definition.metadata.stable_id, 0});
    }
    if ((definition.capabilities & ~structure_capabilities) != 0) {
      issues.push_back({ContentValidationError::UnsupportedCommandCapability,
                        definition.metadata.stable_id, 0});
    }
  }
  std::vector<StableContentId> supplied_structures;
  for (const auto& definition : registry.supply_nodes) {
    supplied_structures.push_back(definition.structure);
    if (!has_structure(registry, definition.structure)) {
      issues.push_back({ContentValidationError::MissingContentReference,
                        definition.metadata.stable_id,
                        definition.structure});
    }
    const auto invalid_values =
        definition.link_range < 0 || definition.capacity < 0 ||
        definition.demand < 0;
    const auto invalid_source =
        definition.source &&
        (!definition.relay || definition.capacity <= 0 ||
         definition.demand != 0);
    const auto invalid_non_source =
        !definition.source && definition.capacity != 0;
    const auto invalid_relay =
        definition.relay ? definition.link_range <= 0
                         : definition.link_range != 0;
    const auto invalid_consumer =
        !definition.source && definition.demand <= 0;
    if (invalid_values || invalid_source || invalid_non_source ||
        invalid_relay || invalid_consumer) {
      issues.push_back({ContentValidationError::InvalidSupplyNode,
                        definition.metadata.stable_id,
                        definition.structure});
    }
  }
  std::ranges::sort(supplied_structures);
  for (std::size_t index = 1; index < supplied_structures.size(); ++index) {
    if (supplied_structures[index] == supplied_structures[index - 1]) {
      issues.push_back({ContentValidationError::DuplicateSupplyNodeStructure,
                        supplied_structures[index],
                        supplied_structures[index]});
    }
  }
  std::vector<StableContentId> care_structures;
  for (const auto& definition : registry.care_facilities) {
    care_structures.push_back(definition.structure);
    if (!has_structure(registry, definition.structure)) {
      issues.push_back({ContentValidationError::MissingContentReference,
                        definition.metadata.stable_id,
                        definition.structure});
    }
    const auto has_supply_profile = std::ranges::any_of(
        registry.supply_nodes, [&definition](const auto& supply) {
          return supply.structure == definition.structure;
        });
    if (!has_supply_profile || definition.intake_range <= 0 ||
        definition.treatment_slots <= 0 ||
        definition.waiting_capacity < 0 || definition.treatment_ticks <= 0) {
      issues.push_back({ContentValidationError::InvalidCareFacility,
                        definition.metadata.stable_id,
                        definition.structure});
    }
  }
  std::ranges::sort(care_structures);
  for (std::size_t index = 1; index < care_structures.size(); ++index) {
    if (care_structures[index] == care_structures[index - 1]) {
      issues.push_back({ContentValidationError::DuplicateCareFacilityStructure,
                        care_structures[index], care_structures[index]});
    }
  }
  for (const auto& definition : registry.abilities) {
    if (definition.faction.has_value() &&
        !has_faction(registry, *definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(*definition.faction)});
    }
    if (definition.windup_ticks < 0 || definition.recovery_ticks < 0) {
      issues.push_back({ContentValidationError::InvalidDuration,
                        definition.metadata.stable_id, 0});
    }
    if (definition.projectile.has_value() &&
        !has_projectile(registry, *definition.projectile)) {
      issues.push_back({ContentValidationError::MissingContentReference,
                        definition.metadata.stable_id,
                        *definition.projectile});
    }
    if ((definition.required_capability &
         ~command_capability(CommandType::ActivatePower)) != 0) {
      issues.push_back({ContentValidationError::UnsupportedCommandCapability,
                        definition.metadata.stable_id, 0});
    }
  }
  for (const auto& definition : registry.projectiles) {
    if (definition.speed_per_tick <= 0 || definition.damage < 0 ||
        definition.radius < 0) {
      issues.push_back({ContentValidationError::InvalidDeterministicValue,
                        definition.metadata.stable_id, 0});
    }
  }
  for (const auto& definition : registry.formations) {
    if (definition.faction.has_value() &&
        !has_faction(registry, *definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(*definition.faction)});
    }
    if (definition.minimum_members <= 0 ||
        definition.maximum_members < definition.minimum_members) {
      issues.push_back({ContentValidationError::InvalidDeterministicValue,
                        definition.metadata.stable_id, 0});
    }
  }
  for (const auto& definition : registry.research) {
    if (definition.faction.has_value() &&
        !has_faction(registry, *definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(*definition.faction)});
    }
    if (definition.cost < 0) {
      issues.push_back({ContentValidationError::NegativeCost,
                        definition.metadata.stable_id, 0});
    }
    if (definition.duration_ticks < 0) {
      issues.push_back({ContentValidationError::InvalidDuration,
                        definition.metadata.stable_id, 0});
    }
    if (definition.prerequisite.has_value() &&
        !has_research(registry, *definition.prerequisite)) {
      issues.push_back({ContentValidationError::InvalidPrerequisite,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(
                            *definition.prerequisite)});
    }
  }
  for (const auto& definition : registry.powers) {
    if (!has_faction(registry, definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(definition.faction)});
    }
    if (!has_ability(registry, definition.ability)) {
      issues.push_back({ContentValidationError::MissingContentReference,
                        definition.metadata.stable_id, definition.ability});
    }
    if (definition.cost < 0) {
      issues.push_back({ContentValidationError::NegativeCost,
                        definition.metadata.stable_id, 0});
    }
    if (definition.cooldown_ticks < 0) {
      issues.push_back({ContentValidationError::InvalidDuration,
                        definition.metadata.stable_id, 0});
    }
  }
  for (const auto& definition : registry.transformations) {
    if (!has_faction(registry, definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(definition.faction)});
    }
    const auto has_unit = [&](const EntityType archetype) {
      return std::ranges::any_of(
          registry.units, [&](const UnitContentDefinition& unit_definition) {
            return unit_definition.faction == definition.faction &&
                   unit_definition.archetype == archetype;
          });
    };
    if (!has_unit(definition.source) || !has_unit(definition.result)) {
      issues.push_back({ContentValidationError::MissingContentReference,
                        definition.metadata.stable_id, 0});
    }
    if (definition.permanent_cost < 0) {
      issues.push_back({ContentValidationError::NegativeCost,
                        definition.metadata.stable_id, 0});
    }
    if (definition.duration_ticks < 0) {
      issues.push_back({ContentValidationError::InvalidDuration,
                        definition.metadata.stable_id, 0});
    }
  }
  for (const auto& definition : registry.vows) {
    if (definition.vow.value != definition.metadata.stable_id ||
        definition.consequence_key.empty()) {
      issues.push_back({ContentValidationError::InvalidDeterministicValue,
                        definition.metadata.stable_id,
                        definition.vow.value});
    }
  }
  for (const auto& definition : registry.ai_doctrines) {
    if (!has_faction(registry, definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(definition.faction)});
    }
  }
  for (const auto& definition : registry.ai_strategies) {
    if (!has_faction(registry, definition.faction)) {
      issues.push_back({ContentValidationError::MissingFactionReference,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(definition.faction)});
    }
    if (definition.desired_workers < 0 ||
        definition.desired_vanguards < 0 ||
        definition.desired_skirmishers < 0) {
      issues.push_back({ContentValidationError::InvalidDeterministicValue,
                        definition.metadata.stable_id, 0});
    }
  }
  for (const auto& definition : registry.objectives) {
    if (definition.related_vow.has_value() &&
        !has_vow(registry, *definition.related_vow)) {
      issues.push_back({ContentValidationError::MissingContentReference,
                        definition.metadata.stable_id,
                        definition.related_vow->value});
    }
    if (definition.capture_radius < 0) {
      issues.push_back({ContentValidationError::InvalidDeterministicValue,
                        definition.metadata.stable_id, 0});
    }
  }

  std::array<std::uint8_t, kResearchCount> visit{};
  std::function<bool(ResearchId)> cyclic = [&](const ResearchId research) {
    const auto index = research_index(research);
    if (index >= visit.size()) {
      return false;
    }
    if (visit[index] == 1) {
      return true;
    }
    if (visit[index] == 2) {
      return false;
    }
    visit[index] = 1;
    const auto found =
        std::ranges::find(registry.research, research,
                          &ResearchContentDefinition::research);
    if (found != registry.research.end() &&
        found->prerequisite.has_value() && cyclic(*found->prerequisite)) {
      return true;
    }
    visit[index] = 2;
    return false;
  };
  for (const auto& definition : registry.research) {
    visit.fill(0);
    if (cyclic(definition.research)) {
      issues.push_back({ContentValidationError::CyclicResearchPrerequisite,
                        definition.metadata.stable_id,
                        static_cast<StableContentId>(definition.research)});
      break;
    }
  }

  return issues;
}

const VowContentDefinition* find_vow_content(
    const ContentRegistry& registry, const VowId vow) noexcept {
  const auto found =
      std::ranges::find(registry.vows, vow, &VowContentDefinition::vow);
  return found == registry.vows.end() ? nullptr : &*found;
}

const AbilityContentDefinition* find_faction_power_ability(
    const ContentRegistry& registry, const FactionId faction) noexcept {
  const auto found =
      std::ranges::find(registry.abilities, std::optional{faction},
                        &AbilityContentDefinition::faction);
  return found == registry.abilities.end() ? nullptr : &*found;
}

const StructureContentDefinition* find_structure_content(
    const ContentRegistry& registry, const FactionId faction,
    const EntityType archetype) noexcept {
  const auto found = std::ranges::find_if(
      registry.structures, [&](const StructureContentDefinition& definition) {
        return definition.faction == faction &&
               definition.archetype == archetype;
      });
  return found == registry.structures.end() ? nullptr : &*found;
}

const SupplyNodeContentDefinition* find_supply_node_content(
    const ContentRegistry& registry, const FactionId faction,
    const EntityType archetype) noexcept {
  const auto* structure =
      find_structure_content(registry, faction, archetype);
  if (structure == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::find(
      registry.supply_nodes, structure->metadata.stable_id,
      &SupplyNodeContentDefinition::structure);
  return found == registry.supply_nodes.end() ? nullptr : &*found;
}

const CareFacilityContentDefinition* find_care_facility_content(
    const ContentRegistry& registry, const FactionId faction,
    const EntityType archetype) noexcept {
  const auto* structure = find_structure_content(registry, faction, archetype);
  if (structure == nullptr) {
    return nullptr;
  }
  const auto found = std::ranges::find(
      registry.care_facilities, structure->metadata.stable_id,
      &CareFacilityContentDefinition::structure);
  return found == registry.care_facilities.end() ? nullptr : &*found;
}

std::string_view faction_presentation_key(const FactionId faction) noexcept {
  const auto& registry = builtin_content();
  const auto found =
      std::ranges::find(registry.factions, faction,
                        &FactionContentDefinition::faction);
  return found == registry.factions.end()
             ? std::string_view{}
             : found->metadata.presentation_key;
}

}  // namespace ashen::core
