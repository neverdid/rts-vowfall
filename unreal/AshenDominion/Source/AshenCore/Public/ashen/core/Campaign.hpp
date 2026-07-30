#pragma once

#include "ashen/core/Types.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace ashen::core {

enum class CampaignAct : std::uint8_t {
  Prologue,
  HonestMercies,
  CountriesOfTheAbandoned,
  WarInsideEveryFaction,
  VowTheLivingCanRefuse,
};

enum class CampaignPerspective : std::uint8_t {
  Mara,
  Aurel,
  Tavra,
  Ione,
  Ensemble,
};

struct StoryMissionDefinition {
  StoryMissionId id{StoryMissionId::BridgeOfNames};
  CampaignAct act{CampaignAct::Prologue};
  CampaignPerspective perspective{CampaignPerspective::Mara};
  std::uint8_t campaign_order{};
  FactionId player_faction{FactionId::Compact};
  FactionId opposing_faction{FactionId::Ascendancy};
  std::string_view title{};
  std::string_view protagonist{};
  std::string_view opening_action{};
  std::string_view briefing{};
  std::string_view objective{};
  std::string_view public_vow{};
  std::string_view reversal{};
  std::string_view gameplay_choice{};
  std::string_view consequence_key{};
  bool vertical_slice_ready{};
};

[[nodiscard]] ASHENCORE_API std::span<const StoryMissionDefinition> story_missions() noexcept;
[[nodiscard]] ASHENCORE_API const StoryMissionDefinition* find_story_mission(
    StoryMissionId mission) noexcept;
[[nodiscard]] ASHENCORE_API std::string_view campaign_act_label(CampaignAct act) noexcept;
[[nodiscard]] ASHENCORE_API std::string_view campaign_perspective_label(
    CampaignPerspective perspective) noexcept;

}  // namespace ashen::core
