#include "ashen/core/Campaign.hpp"

#include <array>

namespace ashen::core {
namespace {

constexpr std::array kMissions{
    StoryMissionDefinition{
        StoryMissionId::BridgeOfNames,
        CampaignAct::Prologue,
        CampaignPerspective::Mara,
        0,
        FactionId::Compact,
        FactionId::Ascendancy,
        "The Bridge of Names",
        "Young Mara Veyr",
        "Repair ferries, count medicine, and keep Greywake's last crossing open.",
        "Twenty years before Bellgrave, enemy pressure, a rising Dread Tide, and Aurel's plague "
        "warning converge on one bridge and one defensible order.",
        "Keep the crossing open while the refugee column reaches Greywake.",
        "The bridge will remain open.",
        "Every institution reports a different number of people still beyond the bridge.",
        "Place the final demolition order without being told whether it is morally correct.",
        "bridge_final_order",
        true,
    },
    StoryMissionDefinition{
        StoryMissionId::OpenBowl,
        CampaignAct::HonestMercies,
        CampaignPerspective::Mara,
        1,
        FactionId::Compact,
        FactionId::Ascendancy,
        "The Open Bowl",
        "Marshal Mara Veyr",
        "Keep three settlements supplied through a winter offensive.",
        "Compact command marks one settlement as strategically unnecessary.",
        "Preserve the retreat network while the capital demands a defensible loss.",
        "No settlement carrying the retreat will be abandoned.",
        "The condemned settlement's ferrymen and kitchens are the only systems keeping the retreat alive.",
        "Keep, break, or amend the promise after learning whose labor the strategy failed to count.",
        "open_bowl_settlement",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::MercyForTheUncounted,
        CampaignAct::HonestMercies,
        CampaignPerspective::Aurel,
        2,
        FactionId::Ascendancy,
        FactionId::Compact,
        "Mercy for the Uncounted",
        "Aurel Sorn",
        "Defend a free hospital and its evacuation corridor.",
        "The Ascendancy feeds, treats, and shelters people every other government rejected.",
        "Keep the hospital functioning until the last civilian convoy departs.",
        "All wounded people will receive treatment.",
        "Several Compact attackers have families receiving care inside the hospital.",
        "Decide whether a volunteer's desperate request for Absolution can settle their future.",
        "hospital_absolution",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::SecondTelling,
        CampaignAct::HonestMercies,
        CampaignPerspective::Tavra,
        3,
        FactionId::Concord,
        FactionId::Compact,
        "The Second Telling",
        "Tavra Nine-Reeds",
        "Restore a memory orchard and wake a buried Dorrin archive.",
        "The first recovered account says humans deliberately poisoned the watershed.",
        "Recover both testimonies before either faction can seal the archive.",
        "The archive will not be made to speak with one voice.",
        "Elder authorities diverted the original river and forced the human settlement onto poisoned land.",
        "Choose who may interpret the archive without allowing testimony to become command.",
        "archive_second_testimony",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::CityThatWasWrittenOff,
        CampaignAct::CountriesOfTheAbandoned,
        CampaignPerspective::Mara,
        4,
        FactionId::Compact,
        FactionId::Ascendancy,
        "The City That Was Written Off",
        "Mara Veyr and Tomas Rill",
        "Negotiate road access through the city built by the abandoned.",
        "Mara expects a ruin and finds elections, kitchens, clinics, adopted-household law, and no desire "
        "to make her either savior or monster.",
        "Keep trade moving while three governments contest the city's legal existence.",
        "No authority will erase this city to simplify the road.",
        "Recognition by any major faction would invalidate households protected by another.",
        "Protect the city's right to define itself even when that blocks Mara's campaign.",
        "uncounted_road_charter",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::LastContradiction,
        CampaignAct::CountriesOfTheAbandoned,
        CampaignPerspective::Ione,
        5,
        FactionId::Ascendancy,
        FactionId::Compact,
        "The Last Contradiction",
        "Ione Vale",
        "Prepare four petitioners who have requested Absolution.",
        "Each petitioner names two sincere desires that no permanent purpose can preserve together.",
        "Protect every petitioner while alternatives, advocates, and waiting periods remain available.",
        "No single yes will own a person's future.",
        "The siege makes delay dangerous and transformation immediately useful.",
        "Delay, approve, redirect, or build an alternative without a route that saves everyone.",
        "absolution_consent_record",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::RiverWithTwoHistories,
        CampaignAct::CountriesOfTheAbandoned,
        CampaignPerspective::Tavra,
        6,
        FactionId::Concord,
        FactionId::Compact,
        "A River with Two Histories",
        "Tavra Nine-Reeds",
        "Redirect a river to stop an advancing army.",
        "One channel protects a Concord archive; the other protects a human district.",
        "Hold the waterworks long enough for both communities to state their claim.",
        "The river will not be diverted without those downstream.",
        "Saving either community destroys something the other cannot replace.",
        "Choose a loss, or earn a third route whose cost falls somewhere else.",
        "river_diversion",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::EmergencyWithoutEnd,
        CampaignAct::WarInsideEveryFaction,
        CampaignPerspective::Mara,
        7,
        FactionId::Compact,
        FactionId::Ascendancy,
        "Emergency Without End",
        "Mara Veyr",
        "Stabilize food distribution after Cassel suspends civilian government.",
        "Cassel's emergency rule prevents immediate collapse and abolishes the public's ability to end it.",
        "Keep the capital supplied without making military necessity permanent law.",
        "Civil command will return before victory makes refusal impossible.",
        "Removing Cassel immediately may kill more people than tolerating him.",
        "Decide who may countermand the army while the emergency is still real.",
        "compact_emergency_authority",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::KindestPrison,
        CampaignAct::WarInsideEveryFaction,
        CampaignPerspective::Ione,
        8,
        FactionId::Ascendancy,
        FactionId::Compact,
        "The Kindest Prison",
        "Ione Vale",
        "Keep a besieged region alive under emergency Absolution.",
        "Refusal remains legal while food, medicine, rank, family, and ceremony make it impossible.",
        "Build a viable untransformed route through the siege.",
        "Mercy will remain possible to refuse.",
        "The transformed workforce is the fastest way to prevent mass starvation.",
        "Preserve refusal without pretending coercive circumstances make every consent meaningless.",
        "ascendancy_refusal_right",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::OldestVeto,
        CampaignAct::WarInsideEveryFaction,
        CampaignPerspective::Tavra,
        9,
        FactionId::Concord,
        FactionId::Compact,
        "The Oldest Veto",
        "Tavra Nine-Reeds",
        "Stop Vaun from restoring the First Vow without human participation.",
        "Vaun has credible evidence that uncontrolled extraction will collapse the memory cycle.",
        "Protect the watershed while opening the restoration council.",
        "The dead may testify, but they will not command the living.",
        "Destroying elder control without replacing its ecological work proves Vaun right.",
        "Oppose inherited authority while preserving the knowledge on which everyone depends.",
        "concord_veto",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::FieldOfNails,
        CampaignAct::WarInsideEveryFaction,
        CampaignPerspective::Mara,
        10,
        FactionId::Compact,
        FactionId::Ascendancy,
        "The Field of Nails",
        "Mara Veyr and Tomas Rill",
        "Bring conflicting Bridge testimonies into the public record.",
        "Mara admits that the official casualty count was knowingly incomplete.",
        "Protect witnesses who accuse every faction, including Mara herself.",
        "No testimony will be silenced to preserve a useful verdict.",
        "Some witnesses reject the names and identities under which they were mourned.",
        "Surrender control over how the truth judges the protagonist.",
        "bridge_public_testimony",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::CouncilUnderFire,
        CampaignAct::VowTheLivingCanRefuse,
        CampaignPerspective::Ensemble,
        11,
        FactionId::Compact,
        FactionId::Ascendancy,
        "The Council Under Fire",
        "Mara, Aurel, Tavra, Tomas, and Ione",
        "Keep an embodied council alive while the Quiet spreads.",
        "Every delegation will leave if the battlefield violates the condition that makes its presence "
        "meaningful.",
        "Maintain enough legitimate disagreement that no institution can claim to speak for all.",
        "Every affected voice will retain the power to refuse this council.",
        "The player cannot satisfy every condition or preserve every tactical advantage.",
        "Protect a plural agreement rather than manufacturing unanimity.",
        "living_council",
        false,
    },
    StoryMissionDefinition{
        StoryMissionId::NamesAtTheWater,
        CampaignAct::VowTheLivingCanRefuse,
        CampaignPerspective::Ensemble,
        12,
        FactionId::Compact,
        FactionId::Ascendancy,
        "Names at the Water",
        "The living and the Uncounted",
        "Release people and places from the final commands stored inside them.",
        "The Quiet completes promises perfectly: soldiers repeat orders, healers cannot stop, guardians "
        "turn wards into prisons, and workers mine beyond survival.",
        "Restore the ability to stop, reinterpret, contradict, and refuse.",
        "No promise may speak for the living without their continuing answer.",
        "Winning requires relinquishing authority rather than concentrating more of it.",
        "Issue orders that free their recipients from the order itself.",
        "answerable_vow",
        false,
    },
};

}  // namespace

std::span<const StoryMissionDefinition> story_missions() noexcept {
  return kMissions;
}

const StoryMissionDefinition* find_story_mission(const StoryMissionId mission) noexcept {
  const auto index = static_cast<std::size_t>(mission);
  return index < kMissions.size() && kMissions[index].id == mission ? &kMissions[index] : nullptr;
}

std::string_view campaign_act_label(const CampaignAct act) noexcept {
  switch (act) {
    case CampaignAct::Prologue:
      return "Prologue";
    case CampaignAct::HonestMercies:
      return "Act I - Three Honest Mercies";
    case CampaignAct::CountriesOfTheAbandoned:
      return "Act II - Countries of the Abandoned";
    case CampaignAct::WarInsideEveryFaction:
      return "Act III - The War Inside Every Faction";
    case CampaignAct::VowTheLivingCanRefuse:
      return "Act IV - A Vow the Living Can Refuse";
  }
  return {};
}

std::string_view campaign_perspective_label(const CampaignPerspective perspective) noexcept {
  switch (perspective) {
    case CampaignPerspective::Mara:
      return "Mara Veyr";
    case CampaignPerspective::Aurel:
      return "Aurel Sorn";
    case CampaignPerspective::Tavra:
      return "Tavra Nine-Reeds";
    case CampaignPerspective::Ione:
      return "Ione Vale";
    case CampaignPerspective::Ensemble:
      return "Ensemble";
  }
  return {};
}

}  // namespace ashen::core
