#include "ashen/core/Campaign.hpp"
#include "ashen/core/Simulation.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <set>
#include <string>
#include <string_view>

namespace {

using namespace ashen::core;

int failures = 0;

#define CHECK(condition)                                                                            \
  do {                                                                                              \
    if (!(condition)) {                                                                             \
      std::cerr << "  check failed at line " << __LINE__ << ": " #condition "\n";                \
      ++failures;                                                                                   \
    }                                                                                               \
  } while (false)

template <typename Test>
void run_test(const std::string_view name, Test&& test) {
  const auto before = failures;
  test();
  std::cout << (failures == before ? "[pass] " : "[fail] ") << name << '\n';
}

void campaign_catalog_is_complete_and_ordered() {
  const auto missions = story_missions();
  CHECK(missions.size() == static_cast<std::size_t>(StoryMissionId::Count));

  std::set<std::string> titles;
  std::array<std::size_t, 5> missions_per_act{};
  for (std::size_t index = 0; index < missions.size(); ++index) {
    const auto& mission = missions[index];
    CHECK(static_cast<std::size_t>(mission.id) == index);
    CHECK(mission.campaign_order == index);
    CHECK(!mission.title.empty());
    CHECK(!mission.protagonist.empty());
    CHECK(!mission.opening_action.empty());
    CHECK(!mission.briefing.empty());
    CHECK(!mission.objective.empty());
    CHECK(!mission.public_vow.empty());
    CHECK(!mission.reversal.empty());
    CHECK(!mission.gameplay_choice.empty());
    CHECK(!mission.consequence_key.empty());
    CHECK(titles.insert(std::string{mission.title}).second);
    ++missions_per_act[static_cast<std::size_t>(mission.act)];
  }

  CHECK((missions_per_act == std::array<std::size_t, 5>{1, 3, 3, 4, 2}));
}

void prologue_is_the_only_currently_playable_story() {
  std::size_t playable = 0;
  for (const auto& mission : story_missions()) {
    playable += mission.vertical_slice_ready ? 1U : 0U;
  }

  const auto* prologue = find_story_mission(StoryMissionId::BridgeOfNames);
  CHECK(prologue != nullptr);
  CHECK(prologue != nullptr && prologue->vertical_slice_ready);
  CHECK(prologue != nullptr && prologue->act == CampaignAct::Prologue);
  CHECK(prologue != nullptr && prologue->perspective == CampaignPerspective::Mara);
  CHECK(prologue != nullptr && prologue->player_faction == FactionId::Compact);
  CHECK(playable == 1);
  CHECK(find_story_mission(StoryMissionId::Count) == nullptr);
}

void story_identity_is_authoritative_simulation_state() {
  SimulationConfig bridge_config{};
  bridge_config.mode = MatchMode::Story;
  bridge_config.story_mission = StoryMissionId::BridgeOfNames;

  SimulationConfig river_config = bridge_config;
  river_config.story_mission = StoryMissionId::RiverWithTwoHistories;

  const Simulation bridge{bridge_config};
  const Simulation river{river_config};
  CHECK(bridge.mode() == MatchMode::Story);
  CHECK(bridge.config().story_mission == StoryMissionId::BridgeOfNames);
  CHECK(river.config().story_mission == StoryMissionId::RiverWithTwoHistories);
  CHECK(bridge.state_hash() != river.state_hash());
}

void every_act_and_perspective_has_a_label() {
  constexpr std::array acts{
      CampaignAct::Prologue,
      CampaignAct::HonestMercies,
      CampaignAct::CountriesOfTheAbandoned,
      CampaignAct::WarInsideEveryFaction,
      CampaignAct::VowTheLivingCanRefuse,
  };
  constexpr std::array perspectives{
      CampaignPerspective::Mara,
      CampaignPerspective::Aurel,
      CampaignPerspective::Tavra,
      CampaignPerspective::Ione,
      CampaignPerspective::Ensemble,
  };
  for (const auto act : acts) {
    CHECK(!campaign_act_label(act).empty());
  }
  for (const auto perspective : perspectives) {
    CHECK(!campaign_perspective_label(perspective).empty());
  }
}

}  // namespace

int main() {
  run_test("campaign catalog is complete and ordered", campaign_catalog_is_complete_and_ordered);
  run_test("prologue is the only currently playable story", prologue_is_the_only_currently_playable_story);
  run_test("story identity is authoritative simulation state",
           story_identity_is_authoritative_simulation_state);
  run_test("every act and perspective has a label", every_act_and_perspective_has_a_label);

  if (failures != 0) {
    std::cerr << failures << " campaign check(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All campaign checks passed.\n";
  return EXIT_SUCCESS;
}
