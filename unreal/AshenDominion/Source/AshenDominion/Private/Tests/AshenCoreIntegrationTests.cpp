#if WITH_DEV_AUTOMATION_TESTS

#include "AshenArena.h"
#include "AshenCheckpointSaveGame.h"
#include "AshenEnvironmentKit.h"
#include "AshenWorldLayout.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInterface.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectGlobals.h"
#include "ashen/core/AIDifficulty.hpp"
#include "ashen/core/AIDoctrine.hpp"
#include "ashen/core/AIInfluenceMap.hpp"
#include "ashen/core/Campaign.hpp"
#include "ashen/core/Catalog.hpp"
#include "ashen/core/CommanderAI.hpp"
#include "ashen/core/Content.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/Snapshot.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <span>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreBootsInUnrealTest, "Ashen.Core.BootsInUnreal",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCoreBootsInUnrealTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);

    ashen::core::Simulation First{};
    ashen::core::Simulation Second{};

    TestEqual(TEXT("Default match seeds ten entities"), static_cast<int32>(First.entities().size()), 10);
    TestEqual(TEXT("Default match seeds seven resource fields"), static_cast<int32>(First.resources().size()), 7);
    TestEqual(TEXT("Default match seeds two contestable relics"), static_cast<int32>(First.control_points().size()), 2);

    First.run(240);
    Second.run(240);

    TestEqual(TEXT("Simulation advances at a deterministic tick"), static_cast<int64>(First.tick()), int64{240});
    TestTrue(TEXT("Equivalent matches produce the same state hash"), First.state_hash() == Second.state_hash());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCheckpointSaveAdapterTest, "Ashen.Core.UnrealCheckpointAdapter",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCheckpointSaveAdapterTest::RunTest(const FString& Parameters)
{
    static_cast<void>(Parameters);

    ashen::core::Simulation Original{};
    Original.run(73);
    const std::vector<std::uint8_t> Snapshot = ashen::core::save_snapshot_v1(Original);

    UAshenCheckpointSaveGame* Save = Cast<UAshenCheckpointSaveGame>(
        UGameplayStatics::CreateSaveGameObject(UAshenCheckpointSaveGame::StaticClass()));
    TestNotNull(TEXT("Unreal creates the checkpoint adapter"), Save);
    if (Save == nullptr)
    {
        return false;
    }

    Save->SnapshotSchemaVersion = ashen::core::kSnapshotSchemaVersion;
    Save->ContentDigest = ashen::core::current_content_digest();
    Save->PipelineDigest = ashen::core::current_pipeline_digest();
    Save->CheckpointTick = Original.tick();
    Save->CheckpointStateHash = Original.state_hash();
    Save->SavedAtUtc = FDateTime::UtcNow();
    Save->SnapshotBytes.Append(Snapshot.data(), static_cast<int32>(Snapshot.size()));

    const FString SlotName = FString::Printf(TEXT("AshenCheckpointAdapterTest-%s"),
                                              *FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, SlotName, 0);
    TestTrue(TEXT("Checkpoint adapter writes through Unreal's save-game system"), bSaved);

    const UAshenCheckpointSaveGame* Loaded = Cast<UAshenCheckpointSaveGame>(
        UGameplayStatics::LoadGameFromSlot(SlotName, 0));
    TestNotNull(TEXT("Checkpoint adapter reloads from the save slot"), Loaded);
    if (Loaded != nullptr)
    {
        TestEqual(TEXT("Adapter version survives serialization"), Loaded->AdapterVersion,
                  UAshenCheckpointSaveGame::CurrentAdapterVersion);
        TestEqual(TEXT("Checkpoint tick survives serialization"), Loaded->CheckpointTick, Original.tick());
        TestTrue(TEXT("Snapshot bytes survive serialization"), Loaded->SnapshotBytes == Save->SnapshotBytes);

        const std::span<const std::uint8_t> Bytes{
            Loaded->SnapshotBytes.GetData(), static_cast<size_t>(Loaded->SnapshotBytes.Num())};
        ashen::core::SnapshotLoadResult Restored = ashen::core::load_snapshot_v1(Bytes);
        TestTrue(TEXT("SnapshotV1 accepts the Unreal-persisted payload"), static_cast<bool>(Restored));
        if (Restored)
        {
            TestTrue(TEXT("Unreal save round trip preserves the authoritative state hash"),
                     Restored.simulation->state_hash() == Original.state_hash());
        }
    }

    TestTrue(TEXT("Automation checkpoint slot is removed"),
             UGameplayStatics::DeleteGameInSlot(SlotName, 0));
    return bSaved && Loaded != nullptr;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenFactionIdentityInUnrealTest,
                                 "Ashen.Core.FactionIdentity",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FAshenFactionIdentityInUnrealTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    SimulationConfig Swapped{};
    Swapped.seed_starting_forces = false;
    Swapped.player_one_faction = FactionId::Ascendancy;
    Swapped.player_two_faction = FactionId::Compact;
    Swapped.commander_players = {true, false};
    Simulation Match{Swapped};
    const EntityId AIControlled =
        Match.spawn_entity(PlayerId::One, EntityType::Vanguard, world(200, 200));
    const EntityId HumanControlled =
        Match.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(1'000, 600));
    TestTrue(TEXT("AI control does not select the entity faction"),
             Match.find_entity(AIControlled) != nullptr &&
                 Match.find_entity(AIControlled)->faction == FactionId::Ascendancy);
    TestTrue(TEXT("Human control does not select the entity faction"),
             Match.find_entity(HumanControlled) != nullptr &&
                 Match.find_entity(HumanControlled)->faction == FactionId::Compact);
    TestTrue(TEXT("Presentation metadata follows explicit faction identity"),
             faction_presentation_key(Match.find_entity(AIControlled)->faction) ==
                     "vowfall.faction.ascendancy" &&
                 faction_presentation_key(Match.find_entity(HumanControlled)->faction) ==
                     "vowfall.faction.compact");

    SimulationConfig Mirror = Swapped;
    Mirror.player_two_faction = FactionId::Ascendancy;
    Mirror.commander_players = {false, true};
    Simulation MirrorMatch{Mirror};
    const EntityId MirrorOne =
        MirrorMatch.spawn_entity(PlayerId::One, EntityType::Worker, world(200, 200));
    const EntityId MirrorTwo =
        MirrorMatch.spawn_entity(PlayerId::Two, EntityType::Worker, world(1'000, 600));
    TestTrue(TEXT("Mirror slots resolve the same faction presentation key"),
             faction_presentation_key(MirrorMatch.find_entity(MirrorOne)->faction) ==
                 faction_presentation_key(MirrorMatch.find_entity(MirrorTwo)->faction));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCampaignCatalogInUnrealTest,
                                 "Ashen.Story.CampaignCatalog",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FAshenCampaignCatalogInUnrealTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    const std::span<const StoryMissionDefinition> Missions = story_missions();
    TestEqual(TEXT("The authored campaign contains thirteen ordered missions"),
              static_cast<int32>(Missions.size()),
              static_cast<int32>(StoryMissionId::Count));

    TSet<FString> Titles;
    int32 PlayableMissions = 0;
    for (int32 Index = 0; Index < static_cast<int32>(Missions.size()); ++Index)
    {
        const StoryMissionDefinition &Mission = Missions[static_cast<size_t>(Index)];
        TestEqual(TEXT("Mission id remains aligned with campaign order"),
                  static_cast<int32>(Mission.id), Index);
        TestFalse(TEXT("Every mission has an RTS objective"), Mission.objective.empty());
        TestFalse(TEXT("Every mission begins with a public vow"), Mission.public_vow.empty());
        TestFalse(TEXT("Every mission owns a reversal"), Mission.reversal.empty());
        Titles.Add(FString(UTF8_TO_TCHAR(Mission.title.data())));
        PlayableMissions += Mission.vertical_slice_ready ? 1 : 0;
    }
    TestEqual(TEXT("Mission titles remain unique"), Titles.Num(), static_cast<int32>(Missions.size()));
    TestEqual(TEXT("Only the honest prologue is marked playable"), PlayableMissions, 1);

    const StoryMissionDefinition *Prologue =
        find_story_mission(StoryMissionId::BridgeOfNames);
    TestNotNull(TEXT("The Bridge of Names is the campaign entry"), Prologue);
    TestTrue(TEXT("The prologue launches as a Compact perspective"),
             Prologue != nullptr && Prologue->player_faction == FactionId::Compact);

    SimulationConfig StoryConfig{};
    StoryConfig.mode = MatchMode::Story;
    StoryConfig.story_mission = StoryMissionId::BridgeOfNames;
    const Simulation StoryMatch{StoryConfig};
    TestEqual(TEXT("Unreal preserves authoritative story mode"),
              static_cast<uint8>(StoryMatch.mode()),
              static_cast<uint8>(MatchMode::Story));
    TestEqual(TEXT("Unreal preserves the selected story mission"),
              static_cast<uint8>(StoryMatch.config().story_mission),
              static_cast<uint8>(StoryMissionId::BridgeOfNames));
    TestTrue(TEXT("The authoritative scenario catalog validates in Unreal"),
             validate_scenarios(builtin_scenarios(), builtin_content()).empty());
    const std::optional<MissionObjectiveView> Objective =
        StoryMatch.primary_mission_objective();
    TestTrue(TEXT("The playable mission exposes a core-owned primary objective"),
             Objective.has_value() &&
                 Objective->content_id == content_id::BridgeObjective &&
                 Objective->status == MissionObjectiveStatus::Active &&
                 Objective->target_tick == 60 * kTicksPerSecond);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreNavigationAndOrdersTest, "Ashen.Core.NavigationAndOrders",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCoreNavigationAndOrdersTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    SimulationConfig Config{};
    Config.seed_starting_forces = false;
    Simulation First{Config};
    Simulation Second{Config};
    const EntityId FirstUnit = First.spawn_entity(PlayerId::One, EntityType::Worker, world(1'000, 700));
    const EntityId SecondUnit = Second.spawn_entity(PlayerId::One, EntityType::Worker, world(1'000, 700));

    const Command CrossRiver{
        .player = PlayerId::One, .type = CommandType::AttackMove, .entities = {FirstUnit}, .target = world(1'400, 700)};
    Command MirroredCrossRiver = CrossRiver;
    MirroredCrossRiver.entities = {SecondUnit};
    TestTrue(TEXT("Attack-move accepts a destination across the river"), First.execute_now(CrossRiver).ok);
    TestTrue(TEXT("Equivalent attack-move is accepted"), Second.execute_now(MirroredCrossRiver).ok);

    const Command QueuedMove{.player = PlayerId::One,
                             .type = CommandType::Move,
                             .entities = {FirstUnit},
                             .target = world(1'480, 700),
                             .queue = true};
    Command MirroredQueuedMove = QueuedMove;
    MirroredQueuedMove.entities = {SecondUnit};
    TestTrue(TEXT("Shift-style queued move is accepted"), First.execute_now(QueuedMove).ok);
    TestTrue(TEXT("Equivalent queued move is accepted"), Second.execute_now(MirroredQueuedMove).ok);

    First.run(600);
    Second.run(600);
    TestTrue(TEXT("Unit completes both orders"),
             First.find_entity(FirstUnit) != nullptr && First.find_entity(FirstUnit)->position == world(1'480, 700));
    TestTrue(TEXT("Navigation and queued orders remain deterministic"), First.state_hash() == Second.state_hash());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreAuthoritativeFogTest, "Ashen.Core.AuthoritativeFogOfWar",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCoreAuthoritativeFogTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    SimulationConfig Config{};
    Config.seed_starting_forces = false;
    Simulation Match{Config};
    const EntityId Attacker = Match.spawn_entity(PlayerId::One, EntityType::Vanguard, world(100, 700));
    const EntityId Enemy = Match.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(1'250, 700));

    const Command HiddenAttack{.player = PlayerId::One,
                               .type = CommandType::Attack,
                               .entities = {Attacker},
                               .target_entity = Enemy};
    TestFalse(TEXT("An unseen enemy cannot be targeted"), Match.execute_now(HiddenAttack).ok);
    TestEqual(TEXT("Unscouted ground begins hidden"),
              static_cast<uint8>(Match.visibility_state_at(world(1'250, 700), PlayerId::One)),
              static_cast<uint8>(VisibilityState::Hidden));

    static_cast<void>(Match.spawn_entity(PlayerId::One, EntityType::Command, world(1'000, 700)));
    TestTrue(TEXT("A friendly observer reveals the enemy"),
             Match.is_entity_visible_to(*Match.find_entity(Enemy), PlayerId::One));
    TestTrue(TEXT("The same attack is accepted after scouting"), Match.execute_now(HiddenAttack).ok);

    TestTrue(TEXT("The enemy can withdraw"),
             Match.execute_now(Command{.player = PlayerId::Two,
                                       .type = CommandType::Move,
                                       .entities = {Enemy},
                                       .target = world(1'800, 700)})
                 .ok);
    Match.run(80);
    TestFalse(TEXT("The enemy leaves current vision"),
              Match.is_entity_visible_to(*Match.find_entity(Enemy), PlayerId::One));
    TestEqual(TEXT("Pursuit ends when contact is lost"), static_cast<uint8>(Match.find_entity(Attacker)->order.type),
              static_cast<uint8>(OrderType::Idle));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreHonestDifficultyTest,
                                 "Ashen.Core.HonestDifficulty",
                                 EAutomationTestFlags::EditorContext |
                                     EAutomationTestFlags::EngineFilter)

bool FAshenCoreHonestDifficultyTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    SimulationConfig Config{};
    Config.seed_starting_forces = false;
    Config.starting_ore = {1'000, 1'000};
    Config.map_size = world(2'000, 800);
    Config.navigation_obstacles.clear();
    Simulation Match{Config};
    static_cast<void>(Match.spawn_entity(PlayerId::One, EntityType::Command,
                                         world(100, 400)));
    static_cast<void>(Match.spawn_entity(PlayerId::Two, EntityType::Command,
                                         world(1'900, 400)));
    const EntityId Observer = Match.spawn_entity(
        PlayerId::One, EntityType::Vanguard, world(800, 400));
    const EntityId Contact = Match.spawn_entity(
        PlayerId::Two, EntityType::Vanguard, world(980, 400));
    static_cast<void>(Match.spawn_entity(PlayerId::One, EntityType::Worker,
                                         world(230, 400)));
    static_cast<void>(Match.add_resource(world(360, 400), 1'200));
    TestTrue(TEXT("The test observer can hold position"),
             Match.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Hold,
                                       .entities = {Observer}})
                 .ok);
    TestTrue(TEXT("The hostile contact can hold position"),
             Match.execute_now(Command{.player = PlayerId::Two,
                                       .type = CommandType::Hold,
                                       .entities = {Contact}})
                 .ok);

    CommanderAI Story{PlayerId::One, AIDifficulty::Story};
    CommanderAI Competitive{PlayerId::One, AIDifficulty::Competitive};
    const AIDifficultyProfile &StoryProfile =
        ai_difficulty_profile(AIDifficulty::Story);
    const AIDifficultyProfile &CompetitiveProfile =
        ai_difficulty_profile(AIDifficulty::Competitive);
    TestTrue(TEXT("Difficulty profiles have stable distinct fingerprints"),
             ai_difficulty_hash(StoryProfile) != 0 &&
                 ai_difficulty_hash(CompetitiveProfile) != 0 &&
                 ai_difficulty_hash(StoryProfile) !=
                     ai_difficulty_hash(CompetitiveProfile));

    bool bCompetitiveObservedOnSchedule = false;
    bool bStoryObservedOnSchedule = false;
    for (Tick TickIndex = 0; TickIndex <= StoryProfile.reaction_delay_ticks;
         ++TickIndex)
    {
        const PlayerObservation Raw = Match.observe(PlayerId::One);
        const PlayerObservation StoryView = Story.perceive(Raw);
        const PlayerObservation CompetitiveView = Competitive.perceive(Raw);
        const bool bStoryHasContact = std::ranges::any_of(
            StoryView.known_enemies(),
            [](const ObservedEnemy &Enemy) { return Enemy.currently_visible; });
        const bool bCompetitiveHasContact = std::ranges::any_of(
            CompetitiveView.known_enemies(),
            [](const ObservedEnemy &Enemy) { return Enemy.currently_visible; });

        TestEqual(TEXT("Difficulty does not alter current own ore"),
                  StoryView.self().ore, Raw.self().ore);
        TestEqual(TEXT("Difficulty does not alter current owned unit count"),
                  static_cast<int32>(StoryView.owned_entities().size()),
                  static_cast<int32>(Raw.owned_entities().size()));
        TestTrue(TEXT("Difficulty does not grant extra map vision"),
                 StoryView.explored_map().cells() ==
                     Raw.explored_map().cells());
        if (TickIndex < CompetitiveProfile.reaction_delay_ticks)
        {
            TestFalse(TEXT("Competitive still respects its reaction window"),
                      bCompetitiveHasContact);
        }
        if (TickIndex < StoryProfile.reaction_delay_ticks)
        {
            TestFalse(TEXT("Story still respects its reaction window"),
                      bStoryHasContact);
        }
        bCompetitiveObservedOnSchedule =
            bCompetitiveObservedOnSchedule ||
            (TickIndex == CompetitiveProfile.reaction_delay_ticks &&
             bCompetitiveHasContact);
        bStoryObservedOnSchedule =
            bStoryObservedOnSchedule ||
            (TickIndex == StoryProfile.reaction_delay_ticks &&
             bStoryHasContact);

        if (TickIndex == 1)
        {
            const CommanderPlan StoryPlan = Story.plan(Raw);
            const CommanderPlan CompetitivePlan = Competitive.plan(Raw);
            TestTrue(TEXT("Story decisions retain the selected profile"),
                     !StoryPlan.decisions.empty() &&
                         std::ranges::all_of(
                             StoryPlan.decisions,
                             [&StoryProfile](const AIPlannedDecision &Decision)
                             {
                                 return Decision.difficulty ==
                                            AIDifficulty::Story &&
                                     Decision.difficulty_hash ==
                                         ai_difficulty_hash(StoryProfile);
                             }));
            TestTrue(TEXT("Competitive decisions retain the selected profile"),
                     !CompetitivePlan.decisions.empty() &&
                         std::ranges::all_of(
                             CompetitivePlan.decisions,
                             [&CompetitiveProfile](
                                 const AIPlannedDecision &Decision)
                             {
                                 return Decision.difficulty ==
                                            AIDifficulty::Competitive &&
                                     Decision.difficulty_hash ==
                                         ai_difficulty_hash(
                                             CompetitiveProfile);
                             }));
        }
        if (TickIndex != StoryProfile.reaction_delay_ticks)
        {
            Match.step();
        }
    }

    TestTrue(TEXT("Competitive observes contact after its full delay"),
             bCompetitiveObservedOnSchedule);
    TestTrue(TEXT("Story observes contact after its full delay"),
             bStoryObservedOnSchedule);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreInfluenceTacticsTest,
                                 "Ashen.Core.InfluenceTactics",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCoreInfluenceTacticsTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    SimulationConfig Config{};
    Config.seed_starting_forces = false;
    Config.starting_ore = {0, 0};
    Config.map_size = world(1'200, 800);
    Config.navigation_obstacles = {{world(700, 180), world(820, 620)}};
    Simulation Match{Config};
    static_cast<void>(Match.spawn_entity(PlayerId::One, EntityType::Command, world(160, 400)));
    const EntityId First = Match.spawn_entity(PlayerId::One, EntityType::Vanguard, world(350, 370));
    const EntityId Second = Match.spawn_entity(PlayerId::One, EntityType::Vanguard, world(350, 430));
    const EntityId Ranged = Match.spawn_entity(PlayerId::One, EntityType::Skirmisher, world(390, 400));
    static_cast<void>(Match.spawn_entity(PlayerId::Two, EntityType::Command, world(1'080, 400)));
    const EntityId Contact = Match.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(590, 400));
    static_cast<void>(Match.spawn_entity(PlayerId::Two, EntityType::Turret, world(570, 245)));
    TestTrue(TEXT("Friendly formation can hold for a deterministic tactical snapshot"),
             Match.execute_now(Command{.player = PlayerId::One,
                                       .type = CommandType::Hold,
                                       .entities = {First, Second, Ranged}})
                 .ok);
    TestTrue(TEXT("Enemy contact can hold for a deterministic tactical snapshot"),
             Match.execute_now(Command{.player = PlayerId::Two,
                                       .type = CommandType::Hold,
                                       .entities = {Contact}})
                 .ok);
    Match.run(30);

    const PlayerObservation Observation = Match.observe(PlayerId::One);
    const AIInfluenceMap FirstMap{Observation};
    const AIInfluenceMap ReplayMap{Observation};
    TestTrue(TEXT("Equivalent Unreal-side influence maps hash identically"),
             FirstMap.hash() != 0 && FirstMap.hash() == ReplayMap.hash());
    TestTrue(TEXT("Known map geometry remains unreachable in the tactical field"),
             !FirstMap.cell_at(world(760, 400)).navigable &&
                 FirstMap.cell_at(world(760, 400)).travel_cost == kAIUnreachableTravelCost);

    const CommanderPlan Plan = CommanderAI{PlayerId::One}.plan(Observation);
    const auto Tactical =
        std::ranges::find(Plan.decisions, AIDecisionLayer::Tactical, &AIPlannedDecision::layer);
    TestTrue(TEXT("The Unreal module produces a tactical decision from the shared C++ planner"),
             Tactical != Plan.decisions.end());
    TestTrue(TEXT("Every Unreal-side tactical candidate retains influence evidence"),
             Tactical != Plan.decisions.end() &&
                 std::ranges::all_of(Tactical->candidates, [](const AICandidateScore &Candidate)
                 {
                     return Candidate.influence_map_hash != 0 && Candidate.influence_sample.has_value();
                 }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreOwnedCommanderTest, "Ashen.Core.CoreOwnedCommander",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCoreOwnedCommanderTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    SimulationConfig Config{};
    Config.commander_players = {true, true};
    Simulation First{Config};
    Simulation Second{Config};

    First.step();
    Second.step();
    TestEqual(TEXT("Core commanders queue rather than immediately mutate"),
              static_cast<int32>(First.entities().size()), 10);
    First.step();
    Second.step();
    TestEqual(TEXT("Both players execute a legal queued construction opening"),
              static_cast<int32>(std::ranges::count_if(First.entities(), [](const Entity &EntityState)
              {
                  return EntityState.type == EntityType::Barracks && EntityState.under_construction;
              })), 2);
    TestTrue(TEXT("Commander commands retain observation provenance"),
             !First.command_trace().empty() &&
                 std::ranges::all_of(First.command_trace(), [](const CommandTraceEntry &Entry)
                 {
                     return Entry.source == CommandSource::CommanderAI && Entry.observation_hash != 0 &&
                         Entry.ai_decision_id != 0 && Entry.accepted &&
                         Entry.issued_tick <= Entry.applied_tick;
                 }));
    TestEqual(TEXT("Each applied opening command has one decision record"),
              static_cast<int32>(First.ai_decision_trace().size()),
              static_cast<int32>(First.command_trace().size()));
    TestTrue(TEXT("Opening decisions retain scores, winners, and accepted results"),
             std::ranges::all_of(First.ai_decision_trace(), [](const AIDecisionRecord &Record)
             {
                 return Record.id != 0 && Record.observation_hash != 0 &&
                     !Record.candidates.empty() && Record.selected_candidate < Record.candidates.size() &&
                     Record.command_sequence != 0 && Record.command_status == AICommandStatus::Accepted;
             }));

    constexpr Tick MaximumMatchTicks = 60'000;
    while (First.status() == MatchStatus::Playing && First.tick() < MaximumMatchTicks)
    {
        First.step();
        Second.step();
        if (First.tick() % 1'000 == 0)
        {
            TestTrue(TEXT("Equivalent bot matches remain deterministic during play"),
                     First.state_hash() == Second.state_hash());
        }
    }

    TestTrue(TEXT("Core-owned bot match finishes within its tick budget"),
             First.status() != MatchStatus::Playing);
    TestTrue(TEXT("Two core-owned bots finish without Unreal decision logic"), First.winner().has_value());
    TestEqual(TEXT("Equivalent bot matches finish on the same tick"),
              static_cast<int64>(First.tick()), static_cast<int64>(Second.tick()));
    TestTrue(TEXT("Core-owned bot matches remain deterministic"), First.state_hash() == Second.state_hash());
    TestTrue(TEXT("Decision traces remain deterministic"),
             First.ai_decision_trace() == Second.ai_decision_trace());
    std::array<bool, 3> ObservedLayers{};
    for (const auto &Record : First.ai_decision_trace())
    {
        ObservedLayers[static_cast<std::size_t>(Record.layer)] = true;
        const auto CommandEntry = std::ranges::find(First.command_trace(), Record.id,
                                                    &CommandTraceEntry::ai_decision_id);
        TestTrue(TEXT("Every completed AI decision links to its authoritative command result"),
                 Record.command_status != AICommandStatus::Queued &&
                     CommandEntry != First.command_trace().end() &&
                     CommandEntry->command.sequence == Record.command_sequence);
    }
    TestTrue(TEXT("Full bot play exercises strategic, tactical, and micro layers"),
             std::ranges::all_of(ObservedLayers, [](const bool Observed) { return Observed; }));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreFactionDoctrinesTest, "Ashen.Core.FactionDoctrines",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCoreFactionDoctrinesTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    const AIDoctrineProfile Compact = ai_doctrine_profile(FactionId::Compact, 91, PlayerId::One);
    const AIDoctrineProfile Ascendancy = ai_doctrine_profile(FactionId::Ascendancy, 91, PlayerId::One);
    const AIDoctrineProfile Concord = ai_doctrine_profile(FactionId::Concord, 91, PlayerId::One);
    TestTrue(TEXT("Compact remains the industrial preservation doctrine"),
             Compact.economy_weight_basis_points > Ascendancy.economy_weight_basis_points &&
                 Compact.preservation_weight_basis_points > Ascendancy.preservation_weight_basis_points);
    TestTrue(TEXT("Ascendancy remains the attrition and dread doctrine"),
             Ascendancy.aggression_weight_basis_points > Compact.aggression_weight_basis_points &&
                 Ascendancy.dread_exploitation_weight_basis_points >
                     Concord.dread_exploitation_weight_basis_points);
    TestTrue(TEXT("Concord remains the warded objective doctrine"),
             Concord.objective_weight_basis_points > Compact.objective_weight_basis_points &&
                 Concord.ward_affinity_weight_basis_points > Compact.ward_affinity_weight_basis_points);

    for (const FactionId Faction : {FactionId::Compact, FactionId::Ascendancy, FactionId::Concord})
    {
        SimulationConfig Config{};
        Config.player_one_faction = Faction;
        Config.player_two_faction = FactionId::Compact;
        Config.seed_starting_forces = false;
        Config.starting_ore = {1'000, 1'000};
        Config.map_size = world(1'600, 800);
        Config.navigation_obstacles.clear();
        Config.match_seed = 91;
        Simulation Match{Config};
        static_cast<void>(Match.spawn_entity(PlayerId::One, EntityType::Command, world(180, 400)));
        const EntityId Defender =
            Match.spawn_entity(PlayerId::One, EntityType::Vanguard, world(360, 400));
        static_cast<void>(Match.spawn_entity(PlayerId::Two, EntityType::Command, world(1'420, 400)));
        const EntityId First =
            Match.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(540, 350));
        const EntityId Second =
            Match.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(540, 450));
        TestTrue(TEXT("Fixture defender holds"),
                 Match.execute_now(Command{.player = PlayerId::One,
                                           .type = CommandType::Hold,
                                           .entities = {Defender}})
                     .ok);
        TestTrue(TEXT("Fixture attackers hold"),
                 Match.execute_now(Command{.player = PlayerId::Two,
                                           .type = CommandType::Hold,
                                           .entities = {First, Second}})
                     .ok);
        Match.run(kTacticalDecisionPhase);

        const CommanderPlan Plan = CommanderAI{PlayerId::One}.plan(Match.observe(PlayerId::One));
        const auto Tactical = std::ranges::find(Plan.decisions, AIDecisionLayer::Tactical,
                                                &AIPlannedDecision::layer);
        TestTrue(TEXT("Each faction resolves the loss-tolerance fixture"),
                 Tactical != Plan.decisions.end());
        if (Tactical == Plan.decisions.end())
        {
            continue;
        }
        const AIAction Expected =
            Faction == FactionId::Ascendancy ? AIAction::EngageForce : AIAction::Retreat;
        TestTrue(TEXT("Faction behavior is visible in the selected command"),
                 Tactical->selected_action == Expected);
        const AIDoctrineProfile ExpectedDoctrine =
            ai_doctrine_profile(Faction, Config.match_seed, PlayerId::One);
        TestTrue(TEXT("Unreal decisions retain their deterministic doctrine fingerprint"),
                 Tactical->doctrine_faction == Faction &&
                     Tactical->temperament == ExpectedDoctrine.temperament &&
                     Tactical->doctrine_hash == ai_doctrine_hash(ExpectedDoctrine));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenWorldVisualFoundationTest, "Ashen.World.VisualFoundation",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenWorldVisualFoundationTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);

    AAshenArena *Arena = GetMutableDefault<AAshenArena>();
    TestNotNull(TEXT("Arena class default object is available"), Arena);
    if (Arena == nullptr)
    {
        return false;
    }

    const UProceduralMeshComponent *Terrain =
        Cast<UProceduralMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("TerrainGeometry")));
    TestNotNull(TEXT("Arena owns its sculpted terrain component"), Terrain);
    TestTrue(TEXT("Visual terrain cannot intercept deterministic RTS input"),
             Terrain != nullptr && Terrain->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

    const UStaticMeshComponent *InteractionGround =
        Cast<UStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("Ground")));
    TestNotNull(TEXT("Arena retains an invisible interaction plane"), InteractionGround);
    TestTrue(TEXT("Interaction plane blocks world traces"),
             InteractionGround != nullptr &&
                 InteractionGround->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
    TestFalse(TEXT("Interaction plane never renders over authored terrain"),
              InteractionGround != nullptr && InteractionGround->IsVisible());

    const UInstancedStaticMeshComponent *Mountain =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("MountainRocks")));
    const UInstancedStaticMeshComponent *MountainSecondary =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("MountainRocksSecondary")));
    const UInstancedStaticMeshComponent *MountainTertiary =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("MountainRocksTertiary")));
    const UInstancedStaticMeshComponent *Mines =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("MineMouths")));
    const UInstancedStaticMeshComponent *Gravewood =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("ForestRoots")));
    const UInstancedStaticMeshComponent *GravewoodTreesA =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("GravewoodTreesA")));
    const UInstancedStaticMeshComponent *GravewoodTreesB =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("GravewoodTreesB")));
    const UInstancedStaticMeshComponent *GravewoodStumps =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("GravewoodStumps")));
    const UInstancedStaticMeshComponent *GravewoodRootsA =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("GravewoodRootsA")));
    const UInstancedStaticMeshComponent *GravewoodRootsB =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("GravewoodRootsB")));
    UProceduralMeshComponent *RoadSurface =
        Cast<UProceduralMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("RoadSurface")));
    UProceduralMeshComponent *RoadStoneSurface =
        Cast<UProceduralMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("RoadStoneSurface")));
    UProceduralMeshComponent *RoadRutSurface =
        Cast<UProceduralMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("RoadRutSurface")));
    const UInstancedStaticMeshComponent *BridgeTimbers =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("BridgeTimbers")));
    const UInstancedStaticMeshComponent *BridgeIron =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("BridgeIron")));
    const UInstancedStaticMeshComponent *Wayshrine =
        Cast<UInstancedStaticMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("MythicArches")));
    UProceduralMeshComponent *RiverSurface =
        Cast<UProceduralMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("RiverSurface")));
    UProceduralMeshComponent *RiverShoreSurface =
        Cast<UProceduralMeshComponent>(Arena->GetDefaultSubobjectByName(TEXT("RiverShoreSurface")));
    const FProcMeshSection *RoadMeshSection =
        RoadSurface != nullptr ? RoadSurface->GetProcMeshSection(0) : nullptr;
    const FProcMeshSection *RoadStoneMeshSection =
        RoadStoneSurface != nullptr ? RoadStoneSurface->GetProcMeshSection(0) : nullptr;
    const FProcMeshSection *RiverShoreMeshSection =
        RiverShoreSurface != nullptr ? RiverShoreSurface->GetProcMeshSection(0) : nullptr;
    const auto FacesRtsCamera = [](const FProcMeshSection *Section)
    {
        if (Section == nullptr || Section->ProcIndexBuffer.Num() < 3)
        {
            return false;
        }
        const FVector A = Section->ProcVertexBuffer[Section->ProcIndexBuffer[0]].Position;
        const FVector B = Section->ProcVertexBuffer[Section->ProcIndexBuffer[1]].Position;
        const FVector C = Section->ProcVertexBuffer[Section->ProcIndexBuffer[2]].Position;
        return FVector::CrossProduct(B - A, C - A).Z < -UE_KINDA_SMALL_NUMBER;
    };
    const int32 MountainInstanceCount =
        (Mountain != nullptr ? Mountain->GetInstanceCount() : 0) +
        (MountainSecondary != nullptr ? MountainSecondary->GetInstanceCount() : 0) +
        (MountainTertiary != nullptr ? MountainTertiary->GetInstanceCount() : 0);
    TestTrue(TEXT("Northwest massif owns a substantial three-mesh rock silhouette"),
             MountainInstanceCount >= 24 && Mountain != nullptr && Mountain->GetInstanceCount() > 0 &&
                 MountainSecondary != nullptr && MountainSecondary->GetInstanceCount() > 0 &&
                 MountainTertiary != nullptr && MountainTertiary->GetInstanceCount() > 0);
    TestTrue(TEXT("Production art never supplies deterministic collision"),
             Mountain != nullptr && Mountain->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                 MountainSecondary != nullptr &&
                 MountainSecondary->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                 MountainTertiary != nullptr &&
                 MountainTertiary->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestEqual(TEXT("The concealed route owns two mine entrances"), Mines != nullptr ? Mines->GetInstanceCount() : 0, 2);
    TestTrue(TEXT("Gravewood owns a dedicated root layer"), Gravewood != nullptr && Gravewood->GetInstanceCount() > 0);
    const int32 GravewoodTreeCount =
        (GravewoodTreesA != nullptr ? GravewoodTreesA->GetInstanceCount() : 0) +
        (GravewoodTreesB != nullptr ? GravewoodTreesB->GetInstanceCount() : 0);
    const int32 GravewoodRootCount =
        (GravewoodRootsA != nullptr ? GravewoodRootsA->GetInstanceCount() : 0) +
        (GravewoodRootsB != nullptr ? GravewoodRootsB->GetInstanceCount() : 0);
    TestEqual(TEXT("Gravewood limits expensive dead-tree silhouettes to five anchors"), GravewoodTreeCount, 5);
    TestEqual(TEXT("Gravewood owns six authored stump landmarks"),
              GravewoodStumps != nullptr ? GravewoodStumps->GetInstanceCount() : 0, 6);
    TestEqual(TEXT("Gravewood root detail stays inside the profiled instance budget"), GravewoodRootCount, 42);
    TestTrue(TEXT("Gravewood production art remains presentation-only"),
             GravewoodTreesA != nullptr &&
                 GravewoodTreesA->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                 GravewoodTreesB != nullptr &&
                 GravewoodTreesB->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                 GravewoodStumps != nullptr &&
                 GravewoodStumps->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                 GravewoodRootsA != nullptr &&
                 GravewoodRootsA->GetCollisionEnabled() == ECollisionEnabled::NoCollision &&
                 GravewoodRootsB != nullptr &&
                 GravewoodRootsB->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Terrain-following road ribbons keep all three routes continuous"),
             RoadSurface != nullptr && RoadSurface->GetNumSections() == 1 &&
                 RoadSurface->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Road ribbon owns renderable vertices and triangles"),
             RoadMeshSection != nullptr && RoadMeshSection->ProcVertexBuffer.Num() > 0 &&
                 RoadMeshSection->ProcIndexBuffer.Num() > 0);
    TestTrue(TEXT("Road ribbon winding faces the RTS camera"), FacesRtsCamera(RoadMeshSection));
    TestTrue(TEXT("Only the direct causeway receives a continuous stone center"),
             RoadStoneSurface != nullptr && RoadStoneSurface->GetNumSections() == 1);
    TestTrue(TEXT("Stone causeway owns renderable vertices and triangles"),
             RoadStoneMeshSection != nullptr && RoadStoneMeshSection->ProcVertexBuffer.Num() > 0 &&
                 RoadStoneMeshSection->ProcIndexBuffer.Num() > 0);
    TestTrue(TEXT("Flank wheel ruts remain continuous and presentation-only"),
             RoadRutSurface != nullptr && RoadRutSurface->GetNumSections() == 1 &&
                 RoadRutSurface->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestNull(TEXT("Legacy slab roadbeds stay removed"),
             Arena->GetDefaultSubobjectByName(TEXT("Roadbed")));
    TestEqual(TEXT("Two flank bridges own complete plank, curb, and bridgehead kits"),
              BridgeTimbers != nullptr ? BridgeTimbers->GetInstanceCount() : 0, 38);
    TestEqual(TEXT("Tall bridge iron that read as deck spikes stays removed"),
              BridgeIron != nullptr ? BridgeIron->GetInstanceCount() : 0, 0);
    TestEqual(TEXT("The off-lane Drowned Wayshrine uses two upright ruin fragments"),
              Wayshrine != nullptr ? Wayshrine->GetInstanceCount() : 0, 2);
    TestNotNull(TEXT("River owns one continuous curved surface"), RiverSurface);
    TestTrue(TEXT("River surface cannot intercept deterministic RTS input"),
             RiverSurface != nullptr &&
                 RiverSurface->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestEqual(TEXT("River surface removes overlapping segment seams"),
              RiverSurface != nullptr ? RiverSurface->GetNumSections() : 0, 1);
    TestTrue(TEXT("Continuous wet shorelines stop before every crossing"),
             RiverShoreSurface != nullptr && RiverShoreSurface->GetNumSections() == 1 &&
                 RiverShoreSurface->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Wet shoreline owns renderable vertices and triangles"),
             RiverShoreMeshSection != nullptr && RiverShoreMeshSection->ProcVertexBuffer.Num() > 0 &&
                 RiverShoreMeshSection->ProcIndexBuffer.Num() > 0);
    TestTrue(TEXT("Wet shoreline winding faces the RTS camera"),
             FacesRtsCamera(RiverShoreMeshSection));
    TestNull(TEXT("Legacy overlapping water segments stay removed"),
             Arena->GetDefaultSubobjectByName(TEXT("WaterSegment_00")));
    TestNull(TEXT("Legacy perimeter monoliths stay removed"),
             Arena->GetDefaultSubobjectByName(TEXT("BoundaryMonoliths")));
    TestEqual(TEXT("Expanded battlefield width remains authoritative"), Ashen::WorldLayout::Width, 4'800.0f);
    TestEqual(TEXT("Expanded battlefield height remains authoritative"), Ashen::WorldLayout::Height, 2'800.0f);

    const UMaterialInterface *Surface =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_VowfallSurface.M_VowfallSurface"));
    const UMaterialInterface *Water =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Art/Materials/M_VowfallWater.M_VowfallWater"));
    TestNotNull(TEXT("Painterly surface master material is available to the project"), Surface);
    TestNotNull(TEXT("Water master material is available to the project"), Water);

    if (Surface != nullptr)
    {
        TArray<FMaterialParameterInfo> TextureParameters;
        TArray<FGuid> TextureIds;
        Surface->GetAllTextureParameterInfo(TextureParameters, TextureIds);
        const auto HasTextureParameter = [&TextureParameters](const FName Name) {
            return TextureParameters.ContainsByPredicate(
                [Name](const FMaterialParameterInfo &Parameter) { return Parameter.Name == Name; });
        };
        TestTrue(TEXT("Surface material accepts production albedo textures"),
                 HasTextureParameter(TEXT("AlbedoTexture")));
        TestTrue(TEXT("Surface material accepts production normal textures"),
                 HasTextureParameter(TEXT("NormalTexture")));
        TestTrue(TEXT("Surface material accepts packed AO and roughness textures"),
                 HasTextureParameter(TEXT("PackedTexture")));

        TArray<FMaterialParameterInfo> VectorParameters;
        TArray<FGuid> VectorIds;
        Surface->GetAllVectorParameterInfo(VectorParameters, VectorIds);
        TestTrue(TEXT("Production textures retain per-surface art-direction tint"),
                 VectorParameters.ContainsByPredicate(
                     [](const FMaterialParameterInfo &Parameter)
                     {
                         return Parameter.Name == TEXT("TextureTint");
                     }));

        TArray<FMaterialParameterInfo> ScalarParameters;
        TArray<FGuid> ScalarIds;
        Surface->GetAllScalarParameterInfo(ScalarParameters, ScalarIds);
        const auto HasScalarParameter = [&ScalarParameters](const FName Name) {
            return ScalarParameters.ContainsByPredicate(
                [Name](const FMaterialParameterInfo &Parameter) { return Parameter.Name == Name; });
        };
        TestTrue(TEXT("Texture blending remains an explicit material control"),
                 HasScalarParameter(TEXT("TextureBlend")));
        TestTrue(TEXT("Texture tiling remains an explicit material control"),
                 HasScalarParameter(TEXT("TextureTiling")));
    }

    const UAshenEnvironmentKitSettings *KitSettings = GetDefault<UAshenEnvironmentKitSettings>();
    TestNotNull(TEXT("Environment-kit settings are registered"), KitSettings);
    TestTrue(TEXT("Licensed source content remains under the external boundary"),
             KitSettings != nullptr && KitSettings->ProductionContentRoot.StartsWith(TEXT("/Game/External/")));

    const TConstArrayView<Ashen::EnvironmentKit::FMeshSpec> MeshSpecs = Ashen::EnvironmentKit::MeshSpecs();
    TestEqual(TEXT("Every visual proxy category owns a semantic production slot"), MeshSpecs.Num(),
              static_cast<int32>(EAshenEnvironmentMeshSlot::Count));
    TSet<uint8> MeshSlots;
    UStaticMesh *FallbackCube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    for (const Ashen::EnvironmentKit::FMeshSpec &Spec : MeshSpecs)
    {
        MeshSlots.Add(static_cast<uint8>(Spec.Slot));
        TestTrue(FString::Printf(TEXT("%s has a canonical external path"), Spec.DisplayName),
                 Ashen::EnvironmentKit::ObjectPath(Spec).StartsWith(TEXT("/Game/External/")));
        TestTrue(FString::Printf(TEXT("%s has a source-controlled Vowfall path"), Spec.DisplayName),
                 Ashen::EnvironmentKit::SourceObjectPath(Spec).StartsWith(
                     TEXT("/Game/Art/Environment/VowfallKit/")));
        TestNotNull(FString::Printf(TEXT("%s has an original Vowfall fallback mesh"), Spec.DisplayName),
                    Ashen::EnvironmentKit::FindSourceMesh(Spec.Slot));
        TestNotNull(FString::Printf(TEXT("%s retains a source-safe fallback"), Spec.DisplayName),
                    Ashen::EnvironmentKit::ResolveMesh(Spec.Slot, FallbackCube));
    }
    TestEqual(TEXT("Environment mesh slots are unique"), MeshSlots.Num(), MeshSpecs.Num());

    const TConstArrayView<Ashen::EnvironmentKit::FSurfaceSpec> SurfaceSpecs =
        Ashen::EnvironmentKit::SurfaceSpecs();
    TestEqual(TEXT("Every textured surface owns a canonical production slot"), SurfaceSpecs.Num(),
              static_cast<int32>(EAshenEnvironmentSurface::Count) - 1);
    TSet<uint8> SurfaceSlots;
    for (const Ashen::EnvironmentKit::FSurfaceSpec &Spec : SurfaceSpecs)
    {
        SurfaceSlots.Add(static_cast<uint8>(Spec.Slot));
        TestTrue(FString::Printf(TEXT("%s has a canonical albedo path"), Spec.DisplayName),
                 Ashen::EnvironmentKit::TextureObjectPath(Spec, TEXT("_BC")).StartsWith(TEXT("/Game/External/")));
    }
    TestEqual(TEXT("Environment surface slots are unique"), SurfaceSlots.Num(), SurfaceSpecs.Num());
    TestTrue(TEXT("Forest-floor variation reuses the acquired moor family"),
             Ashen::EnvironmentKit::SurfaceFallback(EAshenEnvironmentSurface::MoorPatch) ==
                 EAshenEnvironmentSurface::Moor);
    TestTrue(TEXT("Wet and structural stone reuse the normalized road-stone family"),
             Ashen::EnvironmentKit::SurfaceFallback(EAshenEnvironmentSurface::WetStone) ==
                     EAshenEnvironmentSurface::RoadStone &&
                 Ashen::EnvironmentKit::SurfaceFallback(EAshenEnvironmentSurface::MineDark) ==
                     EAshenEnvironmentSurface::RoadStone &&
                 Ashen::EnvironmentKit::SurfaceFallback(EAshenEnvironmentSurface::HumanStone) ==
                     EAshenEnvironmentSurface::RoadStone &&
                 Ashen::EnvironmentKit::SurfaceFallback(EAshenEnvironmentSurface::FoundationStone) ==
                     EAshenEnvironmentSurface::RoadStone);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAshenCoreCompetitiveSliceInUnrealTest, "Ashen.Core.CompetitiveVerticalSlice",
                                 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAshenCoreCompetitiveSliceInUnrealTest::RunTest(const FString &Parameters)
{
    static_cast<void>(Parameters);
    using namespace ashen::core;

    SimulationConfig Config{};
    Config.seed_starting_forces = false;
    Config.navigation_obstacles.clear();
    Simulation Match{Config};
    const EntityId Keep = Match.spawn_entity(PlayerId::One, EntityType::Command, world(200, 400));
    const EntityId Worker = Match.spawn_entity(PlayerId::One, EntityType::Worker, world(350, 400));
    const ResourceId Iron = Match.add_resource(world(350, 270), 1'200);

    Command Build{};
    Build.player = PlayerId::One;
    Build.type = CommandType::Build;
    Build.entities = {Worker};
    Build.target = world(475, 400);
    Build.building_type = EntityType::Barracks;
    TestTrue(TEXT("Worker accepts a valid barracks site"), Match.execute_now(Build).ok);
    Match.run(380);

    const Entity *Barracks = nullptr;
    for (const Entity &Candidate : Match.entities())
    {
        if (Candidate.owner == PlayerId::One && Candidate.type == EntityType::Barracks)
        {
            Barracks = &Candidate;
            break;
        }
    }
    TestNotNull(TEXT("Construction creates a barracks"), Barracks);
    TestTrue(TEXT("Worker completes the barracks"), Barracks != nullptr && !Barracks->under_construction);

    Command Gather{};
    Gather.player = PlayerId::One;
    Gather.type = CommandType::Gather;
    Gather.entities = {Worker};
    Gather.resource = Iron;
    TestTrue(TEXT("Builder returns to the opening economy"), Match.execute_now(Gather).ok);
    Match.run(1'600);

    Command Research{};
    Research.player = PlayerId::One;
    Research.type = CommandType::Research;
    Research.producer = Keep;
    Research.research = ResearchId::TierTwo;
    TestTrue(TEXT("Command keep begins the Black-Iron Age"), Match.execute_now(Research).ok);
    Match.run(research_definition(ResearchId::TierTwo).research_ticks);
    TestTrue(TEXT("Tier-two doctrine completes"), Match.has_research(PlayerId::One, ResearchId::TierTwo));

    const ControlPointId Relic = Match.add_control_point(world(700, 400));
    static_cast<void>(Relic);
    static_cast<void>(Match.spawn_entity(PlayerId::One, EntityType::Vanguard, world(700, 400)));
    Match.run(160);
    TestTrue(TEXT("A lone war band captures an uncontested relic"),
             !Match.control_points().empty() && Match.control_points().back().owner == PlayerId::One);

    Command Power{};
    Power.player = PlayerId::One;
    Power.type = CommandType::ActivatePower;
    TestTrue(TEXT("Faction power activates after sufficient economy"), Match.execute_now(Power).ok);
    TestTrue(TEXT("Faction power begins its cooldown"), Match.player(PlayerId::One).power_cooldown_ticks > 0);

    const EntityId HiddenEnemy = Match.spawn_entity(PlayerId::Two, EntityType::Vanguard, world(1'650, 930));
    TestFalse(TEXT("A distant enemy is concealed by fog"),
              Match.is_entity_visible_to(*Match.find_entity(HiddenEnemy), PlayerId::One));
    static_cast<void>(Match.spawn_entity(PlayerId::One, EntityType::Worker, world(1'520, 930)));
    TestTrue(TEXT("A scout reveals the distant enemy"),
             Match.is_entity_visible_to(*Match.find_entity(HiddenEnemy), PlayerId::One));
    return true;
}

#endif
