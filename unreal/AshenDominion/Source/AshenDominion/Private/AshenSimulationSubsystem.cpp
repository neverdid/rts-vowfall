#include "AshenSimulationSubsystem.h"

#include "AshenControlPointActor.h"
#include "AshenEntityActor.h"
#include "AshenResourceActor.h"
#include "ashen/core/Catalog.hpp"
#include "ashen/core/Simulation.hpp"

#include "Engine/World.h"
#include "Stats/Stats.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

DEFINE_LOG_CATEGORY_STATIC(LogAshenSimulation, Log, All);

class FAshenSimulationRuntime final
{
public:
    explicit FAshenSimulationRuntime(const ashen::core::SimulationConfig& Config) : Simulation(Config) {}

    ashen::core::Simulation Simulation;
};

namespace
{
constexpr float FixedStepSeconds = 1.0f / static_cast<float>(ashen::core::kTicksPerSecond);
constexpr int32 MaxCatchUpSteps = 8;

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
        Runtime->Simulation.step();
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    const auto Result = Runtime->Simulation.execute_now(std::move(Command));
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
    View.Archetype = ToArchetype(Entity->type);
    View.Label = CoreText(ashen::core::entity_definition(Runtime->Simulation.player(Entity->owner).faction,
                                                         Entity->type).label);
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
    const FAshenPlayerView Player = GetPlayerView(0);
    return FString::Printf(TEXT("Destroy the rival command keep  //  Relics held %d / %d"),
                           Player.ControlledRelics,
                           static_cast<int32>(Runtime->Simulation.control_points().size()));
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
    StartMatch();
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
           TEXT("Match started: %d entities, %d resource fields, %d fixed ticks/sec, opponent difficulty %s"),
           static_cast<int32>(Runtime->Simulation.entities().size()),
           static_cast<int32>(Runtime->Simulation.resources().size()),
           ashen::core::kTicksPerSecond,
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
                                    ToArchetype(Entity.type),
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
        Actor->ApplySimulationState(ToWorldPosition(Point.position.x, Point.position.y),
                                    KnownOwner != nullptr ? *KnownOwner : -1,
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
