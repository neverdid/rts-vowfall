#include "ashen/core/Catalog.hpp"

namespace ashen::core {
namespace {

[[nodiscard]] EntityDefinition base_definition(const EntityType type) noexcept {
  switch (type) {
    case EntityType::Worker:
      return {type, EntityKind::Unit, "Fieldwright", 45, 120, 44, 13'000, 4'800, 26'000, 3, 22, 190'000,
              ArmorClass::Laborer, ArmorClass::Laborer, false, 0, 0, 4, 1, 0};
    case EntityType::Vanguard:
      return {type, EntityKind::Unit, "Pikeguard", 72, 160, 84, 15'000, 4'400, 28'000, 10, 18, 230'000,
              ArmorClass::Armored, ArmorClass::Structure, true, 4, 4, 8, 2, 0};
    case EntityType::Skirmisher:
      return {type, EntityKind::Unit, "Cinder Arbalest", 95, 200, 58, 14'000, 6'000, 148'000, 9, 16, 260'000,
              ArmorClass::Light, ArmorClass::Armored, true, 6, 5, 3, 2, 0};
    case EntityType::Command:
      return {type, EntityKind::Building, "March Keep", 0, 0, 720, 46'000, 0, 0, 0, 0, 320'000,
              ArmorClass::Structure, ArmorClass::Laborer, false, 0, 12, 40, 0, 14};
    case EntityType::Barracks:
      return {type, EntityKind::Building, "Assembly Hall", 160, 280, 430, 36'000, 0, 0, 0, 0, 260'000,
              ArmorClass::Structure, ArmorClass::Laborer, false, 0, 10, 16, 0, 8};
    case EntityType::Turret:
      return {type, EntityKind::Building, "Signal Bastion", 120, 220, 260, 27'000, 0, 172'000, 13, 16, 270'000,
              ArmorClass::Structure, ArmorClass::Light, true, 5, 18, 34, 0, 0};
    case EntityType::Hospital:
      return {type, EntityKind::Building, "Field Hospital", 145, 260, 360,
              34'000, 0, 0, 0, 0, 260'000, ArmorClass::Structure,
              ArmorClass::Laborer, false, 0, 4, 28, 0, 0};
  }
  return {};
}

void apply_ascendancy_overrides(EntityDefinition& definition) noexcept {
  switch (definition.type) {
    case EntityType::Worker:
      definition.label = "Votary";
      definition.cost = 40;
      definition.build_ticks = 104;
      definition.hit_points = 38;
      definition.radius = 14'000;
      definition.speed_per_tick = 5'600;
      definition.sight = 205'000;
      definition.terror = 2;
      definition.ward = 2;
      break;
    case EntityType::Vanguard:
      definition.label = "Crowned Bulwark";
      definition.cost = 104;
      definition.build_ticks = 230;
      definition.hit_points = 124;
      definition.radius = 19'000;
      definition.speed_per_tick = 3'900;
      definition.damage = 16;
      definition.attack_cooldown_ticks = 22;
      definition.terror = 20;
      definition.ward = 5;
      definition.supply_cost = 3;
      break;
    case EntityType::Skirmisher:
      definition.label = "Veil Seer";
      definition.cost = 112;
      definition.build_ticks = 220;
      definition.hit_points = 62;
      definition.radius = 15'000;
      definition.speed_per_tick = 5'300;
      definition.attack_range = 160'000;
      definition.damage = 11;
      definition.attack_cooldown_ticks = 19;
      definition.sight = 285'000;
      definition.terror = 13;
      definition.supply_cost = 3;
      break;
    case EntityType::Command:
      definition.label = "House of Quiet";
      definition.hit_points = 760;
      definition.sight = 330'000;
      definition.terror = 30;
      definition.ward = 10;
      definition.supply_provided = 13;
      break;
    case EntityType::Barracks:
      definition.label = "Chrysalis Court";
      definition.cost = 182;
      definition.build_ticks = 320;
      definition.hit_points = 470;
      definition.terror = 22;
      definition.ward = 8;
      break;
    case EntityType::Turret:
      definition.label = "Witness Needle";
      definition.cost = 135;
      definition.build_ticks = 240;
      definition.hit_points = 275;
      definition.attack_range = 185'000;
      definition.damage = 14;
      definition.attack_cooldown_ticks = 18;
      definition.terror = 24;
      definition.ward = 4;
      break;
    case EntityType::Hospital:
      break;
  }
}

void apply_concord_overrides(EntityDefinition& definition) noexcept {
  switch (definition.type) {
    case EntityType::Worker:
      definition.label = "Dorrin Stonewright";
      definition.cost = 55;
      definition.build_ticks = 144;
      definition.hit_points = 58;
      definition.radius = 14'000;
      definition.speed_per_tick = 4'100;
      definition.sight = 210'000;
      definition.ward = 10;
      break;
    case EntityType::Vanguard:
      definition.label = "Orrun Warden";
      definition.cost = 108;
      definition.build_ticks = 250;
      definition.hit_points = 136;
      definition.radius = 21'000;
      definition.speed_per_tick = 3'400;
      definition.damage = 18;
      definition.attack_cooldown_ticks = 24;
      definition.bonus_damage = 9;
      definition.terror = 2;
      definition.ward = 12;
      definition.supply_cost = 4;
      break;
    case EntityType::Skirmisher:
      definition.label = "Lethai Waybow";
      definition.cost = 106;
      definition.build_ticks = 216;
      definition.hit_points = 58;
      definition.radius = 15'000;
      definition.speed_per_tick = 6'600;
      definition.attack_range = 172'000;
      definition.damage = 9;
      definition.attack_cooldown_ticks = 16;
      definition.sight = 315'000;
      definition.ward = 6;
      definition.supply_cost = 2;
      break;
    case EntityType::Command:
      definition.label = "Meeting Stone";
      definition.hit_points = 900;
      definition.sight = 360'000;
      definition.terror = 4;
      definition.ward = 62;
      definition.supply_provided = 13;
      break;
    case EntityType::Barracks:
      definition.label = "Treaty Grove";
      definition.cost = 188;
      definition.build_ticks = 340;
      definition.hit_points = 570;
      definition.ward = 32;
      definition.supply_provided = 7;
      break;
    case EntityType::Turret:
      definition.label = "Resonance Bell";
      definition.cost = 150;
      definition.build_ticks = 260;
      definition.hit_points = 340;
      definition.attack_range = 190'000;
      definition.damage = 16;
      definition.attack_cooldown_ticks = 20;
      definition.ward = 46;
      break;
    case EntityType::Hospital:
      break;
  }
}

}  // namespace

FactionDefinition faction_definition(const FactionId faction) noexcept {
  switch (faction) {
    case FactionId::Compact:
      return {faction, "Cinder Compact", 10'000, 0};
    case FactionId::Ascendancy:
      return {faction, "Gloam Ascendancy", 10'400, 6};
    case FactionId::Concord:
      return {faction, "Elder Concord", 9'600, 10};
  }
  return {};
}

ResearchDefinition research_definition(const ResearchId research) noexcept {
  switch (research) {
    case ResearchId::TierTwo:
      return {research, std::nullopt, "Black-Iron Age", 165, 320, EntityType::Command, std::nullopt};
    case ResearchId::TemperedOaths:
      return {research, FactionId::Compact, "Interlocking Drills", 145, 260, EntityType::Barracks,
              ResearchId::TierTwo};
    case ResearchId::Wardcraft:
      return {research, FactionId::Compact, "Field Charters", 130, 240, EntityType::Command,
              ResearchId::TierTwo};
    case ResearchId::ChorusOfKnives:
      return {research, FactionId::Ascendancy, "Perfected Purpose", 135, 240, EntityType::Barracks,
              ResearchId::TierTwo};
    case ResearchId::PitBroods:
      return {research, FactionId::Ascendancy, "Merciful Shaping", 150, 280, EntityType::Barracks,
              ResearchId::TierTwo};
    case ResearchId::VaultPlate:
      return {research, FactionId::Concord, "Living Ramparts", 165, 300, EntityType::Command,
              ResearchId::TierTwo};
    case ResearchId::SiegeLiturgy:
      return {research, FactionId::Concord, "Deep Resonance", 155, 280, EntityType::Barracks,
              ResearchId::TierTwo};
  }
  return {};
}

PowerDefinition power_definition(const FactionId faction) noexcept {
  switch (faction) {
    case FactionId::Compact:
      return {faction, "No One Left Uncounted", 45, 960};
    case FactionId::Ascendancy:
      return {faction, "Manifest Absolution", 72, 1'120};
    case FactionId::Concord:
      return {faction, "Wake the Ground", 60, 1'160};
  }
  return {};
}

EntityDefinition entity_definition(const FactionId faction, const EntityType type) noexcept {
  auto definition = base_definition(type);
  if (faction == FactionId::Ascendancy) {
    apply_ascendancy_overrides(definition);
  } else if (faction == FactionId::Concord) {
    apply_concord_overrides(definition);
  }
  return definition;
}

bool can_train(const EntityType producer, const EntityType unit) noexcept {
  return (producer == EntityType::Command && unit == EntityType::Worker) ||
         (producer == EntityType::Barracks &&
          (unit == EntityType::Vanguard || unit == EntityType::Skirmisher));
}

bool is_unit(const EntityType type) noexcept {
  return type == EntityType::Worker || type == EntityType::Vanguard || type == EntityType::Skirmisher;
}

bool is_building(const EntityType type) noexcept {
  return type == EntityType::Command || type == EntityType::Barracks ||
         type == EntityType::Turret || type == EntityType::Hospital;
}

std::string_view to_string(const FactionId faction) noexcept {
  return faction_definition(faction).name;
}

std::string_view to_string(const EntityType type) noexcept {
  return base_definition(type).label;
}

std::string_view to_string(const ResearchId research) noexcept {
  return research_definition(research).label;
}

}  // namespace ashen::core
