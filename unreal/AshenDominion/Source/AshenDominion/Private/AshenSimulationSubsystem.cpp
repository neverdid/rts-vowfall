#include "AshenSimulationSubsystem.h"

#include "AshenCheckpointSaveGame.h"
#include "AshenControlPointActor.h"
#include "AshenEntityActor.h"
#include "AshenResourceActor.h"
#include "ashen/core/Catalog.hpp"
#include "ashen/core/Replay.hpp"
#include "ashen/core/Simulation.hpp"
#include "ashen/core/SimulationEvent.hpp"
#include "ashen/core/Snapshot.hpp"

#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Stats/Stats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogAshenSimulation, Log, All);

class FAshenSimulationRuntime final
{
public:
    explicit FAshenSimulationRuntime(const ashen::core::SimulationConfig& Config)
        : Simulation(Config), ReplayRecorder(MakeUnique<ashen::core::ReplayRecorder>(Simulation))
    {
    }

    explicit FAshenSimulationRuntime(ashen::core::Simulation&& RestoredSimulation)
        : Simulation(std::move(RestoredSimulation)), ReplayRecorder(MakeUnique<ashen::core::ReplayRecorder>(Simulation))
    {
    }

    ashen::core::CommandResult ExecuteExternal(ashen::core::Command Command)
    {
        return ReplayRecorder->execute_now(Simulation, std::move(Command));
    }

    void ResetReplayRecorder()
    {
        ReplayRecorder = MakeUnique<ashen::core::ReplayRecorder>(Simulation);
    }

    ashen::core::Simulation Simulation;
    TUniquePtr<ashen::core::ReplayRecorder> ReplayRecorder;
};

namespace
{
constexpr float FixedStepSeconds = 1.0f / static_cast<float>(ashen::core::kTicksPerSecond);
constexpr int32 MaxCatchUpSteps = 8;
constexpr ashen::core::Tick ReplayCheckpointInterval = 600;
const FString CheckpointSlotName(TEXT("VowfallQuickCheckpoint"));
constexpr int32 CheckpointUserIndex = 0;

bool ContainsInvalidId(const TArray<int32>& EntityIds)
{
    return EntityIds.ContainsByPredicate([](const int32 Id)
    {
        return Id <= 0;
    });
}

EAshenEntityArchetype ToArchetype(const ashen::core::EntityType Type)
{
    using ashen::core::EntityType;
    switch (Type)
    {
    case EntityType::Worker:
        return EAshenEntityArchetype::Worker;
    case EntityType::Vanguard:
        return EAshenEntityArchetype::Vanguard;
    case EntityType::Skirmisher:
        return EAshenEntityArchetype::Skirmisher;
    case EntityType::Command:
        return EAshenEntityArchetype::Command;
    case EntityType::Barracks:
        return EAshenEntityArchetype::Barracks;
    case EntityType::Turret:
        return EAshenEntityArchetype::Turret;
    }
    return EAshenEntityArchetype::Worker;
}

ashen::core::EntityType ToEntityType(const EAshenEntityArchetype Archetype)
{
    using ashen::core::EntityType;
    switch (Archetype)
    {
    case EAshenEntityArchetype::Worker:
        return EntityType::Worker;
    case EAshenEntityArchetype::Vanguard:
        return EntityType::Vanguard;
    case EAshenEntityArchetype::Skirmisher:
        return EntityType::Skirmisher;
    case EAshenEntityArchetype::Command:
        return EntityType::Command;
    case EAshenEntityArchetype::Barracks:
        return EntityType::Barracks;
    case EAshenEntityArchetype::Turret:
        return EntityType::Turret;
    }
    return EntityType::Worker;
}

ashen::core::ResearchId ToResearch(const EAshenResearch Research)
{
    return static_cast<ashen::core::ResearchId>(Research);
}

ashen::core::UnitStance ToStance(const EAshenStance Stance)
{
    return static_cast<ashen::core::UnitStance>(Stance);
}

EAshenStance ToStance(const ashen::core::UnitStance Stance)
{
    return static_cast<EAshenStance>(Stance);
}

EAshenVisibility ToVisibility(const ashen::core::VisibilityState Visibility)
{
    return static_cast<EAshenVisibility>(Visibility);
}

EAshenFaction ToFaction(const ashen::core::FactionId Faction)
{
    using ashen::core::FactionId;
    switch (Faction)
    {
    case FactionId::Compact:
        return EAshenFaction::Compact;
    case FactionId::Ascendancy:
        return EAshenFaction::Ascendancy;
    case FactionId::Concord:
        return EAshenFaction::Concord;
    }
    return EAshenFaction::None;
}

EAshenSimulationEventType ToSimulationEventType(const ashen::core::SimulationEventType Type)
{
    using ashen::core::SimulationEventType;
    switch (Type)
    {
    case SimulationEventType::EntitySpawned:
        return EAshenSimulationEventType::EntitySpawned;
    case SimulationEventType::EntityDestroyed:
        return EAshenSimulationEventType::EntityDestroyed;
    case SimulationEventType::UnitDamaged:
        return EAshenSimulationEventType::UnitDamaged;
    case SimulationEventType::UnitWounded:
        return EAshenSimulationEventType::UnitWounded;
    case SimulationEventType::UnitKilled:
        return EAshenSimulationEventType::UnitKilled;
    case SimulationEventType::UnitRecovered:
        return EAshenSimulationEventType::UnitRecovered;
    case SimulationEventType::FormationCreated:
        return EAshenSimulationEventType::FormationCreated;
    case SimulationEventType::FormationBroken:
        return EAshenSimulationEventType::FormationBroken;
    case SimulationEventType::ResolveThresholdChanged:
        return EAshenSimulationEventType::ResolveThresholdChanged;
    case SimulationEventType::SupplyConnected:
        return EAshenSimulationEventType::SupplyConnected;
    case SimulationEventType::SupplyDisconnected:
        return EAshenSimulationEventType::SupplyDisconnected;
    case SimulationEventType::VowMade:
        return EAshenSimulationEventType::VowMade;
    case SimulationEventType::VowKept:
        return EAshenSimulationEventType::VowKept;
    case SimulationEventType::VowAmended:
        return EAshenSimulationEventType::VowAmended;
    case SimulationEventType::VowBroken:
        return EAshenSimulationEventType::VowBroken;
    case SimulationEventType::TransformationStarted:
        return EAshenSimulationEventType::TransformationStarted;
    case SimulationEventType::TransformationCompleted:
        return EAshenSimulationEventType::TransformationCompleted;
    case SimulationEventType::TestimonyDiscovered:
        return EAshenSimulationEventType::TestimonyDiscovered;
    case SimulationEventType::ObjectiveContested:
        return EAshenSimulationEventType::ObjectiveContested;
    case SimulationEventType::ObjectiveCaptured:
        return EAshenSimulationEventType::ObjectiveCaptured;
    case SimulationEventType::ProjectileLaunched:
        return EAshenSimulationEventType::ProjectileLaunched;
    case SimulationEventType::AbilityStarted:
        return EAshenSimulationEventType::AbilityStarted;
    case SimulationEventType::AbilityInterrupted:
        return EAshenSimulationEventType::AbilityInterrupted;
    case SimulationEventType::MissionObjectiveChanged:
        return EAshenSimulationEventType::MissionObjectiveChanged;
    }
    return EAshenSimulationEventType::EntitySpawned;
}

ashen::core::AIDifficulty ToCoreDifficulty(const EAshenAIDifficulty Difficulty)
{
    using ashen::core::AIDifficulty;
    switch (Difficulty)
    {
    case EAshenAIDifficulty::Story:
        return AIDifficulty::Story;
    case EAshenAIDifficulty::Standard:
        return AIDifficulty::Standard;
    case EAshenAIDifficulty::Veteran:
        return AIDifficulty::Veteran;
    case EAshenAIDifficulty::Competitive:
        return AIDifficulty::Competitive;
    }
    return AIDifficulty::Standard;
}

EAshenAIDifficulty ToAshenDifficulty(const ashen::core::AIDifficulty Difficulty)
{
    using ashen::core::AIDifficulty;
    switch (Difficulty)
    {
    case AIDifficulty::Story:
        return EAshenAIDifficulty::Story;
    case AIDifficulty::Standard:
        return EAshenAIDifficulty::Standard;
    case AIDifficulty::Veteran:
        return EAshenAIDifficulty::Veteran;
    case AIDifficulty::Competitive:
        return EAshenAIDifficulty::Competitive;
    }
    return EAshenAIDifficulty::Standard;
}

FString CoreText(const std::string_view Text)
{
    return FString(UTF8_TO_TCHAR(Text.data()));
}

ashen::core::Vec2 ToCorePosition(const FVector& WorldPosition)
{
    return {
        static_cast<int32>(std::lround(WorldPosition.X / UAshenSimulationSubsystem::RenderScale *
                                      ashen::core::kWorldScale)),
        static_cast<int32>(std::lround(WorldPosition.Y / UAshenSimulationSubsystem::RenderScale *
                                      ashen::core::kWorldScale)),
    };
}
}

void UAshenSimulationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Accumulator = 0.0f;
    bCheckpointAvailable = UGameplayStatics::DoesSaveGameExist(CheckpointSlotName, CheckpointUserIndex);
}

void UAshenSimulationSubsystem::Deinitialize()
{
    delete Runtime;
    Runtime = nullptr;
    EntityActors.Reset();
    ResourceActors.Reset();
    ControlPointActors.Reset();
    KnownControlPointOwners.Reset();
    KnownControlPointInfluence.Reset();
    Super::Deinitialize();
}

void UAshenSimulationSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
    Super::OnWorldBeginPlay(InWorld);
    if (InWorld.IsGameWorld())
    {
        StartMatch();
    }
}

void UAshenSimulationSubsystem::Tick(const float DeltaTime)
{
    if (Runtime == nullptr || !bGameplayEnabled)
    {
        return;
    }

    Accumulator = FMath::Min(Accumulator + DeltaTime, FixedStepSeconds * MaxCatchUpSteps);
    int32 Steps = 0;
    while (Accumulator >= FixedStepSeconds && Steps < MaxCatchUpSteps)
    {
        const ashen::core::Tick PreviousTick = Runtime->Simulation.tick();
        Runtime->Simulation.step();
        if (Runtime->Simulation.tick() != PreviousTick &&
            Runtime->Simulation.tick() % ReplayCheckpointInterval == 0)
        {
            Runtime->ReplayRecorder->capture_checkpoint(Runtime->Simulation);
        }
        Accumulator -= FixedStepSeconds;
        ++Steps;
    }

    if (Steps > 0)
    {
        SyncWorldActors();
    }
}

TStatId UAshenSimulationSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UAshenSimulationSubsystem, STATGROUP_Tickables);
}

bool UAshenSimulationSubsystem::IssueMove(const TArray<int32>& EntityIds, const FVector& WorldTarget,
                                          const bool bQueue)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds))
    {
        return StoreCommandResult(false, TEXT("Select a valid war band before issuing movement."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Move;
    Command.target = ToCorePosition(WorldTarget);
    Command.queue = bQueue;
    Command.entities.reserve(static_cast<size_t>(EntityIds.Num()));
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueAttack(const TArray<int32>& EntityIds, const int32 TargetEntityId,
                                            const bool bQueue)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds) || TargetEntityId <= 0)
    {
        return StoreCommandResult(false, TEXT("Choose a visible enemy and a valid war band."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Attack;
    Command.target_entity = ashen::core::EntityId{static_cast<uint32>(TargetEntityId)};
    Command.queue = bQueue;
    Command.entities.reserve(static_cast<size_t>(EntityIds.Num()));
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueAttackMove(const TArray<int32>& EntityIds, const FVector& WorldTarget,
                                                const bool bQueue)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds))
    {
        return StoreCommandResult(false, TEXT("Select a valid war band before advancing."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::AttackMove;
    Command.target = ToCorePosition(WorldTarget);
    Command.queue = bQueue;
    Command.entities.reserve(static_cast<size_t>(EntityIds.Num()));
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueGather(const TArray<int32>& EntityIds, const int32 ResourceId,
                                            const bool bQueue)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds) || ResourceId <= 0)
    {
        return StoreCommandResult(false, TEXT("Select workers and a cursed-iron field."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Gather;
    Command.resource = ashen::core::ResourceId{static_cast<uint32>(ResourceId)};
    Command.queue = bQueue;
    Command.entities.reserve(static_cast<size_t>(EntityIds.Num()));
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssuePatrol(const TArray<int32>& EntityIds, const FVector& WorldTarget,
                                            const bool bQueue)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds))
    {
        return StoreCommandResult(false, TEXT("Select a valid war band before setting a patrol."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Patrol;
    Command.target = ToCorePosition(WorldTarget);
    Command.queue = bQueue;
    Command.entities.reserve(static_cast<size_t>(EntityIds.Num()));
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueStop(const TArray<int32>& EntityIds)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds))
    {
        return StoreCommandResult(false, TEXT("Select a valid war band before ordering a stop."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Stop;
    Command.entities.reserve(static_cast<size_t>(EntityIds.Num()));
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueHold(const TArray<int32>& EntityIds, const bool bQueue)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds))
    {
        return StoreCommandResult(false, TEXT("Select a valid war band before holding ground."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Hold;
    Command.queue = bQueue;
    Command.entities.reserve(static_cast<size_t>(EntityIds.Num()));
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueSetRallyPoint(const int32 ProducerId, const FVector& WorldTarget)
{
    if (Runtime == nullptr || ProducerId <= 0)
    {
        return StoreCommandResult(false, TEXT("Select a completed production structure."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::SetRallyPoint;
    Command.producer = ashen::core::EntityId{static_cast<uint32>(ProducerId)};
    Command.target = ToCorePosition(WorldTarget);
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? TEXT("Rally point set.") : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueTrain(const int32 ProducerId, const bool bSecondaryUnit)
{
    if (Runtime == nullptr || ProducerId <= 0)
    {
        return StoreCommandResult(false, TEXT("Select a completed production structure."));
    }

    const auto* Producer = Runtime->Simulation.find_entity(
        ashen::core::EntityId{static_cast<uint32>(ProducerId)});
    if (Producer == nullptr || Producer->owner != ashen::core::PlayerId::One)
    {
        return StoreCommandResult(false, TEXT("That structure does not answer to the Compact."));
    }

    ashen::core::EntityType UnitType = ashen::core::EntityType::Worker;
    if (Producer->type == ashen::core::EntityType::Barracks)
    {
        UnitType = bSecondaryUnit ? ashen::core::EntityType::Skirmisher : ashen::core::EntityType::Vanguard;
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Train;
    Command.producer = ashen::core::EntityId{static_cast<uint32>(ProducerId)};
    Command.train_type = UnitType;
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? FString() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueBuild(const int32 WorkerId, const EAshenEntityArchetype Building,
                                           const FVector& WorldTarget)
{
    if (Runtime == nullptr || WorkerId <= 0 ||
        (Building != EAshenEntityArchetype::Barracks && Building != EAshenEntityArchetype::Turret))
    {
        return StoreCommandResult(false, TEXT("Select one worker and a valid field structure."));
    }

    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Build;
    Command.entities = {ashen::core::EntityId{static_cast<uint32>(WorkerId)}};
    Command.target = ToCorePosition(WorldTarget);
    Command.building_type = ToEntityType(Building);
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? TEXT("Construction order accepted.") : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::CanPlaceBuilding(const EAshenEntityArchetype Building,
                                                 const FVector& WorldTarget) const
{
    if (Runtime == nullptr ||
        (Building != EAshenEntityArchetype::Barracks && Building != EAshenEntityArchetype::Turret))
    {
        return false;
    }
    return Runtime->Simulation.can_place_building(ToCorePosition(WorldTarget), ToEntityType(Building));
}

bool UAshenSimulationSubsystem::IssueResearch(const int32 ProducerId, const EAshenResearch Research)
{
    if (Runtime == nullptr || ProducerId <= 0)
    {
        return StoreCommandResult(false, TEXT("Select the structure responsible for that doctrine."));
    }
    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Research;
    Command.producer = ashen::core::EntityId{static_cast<uint32>(ProducerId)};
    Command.research = ToResearch(Research);
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? TEXT("Doctrine entered the archive queue.")
                                                   : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueActivatePower()
{
    if (Runtime == nullptr)
    {
        return StoreCommandResult(false, TEXT("The command network is unavailable."));
    }
    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::ActivatePower;
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? GetFactionPowerLabel() : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueRetreat(const TArray<int32>& EntityIds)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds))
    {
        return StoreCommandResult(false, TEXT("Select a war band before ordering retreat."));
    }
    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::Retreat;
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? TEXT("Retreat route set to the March Keep.")
                                                   : CoreText(Result.reason));
}

bool UAshenSimulationSubsystem::IssueSetStance(const TArray<int32>& EntityIds, const EAshenStance Stance)
{
    if (Runtime == nullptr || EntityIds.IsEmpty() || ContainsInvalidId(EntityIds))
    {
        return StoreCommandResult(false, TEXT("Select units before changing their stance."));
    }
    ashen::core::Command Command{};
    Command.player = ashen::core::PlayerId::One;
    Command.type = ashen::core::CommandType::SetStance;
    Command.stance = ToStance(Stance);
    for (const int32 Id : EntityIds)
    {
        Command.entities.push_back(ashen::core::EntityId{static_cast<uint32>(Id)});
    }
    const auto Result = Runtime->ExecuteExternal(std::move(Command));
    return StoreCommandResult(Result.ok, Result.ok ? TEXT("War-band stance updated.") : CoreText(Result.reason));
}

FAshenPlayerView UAshenSimulationSubsystem::GetPlayerView(const int32 PlayerIndex) const
{
    FAshenPlayerView View{};
    if (Runtime == nullptr)
    {
        return View;
    }

    const auto Player = PlayerIndex == 1 ? ashen::core::PlayerId::Two : ashen::core::PlayerId::One;
    const auto& State = Runtime->Simulation.player(Player);
    View.Faction = ToFaction(State.faction);
    View.Ore = State.ore;
    View.SupplyUsed = State.supply_used;
    View.SupplyCap = State.supply_cap;
    View.Resolve = State.resolve;
    View.TechTier = State.tech_tier;
    View.PowerCooldownSeconds = static_cast<float>(State.power_cooldown_ticks) / ashen::core::kTicksPerSecond;
    for (const auto& Point : Runtime->Simulation.control_points())
    {
        View.ControlledRelics += Point.owner == Player ? 1 : 0;
    }
    if (!State.research_queue.empty())
    {
        const auto& Task = State.research_queue.front();
        View.ActiveResearch = CoreText(ashen::core::to_string(Task.id));
        View.ResearchProgress = Task.total_ticks > 0
                                    ? 1.0f - static_cast<float>(Task.remaining_ticks) / Task.total_ticks
                                    : 1.0f;
    }
    return View;
}

FAshenEntityView UAshenSimulationSubsystem::GetEntityView(const int32 EntityId) const
{
    FAshenEntityView View{};
    if (Runtime == nullptr || EntityId <= 0)
    {
        return View;
    }
    const auto* Entity = Runtime->Simulation.find_entity(ashen::core::EntityId{static_cast<uint32>(EntityId)});
    if (Entity == nullptr)
    {
        return View;
    }
    if (Entity->owner != ashen::core::PlayerId::One &&
        !Runtime->Simulation.is_entity_visible_to(*Entity, ashen::core::PlayerId::One))
    {
        return View;
    }
    View.EntityId = EntityId;
    View.Faction = ToFaction(Entity->faction);
    View.Archetype = ToArchetype(Entity->type);
    View.Label = CoreText(ashen::core::entity_definition(Entity->faction, Entity->type).label);
    View.HitPoints = Entity->hit_points;
    View.MaxHitPoints = Entity->max_hit_points;
    View.Resolve = Entity->resolve;
    View.bUnderConstruction = Entity->under_construction;
    View.ConstructionProgress = Entity->under_construction && Entity->construction_total_ticks > 0
                                    ? static_cast<float>(Entity->construction_ticks) / Entity->construction_total_ticks
                                    : 1.0f;
    View.QueueCount = static_cast<int32>(Entity->production_queue.size());
    if (!Entity->production_queue.empty())
    {
        const auto& Task = Entity->production_queue.front();
        View.QueueProgress = Task.total_ticks > 0
                                 ? 1.0f - static_cast<float>(Task.remaining_ticks) / Task.total_ticks
                                 : 1.0f;
    }
    View.Stance = ToStance(Entity->stance);
    return View;
}

TArray<FAshenControlPointView> UAshenSimulationSubsystem::GetControlPointViews() const
{
    TArray<FAshenControlPointView> Views;
    if (Runtime == nullptr)
    {
        return Views;
    }
    Views.Reserve(static_cast<int32>(Runtime->Simulation.control_points().size()));
    for (const auto& Point : Runtime->Simulation.control_points())
    {
        FAshenControlPointView& View = Views.AddDefaulted_GetRef();
        View.ControlPointId = static_cast<int32>(Point.id.value);
        View.WorldPosition = ToWorldPosition(Point.position.x, Point.position.y);
        View.Visibility = ToVisibility(Runtime->Simulation.visibility_state_at(
            Point.position, ashen::core::PlayerId::One));
        const int32* KnownOwner = KnownControlPointOwners.Find(Point.id.value);
        const float* KnownInfluence = KnownControlPointInfluence.Find(Point.id.value);
        View.OwnerIndex = KnownOwner != nullptr ? *KnownOwner : -1;
        if (View.OwnerIndex == 0 || View.OwnerIndex == 1)
        {
            const auto Owner = View.OwnerIndex == 1 ? ashen::core::PlayerId::Two : ashen::core::PlayerId::One;
            View.OwnerFaction = ToFaction(Runtime->Simulation.player(Owner).faction);
        }
        View.Influence = KnownInfluence != nullptr ? *KnownInfluence : 0.0f;
    }
    return Views;
}

EAshenVisibility UAshenSimulationSubsystem::GetLocalVisibilityAt(const FVector& WorldPosition) const
{
    if (Runtime == nullptr)
    {
        return EAshenVisibility::Hidden;
    }
    return ToVisibility(Runtime->Simulation.visibility_state_at(
        ToCorePosition(WorldPosition), ashen::core::PlayerId::One));
}

FAshenVisibilityGridView UAshenSimulationSubsystem::GetLocalVisibilityGrid() const
{
    FAshenVisibilityGridView View{};
    if (Runtime == nullptr)
    {
        return View;
    }

    const ashen::core::VisibilityGrid& Grid = Runtime->Simulation.visibility(ashen::core::PlayerId::One);
    View.Columns = Grid.columns();
    View.Rows = Grid.rows();
    View.CellWorldSize = static_cast<float>(Grid.cell_size()) / ashen::core::kWorldScale * RenderScale;
    View.Cells.Reserve(static_cast<int32>(Grid.cells().size()));
    for (const ashen::core::VisibilityState State : Grid.cells())
    {
        View.Cells.Add(ToVisibility(State));
    }
    return View;
}

TArray<FAshenSimulationEventView> UAshenSimulationSubsystem::GetSimulationEventsSince(
    const int64 LastEventId) const
{
    TArray<FAshenSimulationEventView> Views;
    if (Runtime == nullptr)
    {
        return Views;
    }

    for (const ashen::core::SimulationEvent& Event : Runtime->Simulation.events())
    {
        if (static_cast<int64>(Event.id.value) <= LastEventId)
        {
            continue;
        }

        FAshenSimulationEventView& View = Views.AddDefaulted_GetRef();
        View.EventId = static_cast<int64>(Event.id.value);
        View.Tick = static_cast<int64>(Event.tick);
        View.Type = ToSimulationEventType(ashen::core::event_type(Event));
        std::visit([&View](const auto& Payload)
        {
            using PayloadType = std::decay_t<decltype(Payload)>;
            using namespace ashen::core;
            if constexpr (std::is_same_v<PayloadType, EntitySpawnedEvent> ||
                          std::is_same_v<PayloadType, EntityDestroyedEvent>)
            {
                View.EntityId = static_cast<int32>(Payload.entity.value);
                View.PlayerIndex = static_cast<int32>(Payload.owner);
                View.Faction = ToFaction(Payload.faction);
                View.ContentId = static_cast<int32>(Payload.archetype);
            }
            else if constexpr (std::is_same_v<PayloadType, UnitDamagedEvent>)
            {
                View.EntityId = static_cast<int32>(Payload.source.value);
                View.TargetEntityId = static_cast<int32>(Payload.target.value);
                View.Amount = Payload.amount;
            }
            else if constexpr (std::is_same_v<PayloadType, UnitWoundedEvent>)
            {
                View.EntityId = static_cast<int32>(Payload.entity.value);
                View.TargetEntityId = static_cast<int32>(Payload.source.value);
                View.Amount = Payload.remaining_hit_points;
            }
            else if constexpr (std::is_same_v<PayloadType, UnitKilledEvent>)
            {
                View.EntityId = static_cast<int32>(Payload.killer.value);
                View.TargetEntityId = static_cast<int32>(Payload.entity.value);
            }
            else if constexpr (std::is_same_v<PayloadType, UnitRecoveredEvent>)
            {
                View.EntityId = static_cast<int32>(Payload.entity.value);
                View.TargetEntityId = static_cast<int32>(Payload.recovery_source.value);
            }
            else if constexpr (std::is_same_v<PayloadType, FormationCreatedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.formation.value);
                View.PlayerIndex = static_cast<int32>(Payload.owner);
            }
            else if constexpr (std::is_same_v<PayloadType, FormationBrokenEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.formation.value);
            }
            else if constexpr (std::is_same_v<PayloadType, ResolveThresholdChangedEvent>)
            {
                View.EntityId = static_cast<int32>(Payload.entity.value);
                View.Amount = Payload.resolve;
            }
            else if constexpr (std::is_same_v<PayloadType, SupplyConnectedEvent> ||
                               std::is_same_v<PayloadType, SupplyDisconnectedEvent>)
            {
                View.EntityId = static_cast<int32>(Payload.entity.value);
            }
            else if constexpr (std::is_same_v<PayloadType, VowMadeEvent> ||
                               std::is_same_v<PayloadType, VowKeptEvent> ||
                               std::is_same_v<PayloadType, VowBrokenEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.vow.value);
                View.PlayerIndex = static_cast<int32>(Payload.maker);
            }
            else if constexpr (std::is_same_v<PayloadType, VowAmendedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.vow.value);
                View.PlayerIndex = static_cast<int32>(Payload.maker);
                View.TargetEntityId = static_cast<int32>(Payload.participating_affected_player);
                View.Amount = static_cast<int32>(Payload.revision);
            }
            else if constexpr (std::is_same_v<PayloadType, TransformationStartedEvent> ||
                               std::is_same_v<PayloadType, TransformationCompletedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.definition);
                View.EntityId = static_cast<int32>(Payload.entity.value);
                View.Amount = static_cast<int32>(Payload.transformation.value);
            }
            else if constexpr (std::is_same_v<PayloadType, TestimonyDiscoveredEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.testimony);
                View.PlayerIndex = static_cast<int32>(Payload.discoverer);
            }
            else if constexpr (std::is_same_v<PayloadType, ObjectiveContestedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.objective.value);
            }
            else if constexpr (std::is_same_v<PayloadType, ObjectiveCapturedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.objective.value);
                View.PlayerIndex = static_cast<int32>(Payload.owner);
                View.Amount = Payload.previous_owner.has_value()
                                  ? static_cast<int32>(*Payload.previous_owner)
                                  : -1;
            }
            else if constexpr (std::is_same_v<PayloadType, ProjectileLaunchedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.projectile);
                View.EntityId = static_cast<int32>(Payload.source.value);
                View.TargetEntityId = static_cast<int32>(Payload.target.value);
            }
            else if constexpr (std::is_same_v<PayloadType, AbilityStartedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.ability);
                View.PlayerIndex = static_cast<int32>(Payload.owner);
                View.EntityId = static_cast<int32>(Payload.source.value);
            }
            else if constexpr (std::is_same_v<PayloadType, AbilityInterruptedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.ability);
                View.EntityId = static_cast<int32>(Payload.source.value);
                View.TargetEntityId = static_cast<int32>(Payload.interrupter.value);
            }
            else if constexpr (std::is_same_v<PayloadType, MissionObjectiveChangedEvent>)
            {
                View.ContentId = static_cast<int32>(Payload.objective);
                View.PlayerIndex = static_cast<int32>(Payload.previous);
                View.Amount = static_cast<int32>(Payload.current);
            }
        }, Event.payload);
    }
    return Views;
}

TArray<FAshenResearchView> UAshenSimulationSubsystem::GetResearchViews(const int32 ProducerId) const
{
    TArray<FAshenResearchView> Views;
    if (Runtime == nullptr)
    {
        return Views;
    }
    using namespace ashen::core;
    const auto& Player = Runtime->Simulation.player(PlayerId::One);
    const Entity* Producer = ProducerId > 0
                                 ? Runtime->Simulation.find_entity(EntityId{static_cast<uint32>(ProducerId)})
                                 : nullptr;
    for (std::size_t Index = 0; Index < kResearchCount; ++Index)
    {
        const auto Research = static_cast<ResearchId>(Index);
        const auto Definition = research_definition(Research);
        if (Definition.faction.has_value() && *Definition.faction != Player.faction)
        {
            continue;
        }
        FAshenResearchView& View = Views.AddDefaulted_GetRef();
        View.Research = static_cast<EAshenResearch>(Research);
        View.Label = CoreText(Definition.label);
        View.Cost = Definition.cost;
        View.bCompleted = Player.researched[Index];
        const auto Task = std::ranges::find_if(Player.research_queue, [Research](const ResearchTask& Candidate)
        {
            return Candidate.id == Research;
        });
        View.bInProgress = Task != Player.research_queue.end();
        if (View.bInProgress)
        {
            View.Progress = Task->total_ticks > 0
                                ? 1.0f - static_cast<float>(Task->remaining_ticks) / Task->total_ticks
                                : 1.0f;
        }
        const bool bPrerequisite = !Definition.prerequisite.has_value() ||
                                   Player.researched[research_index(*Definition.prerequisite)];
        View.bAvailable = !View.bCompleted && !View.bInProgress && Player.research_queue.empty() &&
                          Player.ore >= Definition.cost && Producer != nullptr && !Producer->under_construction &&
                          Producer->owner == PlayerId::One && Producer->type == Definition.producer && bPrerequisite;
    }
    return Views;
}

int32 UAshenSimulationSubsystem::GetRuinTide() const
{
    return Runtime == nullptr ? 0 : Runtime->Simulation.ruin_tide();
}

FString UAshenSimulationSubsystem::GetFactionPowerLabel() const
{
    if (Runtime == nullptr)
    {
        return TEXT("FACTION DOCTRINE");
    }
    return CoreText(ashen::core::power_definition(Runtime->Simulation.player(ashen::core::PlayerId::One).faction).label);
}

FString UAshenSimulationSubsystem::GetObjectiveText() const
{
    if (Runtime == nullptr)
    {
        return {};
    }
    const std::optional<ashen::core::MissionObjectiveView> Objective =
        Runtime->Simulation.primary_mission_objective();
    if (!Objective.has_value())
    {
        return {};
    }
    const FString Label = CoreText(Objective->label);
    switch (Objective->status)
    {
    case ashen::core::MissionObjectiveStatus::Active:
        if (Objective->target_tick > Objective->current_tick)
        {
            const ashen::core::Tick RemainingTicks =
                Objective->target_tick - Objective->current_tick;
            const int64 RemainingSeconds = static_cast<int64>(
                (RemainingTicks + ashen::core::kTicksPerSecond - 1) /
                ashen::core::kTicksPerSecond);
            return FString::Printf(TEXT("%s  //  %llds remaining"),
                                   *Label, RemainingSeconds);
        }
        return Label;
    case ashen::core::MissionObjectiveStatus::Succeeded:
        return FString::Printf(TEXT("%s  //  SUCCEEDED"), *Label);
    case ashen::core::MissionObjectiveStatus::Failed:
        return FString::Printf(TEXT("%s  //  FAILED"), *Label);
    case ashen::core::MissionObjectiveStatus::Inactive:
        return Label;
    }
    return Label;
}

FString UAshenSimulationSubsystem::GetStoryChapterText() const
{
    if (!bStoryMode)
    {
        return {};
    }
    const ashen::core::StoryMissionDefinition* Mission =
        ashen::core::find_story_mission(ActiveStoryMission);
    return Mission == nullptr ? FString() : CoreText(ashen::core::campaign_act_label(Mission->act));
}

FString UAshenSimulationSubsystem::GetStoryMissionTitle() const
{
    if (!bStoryMode)
    {
        return {};
    }
    const ashen::core::StoryMissionDefinition* Mission =
        ashen::core::find_story_mission(ActiveStoryMission);
    return Mission == nullptr ? FString() : CoreText(Mission->title);
}

FString UAshenSimulationSubsystem::GetStoryProtagonist() const
{
    if (!bStoryMode)
    {
        return {};
    }
    const ashen::core::StoryMissionDefinition* Mission =
        ashen::core::find_story_mission(ActiveStoryMission);
    return Mission == nullptr ? FString() : CoreText(Mission->protagonist);
}

FString UAshenSimulationSubsystem::GetStoryVowText() const
{
    if (!bStoryMode)
    {
        return {};
    }
    const ashen::core::StoryMissionDefinition* Mission =
        ashen::core::find_story_mission(ActiveStoryMission);
    return Mission == nullptr ? FString() : CoreText(Mission->public_vow);
}

int64 UAshenSimulationSubsystem::GetSimulationTick() const
{
    return Runtime == nullptr ? 0 : static_cast<int64>(Runtime->Simulation.tick());
}

int32 UAshenSimulationSubsystem::GetEntityCount() const
{
    if (Runtime == nullptr)
    {
        return 0;
    }
    const int32 Owned = static_cast<int32>(std::ranges::count_if(
        Runtime->Simulation.entities(), [](const ashen::core::Entity& Entity)
        {
            return Entity.owner == ashen::core::PlayerId::One;
        }));
    return Owned + static_cast<int32>(Runtime->Simulation.visible_enemy_ids(ashen::core::PlayerId::One).size());
}

EAshenEntityArchetype UAshenSimulationSubsystem::GetEntityArchetype(const int32 EntityId) const
{
    if (Runtime != nullptr && EntityId > 0)
    {
        if (const auto* Entity = Runtime->Simulation.find_entity(
                ashen::core::EntityId{static_cast<uint32>(EntityId)}))
        {
            if (Entity->owner == ashen::core::PlayerId::One ||
                Runtime->Simulation.is_entity_visible_to(*Entity, ashen::core::PlayerId::One))
            {
                return ToArchetype(Entity->type);
            }
        }
    }
    return EAshenEntityArchetype::Worker;
}

FString UAshenSimulationSubsystem::GetEntityOrderLabel(const int32 EntityId) const
{
    if (Runtime == nullptr || EntityId <= 0)
    {
        return TEXT("IDLE");
    }
    const auto* Entity = Runtime->Simulation.find_entity(ashen::core::EntityId{static_cast<uint32>(EntityId)});
    if (Entity == nullptr || (Entity->owner != ashen::core::PlayerId::One &&
                              !Runtime->Simulation.is_entity_visible_to(*Entity, ashen::core::PlayerId::One)))
    {
        return TEXT("IDLE");
    }

    using ashen::core::OrderType;
    switch (Entity->order.type)
    {
    case OrderType::Move:
        return TEXT("MOVING");
    case OrderType::Attack:
        return TEXT("FOCUS FIRE");
    case OrderType::AttackMove:
        return TEXT("ADVANCING");
    case OrderType::Gather:
        return TEXT("GATHERING");
    case OrderType::Build:
        return TEXT("CONSTRUCTING");
    case OrderType::Patrol:
        return TEXT("PATROLLING");
    case OrderType::Hold:
        return TEXT("HOLDING");
    case OrderType::Idle:
        return TEXT("IDLE");
    }
    return TEXT("IDLE");
}

TArray<FVector> UAshenSimulationSubsystem::GetEntityRoute(const int32 EntityId) const
{
    TArray<FVector> Route;
    if (Runtime == nullptr || EntityId <= 0)
    {
        return Route;
    }
    const auto* Entity = Runtime->Simulation.find_entity(ashen::core::EntityId{static_cast<uint32>(EntityId)});
    if (Entity == nullptr || (Entity->owner != ashen::core::PlayerId::One &&
                              !Runtime->Simulation.is_entity_visible_to(*Entity, ashen::core::PlayerId::One)))
    {
        return Route;
    }

    Route.Reserve(static_cast<int32>(Entity->order.route.size() -
                                     FMath::Min(Entity->order.route_index, Entity->order.route.size())));
    for (size_t Index = Entity->order.route_index; Index < Entity->order.route.size(); ++Index)
    {
        Route.Add(ToWorldPosition(Entity->order.route[Index].x, Entity->order.route[Index].y));
    }
    return Route;
}

void UAshenSimulationSubsystem::SetGameplayEnabled(const bool bEnabled)
{
    const bool bWasEnabled = bGameplayEnabled;
    bGameplayEnabled = bEnabled;
    Accumulator = 0.0f;
    if (bEnabled && !bWasEnabled && Runtime != nullptr && Runtime->Simulation.tick() == 0)
    {
        PrimeOpeningEconomy();
    }
}

void UAshenSimulationSubsystem::RestartMatch()
{
    DestroyWorldActors();
    StartMatch();
}

bool UAshenSimulationSubsystem::SaveCheckpoint()
{
    if (Runtime == nullptr)
    {
        return StoreCommandResult(false, TEXT("CHECKPOINT FAILED // no active simulation"));
    }

    const std::vector<std::uint8_t> Bytes = ashen::core::save_snapshot_v1(Runtime->Simulation);
    if (Bytes.size() > static_cast<size_t>(MAX_int32))
    {
        return StoreCommandResult(false, TEXT("CHECKPOINT FAILED // snapshot exceeds Unreal save limits"));
    }

    UAshenCheckpointSaveGame* Checkpoint = Cast<UAshenCheckpointSaveGame>(
        UGameplayStatics::CreateSaveGameObject(UAshenCheckpointSaveGame::StaticClass()));
    if (Checkpoint == nullptr)
    {
        return StoreCommandResult(false, TEXT("CHECKPOINT FAILED // save adapter unavailable"));
    }

    Checkpoint->SnapshotSchemaVersion = ashen::core::kSnapshotSchemaVersion;
    Checkpoint->ContentDigest = ashen::core::current_content_digest();
    Checkpoint->PipelineDigest = ashen::core::current_pipeline_digest();
    Checkpoint->CheckpointTick = Runtime->Simulation.tick();
    Checkpoint->CheckpointStateHash = Runtime->Simulation.state_hash();
    Checkpoint->SavedAtUtc = FDateTime::UtcNow();
    Checkpoint->SnapshotBytes.Append(Bytes.data(), static_cast<int32>(Bytes.size()));

    if (!UGameplayStatics::SaveGameToSlot(Checkpoint, CheckpointSlotName, CheckpointUserIndex))
    {
        return StoreCommandResult(false, TEXT("CHECKPOINT FAILED // could not write save slot"));
    }

    bCheckpointAvailable = true;
    SavedCheckpointTick = Checkpoint->CheckpointTick;
    Runtime->ResetReplayRecorder();
    LastCommandMessage = FString::Printf(TEXT("CHECKPOINT SEALED // TICK %llu"),
                                         static_cast<unsigned long long>(SavedCheckpointTick));
    UE_LOG(LogAshenSimulation, Display, TEXT("Saved SnapshotV1 checkpoint at tick %llu (%d bytes)"),
           static_cast<unsigned long long>(SavedCheckpointTick), Checkpoint->SnapshotBytes.Num());
    return true;
}

bool UAshenSimulationSubsystem::LoadCheckpoint()
{
    if (!UGameplayStatics::DoesSaveGameExist(CheckpointSlotName, CheckpointUserIndex))
    {
        bCheckpointAvailable = false;
        return StoreCommandResult(false, TEXT("RESTORE FAILED // no checkpoint in the archive"));
    }

    const UAshenCheckpointSaveGame* Checkpoint = Cast<UAshenCheckpointSaveGame>(
        UGameplayStatics::LoadGameFromSlot(CheckpointSlotName, CheckpointUserIndex));
    if (Checkpoint == nullptr || Checkpoint->AdapterVersion != UAshenCheckpointSaveGame::CurrentAdapterVersion)
    {
        return StoreCommandResult(false, TEXT("RESTORE FAILED // unsupported save adapter"));
    }

    const std::span<const std::uint8_t> Bytes{
        Checkpoint->SnapshotBytes.GetData(), static_cast<size_t>(Checkpoint->SnapshotBytes.Num())};
    ashen::core::SnapshotLoadResult Loaded = ashen::core::load_snapshot_v1(Bytes);
    if (!Loaded)
    {
        LastCommandMessage = FString::Printf(TEXT("RESTORE REJECTED // %s"),
                                             *CoreText(ashen::core::to_string(Loaded.error)));
        UE_LOG(LogAshenSimulation, Warning, TEXT("Rejected checkpoint SnapshotV1: %s"), *LastCommandMessage);
        return false;
    }

    const bool bMetadataMatches =
        Checkpoint->SnapshotSchemaVersion == Loaded.header.schema_version &&
        Checkpoint->ContentDigest == Loaded.header.content_digest &&
        Checkpoint->PipelineDigest == Loaded.header.pipeline_digest &&
        Checkpoint->CheckpointTick == Loaded.header.checkpoint_tick &&
        Checkpoint->CheckpointStateHash == Loaded.header.checkpoint_state_hash;
    if (!bMetadataMatches)
    {
        return StoreCommandResult(false, TEXT("RESTORE REJECTED // save metadata does not match SnapshotV1"));
    }

    FAshenSimulationRuntime* RestoredRuntime =
        new FAshenSimulationRuntime(std::move(*Loaded.simulation));
    FAshenSimulationRuntime* PreviousRuntime = Runtime;
    Runtime = RestoredRuntime;
    delete PreviousRuntime;

    const ashen::core::SimulationConfig& Config = Runtime->Simulation.config();
    bStoryMode = Config.mode == ashen::core::MatchMode::Story;
    ActiveStoryMission = Config.story_mission;
    OpponentDifficulty = ToAshenDifficulty(
        Config.commander_difficulties[ashen::core::player_index(ashen::core::PlayerId::Two)]);
    Accumulator = 0.0f;
    bCheckpointAvailable = true;
    SavedCheckpointTick = Loaded.header.checkpoint_tick;
    DestroyWorldActors();
    SyncWorldActors();

    LastCommandMessage = FString::Printf(TEXT("CHECKPOINT RESTORED // TICK %llu"),
                                         static_cast<unsigned long long>(SavedCheckpointTick));
    UE_LOG(LogAshenSimulation, Display, TEXT("Restored SnapshotV1 checkpoint at tick %llu"),
           static_cast<unsigned long long>(SavedCheckpointTick));
    return true;
}

bool UAshenSimulationSubsystem::ExportReplay()
{
    if (Runtime == nullptr || !Runtime->ReplayRecorder.IsValid())
    {
        return StoreCommandResult(false, TEXT("REPLAY EXPORT FAILED // no active recording"));
    }

    const ashen::core::ReplayData Replay = Runtime->ReplayRecorder->finish(Runtime->Simulation);
    const std::vector<std::uint8_t> Bytes = ashen::core::save_replay_v1(Replay);
    if (Bytes.size() > static_cast<size_t>(MAX_int32))
    {
        return StoreCommandResult(false, TEXT("REPLAY EXPORT FAILED // recording exceeds Unreal file limits"));
    }

    const ashen::core::ReplayVerificationResult Verification = ashen::core::verify_replay_v1(Bytes);
    if (!Verification)
    {
        LastCommandMessage = FString::Printf(TEXT("REPLAY REJECTED // %s at tick %llu"),
                                             *CoreText(ashen::core::to_string(Verification.error)),
                                             static_cast<unsigned long long>(Verification.mismatch_tick));
        UE_LOG(LogAshenSimulation, Error, TEXT("Refused to export invalid ReplayV1: %s"), *LastCommandMessage);
        return false;
    }

    const FString ReplayDirectory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Replays"));
    if (!IFileManager::Get().DirectoryExists(*ReplayDirectory) &&
        !IFileManager::Get().MakeDirectory(*ReplayDirectory, true))
    {
        return StoreCommandResult(false, TEXT("REPLAY EXPORT FAILED // could not create Saved/Replays"));
    }
    const FString ExportId = FGuid::NewGuid().ToString(EGuidFormats::Digits).Left(8);
    const FString Filename = FString::Printf(
        TEXT("Vowfall-%s-tick-%llu-%s.vowreplay"), *FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S")),
        static_cast<unsigned long long>(Runtime->Simulation.tick()), *ExportId);
    const FString ReplayPath = FPaths::Combine(ReplayDirectory, Filename);
    TArray<uint8> UnrealBytes;
    UnrealBytes.Append(Bytes.data(), static_cast<int32>(Bytes.size()));
    if (!FFileHelper::SaveArrayToFile(UnrealBytes, *ReplayPath))
    {
        return StoreCommandResult(false, TEXT("REPLAY EXPORT FAILED // could not write replay file"));
    }

    LastCommandMessage = FString::Printf(TEXT("REPLAY VERIFIED // Saved/Replays/%s"), *Filename);
    UE_LOG(LogAshenSimulation, Display, TEXT("Exported verified ReplayV1: %s"), *ReplayPath);
    return true;
}

void UAshenSimulationSubsystem::DestroyWorldActors()
{
    for (const TPair<uint32, TWeakObjectPtr<AAshenEntityActor>>& Pair : EntityActors)
    {
        if (Pair.Value.IsValid())
        {
            Pair.Value->Destroy();
        }
    }
    for (const TPair<uint32, TWeakObjectPtr<AAshenResourceActor>>& Pair : ResourceActors)
    {
        if (Pair.Value.IsValid())
        {
            Pair.Value->Destroy();
        }
    }
    for (const TPair<uint32, TWeakObjectPtr<AAshenControlPointActor>>& Pair : ControlPointActors)
    {
        if (Pair.Value.IsValid())
        {
            Pair.Value->Destroy();
        }
    }
    EntityActors.Reset();
    ResourceActors.Reset();
    ControlPointActors.Reset();
    KnownControlPointOwners.Reset();
    KnownControlPointInfluence.Reset();
}

void UAshenSimulationSubsystem::ConfigureSkirmish()
{
    bStoryMode = false;
    OpponentDifficulty = EAshenAIDifficulty::Standard;
    RestartMatch();
}

void UAshenSimulationSubsystem::ConfigureStoryMission(const ashen::core::StoryMissionId Mission)
{
    const ashen::core::StoryMissionDefinition* Definition =
        ashen::core::find_story_mission(Mission);
    if (Definition == nullptr || !Definition->vertical_slice_ready)
    {
        UE_LOG(LogAshenSimulation, Warning, TEXT("Rejected unavailable story mission %d"),
               static_cast<int32>(Mission));
        return;
    }
    bStoryMode = true;
    ActiveStoryMission = Mission;
    OpponentDifficulty = EAshenAIDifficulty::Story;
    RestartMatch();
}

void UAshenSimulationSubsystem::SetOpponentDifficulty(
    const EAshenAIDifficulty Difficulty)
{
    OpponentDifficulty = Difficulty;
}

bool UAshenSimulationSubsystem::IsMatchOver() const
{
    return Runtime != nullptr && Runtime->Simulation.status() != ashen::core::MatchStatus::Playing;
}

bool UAshenSimulationSubsystem::DidLocalPlayerWin() const
{
    return Runtime != nullptr && Runtime->Simulation.winner().has_value() &&
           Runtime->Simulation.winner().value() == ashen::core::PlayerId::One;
}

bool UAshenSimulationSubsystem::StoreCommandResult(const bool bOk, const FString& FailureMessage)
{
    LastCommandMessage = FailureMessage;
    return bOk;
}

void UAshenSimulationSubsystem::StartMatch()
{
    delete Runtime;
    ashen::core::SimulationConfig Config{};
    if (bStoryMode)
    {
        if (const ashen::core::StoryMissionDefinition* Mission =
                ashen::core::find_story_mission(ActiveStoryMission))
        {
            Config.mode = ashen::core::MatchMode::Story;
            Config.story_mission = Mission->id;
            Config.player_one_faction = Mission->player_faction;
            Config.player_two_faction = Mission->opposing_faction;
            Config.match_seed = static_cast<uint64>(Mission->campaign_order) + 1;
        }
    }
    Config.commander_players[ashen::core::player_index(ashen::core::PlayerId::Two)] = true;
    Config.commander_difficulties[ashen::core::player_index(
        ashen::core::PlayerId::Two)] = ToCoreDifficulty(OpponentDifficulty);
    Runtime = new FAshenSimulationRuntime(Config);
    Accumulator = 0.0f;
    LastCommandMessage.Reset();
    KnownControlPointOwners.Reset();
    KnownControlPointInfluence.Reset();
    bGameplayEnabled = false;
    UE_LOG(LogAshenSimulation, Display,
           TEXT("Match started: %d entities, %d resource fields, %d fixed ticks/sec, mode %s, opponent difficulty %s"),
           static_cast<int32>(Runtime->Simulation.entities().size()),
           static_cast<int32>(Runtime->Simulation.resources().size()),
           ashen::core::kTicksPerSecond,
           Config.mode == ashen::core::MatchMode::Story ? TEXT("story") : TEXT("skirmish"),
           *CoreText(ashen::core::to_string(
               Config.commander_difficulties[ashen::core::player_index(
                   ashen::core::PlayerId::Two)])));
    SyncWorldActors();
}

void UAshenSimulationSubsystem::PrimeOpeningEconomy()
{
    if (Runtime == nullptr)
    {
        return;
    }

    using namespace ashen::core;
    std::vector<EntityId> Workers;
    for (const Entity& EntityState : Runtime->Simulation.entities())
    {
        if (EntityState.owner == PlayerId::One && EntityState.type == EntityType::Worker)
        {
            Workers.push_back(EntityState.id);
        }
    }

    const ResourceNode* ChosenResource = nullptr;
    for (const ResourceNode& Resource : Runtime->Simulation.resources())
    {
        if (ChosenResource == nullptr || Resource.position.x < ChosenResource->position.x)
        {
            ChosenResource = &Resource;
        }
    }
    if (Workers.empty() || ChosenResource == nullptr)
    {
        return;
    }

    Command Gather{};
    Gather.player = PlayerId::One;
    Gather.type = CommandType::Gather;
    Gather.entities = std::move(Workers);
    Gather.resource = ChosenResource->id;
    static_cast<void>(Runtime->Simulation.execute_now(std::move(Gather)));
    Runtime->ResetReplayRecorder();
    UE_LOG(LogAshenSimulation, Display, TEXT("Opening workers assigned to cursed iron"));
}

void UAshenSimulationSubsystem::SyncWorldActors()
{
    UWorld* World = GetWorld();
    if (Runtime == nullptr || World == nullptr)
    {
        return;
    }

    TSet<uint32> LiveEntities;
    for (const auto& Entity : Runtime->Simulation.entities())
    {
        LiveEntities.Add(Entity.id.value);
        AAshenEntityActor* Actor = EntityActors.FindRef(Entity.id.value).Get();
        if (Actor == nullptr)
        {
            Actor = World->SpawnActor<AAshenEntityActor>();
            if (Actor == nullptr)
            {
                continue;
            }
            EntityActors.Add(Entity.id.value, Actor);
            Actor->InitializeEntity(static_cast<int32>(Entity.id.value), static_cast<uint8>(Entity.owner),
                                    ToFaction(Entity.faction), ToArchetype(Entity.type),
                                    static_cast<float>(Entity.radius) / ashen::core::kWorldScale * RenderScale);
        }

        const float HealthFraction = Entity.max_hit_points > 0
                                         ? static_cast<float>(Entity.hit_points) / Entity.max_hit_points
                                         : 0.0f;
        const float ResolveFraction = static_cast<float>(Entity.resolve) / 100.0f;
        const float ConstructionProgress = Entity.under_construction && Entity.construction_total_ticks > 0
                                               ? static_cast<float>(Entity.construction_ticks) /
                                                     Entity.construction_total_ticks
                                               : 1.0f;
        Actor->ApplySimulationState(ToWorldPosition(Entity.position.x, Entity.position.y), HealthFraction,
                                    ResolveFraction, ConstructionProgress, Entity.under_construction);
        Actor->SetFogVisible(Entity.owner == ashen::core::PlayerId::One ||
                             Runtime->Simulation.is_entity_visible_to(Entity, ashen::core::PlayerId::One));
    }

    TArray<uint32> EntityKeys;
    EntityActors.GetKeys(EntityKeys);
    for (const uint32 Id : EntityKeys)
    {
        if (!LiveEntities.Contains(Id))
        {
            if (AAshenEntityActor* Actor = EntityActors.FindRef(Id).Get())
            {
                Actor->Destroy();
            }
            EntityActors.Remove(Id);
        }
    }

    for (const auto& Resource : Runtime->Simulation.resources())
    {
        AAshenResourceActor* Actor = ResourceActors.FindRef(Resource.id.value).Get();
        if (Actor == nullptr)
        {
            Actor = World->SpawnActor<AAshenResourceActor>();
            if (Actor == nullptr)
            {
                continue;
            }
            ResourceActors.Add(Resource.id.value, Actor);
            Actor->InitializeResource(static_cast<int32>(Resource.id.value),
                                      static_cast<float>(Resource.radius) / ashen::core::kWorldScale * RenderScale);
        }
        Actor->ApplySimulationState(ToWorldPosition(Resource.position.x, Resource.position.y));
        Actor->SetFogState(ToVisibility(Runtime->Simulation.visibility_state_at(
            Resource.position, ashen::core::PlayerId::One)));
    }

    TSet<uint32> LiveControlPoints;
    for (const auto& Point : Runtime->Simulation.control_points())
    {
        LiveControlPoints.Add(Point.id.value);
        AAshenControlPointActor* Actor = ControlPointActors.FindRef(Point.id.value).Get();
        if (Actor == nullptr)
        {
            Actor = World->SpawnActor<AAshenControlPointActor>();
            if (Actor == nullptr)
            {
                continue;
            }
            ControlPointActors.Add(Point.id.value, Actor);
            Actor->InitializeControlPoint(static_cast<int32>(Point.id.value),
                                          static_cast<float>(Point.radius) / ashen::core::kWorldScale * RenderScale);
        }
        const ashen::core::VisibilityState Visibility = Runtime->Simulation.visibility_state_at(
            Point.position, ashen::core::PlayerId::One);
        if (Visibility == ashen::core::VisibilityState::Visible)
        {
            KnownControlPointOwners.Add(Point.id.value,
                                        Point.owner.has_value() ? static_cast<int32>(*Point.owner) : -1);
            KnownControlPointInfluence.Add(Point.id.value,
                                           static_cast<float>(Point.influence) / 10'000.0f);
        }
        const int32* KnownOwner = KnownControlPointOwners.Find(Point.id.value);
        const float* KnownInfluence = KnownControlPointInfluence.Find(Point.id.value);
        EAshenFaction OwnerFaction = EAshenFaction::None;
        if (KnownOwner != nullptr && (*KnownOwner == 0 || *KnownOwner == 1))
        {
            const auto Owner = *KnownOwner == 1 ? ashen::core::PlayerId::Two : ashen::core::PlayerId::One;
            OwnerFaction = ToFaction(Runtime->Simulation.player(Owner).faction);
        }
        EAshenFaction PressureFaction = EAshenFaction::None;
        if (KnownInfluence != nullptr && !FMath::IsNearlyZero(*KnownInfluence))
        {
            const auto PressuringPlayer = *KnownInfluence > 0.0f
                                              ? ashen::core::PlayerId::One
                                              : ashen::core::PlayerId::Two;
            PressureFaction = ToFaction(Runtime->Simulation.player(PressuringPlayer).faction);
        }
        Actor->ApplySimulationState(ToWorldPosition(Point.position.x, Point.position.y),
                                    OwnerFaction, PressureFaction,
                                    KnownInfluence != nullptr ? *KnownInfluence : 0.0f,
                                    Runtime->Simulation.ruin_tide());
        Actor->SetFogState(ToVisibility(Visibility));
    }

    TArray<uint32> ControlKeys;
    ControlPointActors.GetKeys(ControlKeys);
    for (const uint32 Id : ControlKeys)
    {
        if (!LiveControlPoints.Contains(Id))
        {
            if (AAshenControlPointActor* Actor = ControlPointActors.FindRef(Id).Get())
            {
                Actor->Destroy();
            }
            ControlPointActors.Remove(Id);
        }
    }
}

FVector UAshenSimulationSubsystem::ToWorldPosition(const int32 CoreX, const int32 CoreY) const
{
    return {
        static_cast<float>(CoreX) / ashen::core::kWorldScale * RenderScale,
        static_cast<float>(CoreY) / ashen::core::kWorldScale * RenderScale,
        0.0f,
    };
}
