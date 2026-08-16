#pragma once

#include "AshenTypes.h"
#include "Subsystems/WorldSubsystem.h"
#include "ashen/core/Campaign.hpp"
#include "AshenSimulationSubsystem.generated.h"

class AAshenEntityActor;
class AAshenControlPointActor;
class AAshenResourceActor;
class FAshenSimulationRuntime;

UCLASS()
class ASHENDOMINION_API UAshenSimulationSubsystem final : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    static constexpr float RenderScale = 2.0f;

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void OnWorldBeginPlay(UWorld& InWorld) override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueMove(const TArray<int32>& EntityIds, const FVector& WorldTarget, bool bQueue);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueAttack(const TArray<int32>& EntityIds, int32 TargetEntityId, bool bQueue);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueAttackMove(const TArray<int32>& EntityIds, const FVector& WorldTarget, bool bQueue);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueGather(const TArray<int32>& EntityIds, int32 ResourceId, bool bQueue);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssuePatrol(const TArray<int32>& EntityIds, const FVector& WorldTarget, bool bQueue);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueStop(const TArray<int32>& EntityIds);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueHold(const TArray<int32>& EntityIds, bool bQueue);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueSetRallyPoint(int32 ProducerId, const FVector& WorldTarget);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueTrain(int32 ProducerId, bool bSecondaryUnit);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueBuild(int32 WorkerId, EAshenEntityArchetype Building, const FVector& WorldTarget);

    UFUNCTION(BlueprintPure, Category = "Ashen|Commands")
    bool CanPlaceBuilding(EAshenEntityArchetype Building, const FVector& WorldTarget) const;

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueResearch(int32 ProducerId, EAshenResearch Research);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueActivatePower();

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueRetreat(const TArray<int32>& EntityIds);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueSetStance(const TArray<int32>& EntityIds, EAshenStance Stance);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Commands")
    bool IssueRecoverCasualty(int32 UnitIdentityId);

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    FAshenPlayerView GetPlayerView(int32 PlayerIndex) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    FAshenEntityView GetEntityView(int32 EntityId) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    TArray<FAshenControlPointView> GetControlPointViews() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    EAshenVisibility GetLocalVisibilityAt(const FVector& WorldPosition) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    FAshenVisibilityGridView GetLocalVisibilityGrid() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Events")
    TArray<FAshenSimulationEventView> GetSimulationEventsSince(int64 LastEventId) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Casualties")
    bool IsCasualtyRecoverable(int32 UnitIdentityId) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Casualties")
    bool CanIssueCasualtyRecovery(int32 UnitIdentityId) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Casualties")
    int32 GetCasualtyRecoveryAnchorId(int32 UnitIdentityId) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    TArray<FAshenResearchView> GetResearchViews(int32 ProducerId) const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    int32 GetRuinTide() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    FString GetFactionPowerLabel() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    FString GetObjectiveText() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Story")
    bool IsStoryMatch() const noexcept { return bStoryMode; }

    UFUNCTION(BlueprintPure, Category = "Ashen|Story")
    FString GetStoryChapterText() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Story")
    FString GetStoryMissionTitle() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Story")
    FString GetStoryProtagonist() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|Story")
    FString GetStoryVowText() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    FString GetLastCommandMessage() const { return LastCommandMessage; }

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    int64 GetSimulationTick() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    int32 GetEntityCount() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    EAshenEntityArchetype GetEntityArchetype(int32 EntityId) const;

    FString GetEntityOrderLabel(int32 EntityId) const;
    TArray<FVector> GetEntityRoute(int32 EntityId) const;

    UFUNCTION(BlueprintCallable, Category = "Ashen|State")
    void SetGameplayEnabled(bool bEnabled);

    UFUNCTION(BlueprintCallable, Category = "Ashen|State")
    void RestartMatch();

    UFUNCTION(BlueprintCallable, Category = "Ashen|Persistence")
    bool SaveCheckpoint();

    UFUNCTION(BlueprintCallable, Category = "Ashen|Persistence")
    bool LoadCheckpoint();

    UFUNCTION(BlueprintCallable, Category = "Ashen|Persistence")
    bool ExportReplay();

    UFUNCTION(BlueprintPure, Category = "Ashen|Persistence")
    bool HasCheckpoint() const noexcept { return bCheckpointAvailable; }

    UFUNCTION(BlueprintPure, Category = "Ashen|Persistence")
    int64 GetCheckpointTick() const noexcept { return static_cast<int64>(SavedCheckpointTick); }

    void ConfigureSkirmish();
    void ConfigureStoryMission(ashen::core::StoryMissionId Mission);

    UFUNCTION(BlueprintCallable, Category = "Ashen|Match")
    void SetOpponentDifficulty(EAshenAIDifficulty Difficulty);

    UFUNCTION(BlueprintPure, Category = "Ashen|Match")
    EAshenAIDifficulty GetOpponentDifficulty() const noexcept { return OpponentDifficulty; }

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    bool IsGameplayEnabled() const noexcept { return bGameplayEnabled; }

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    bool IsMatchOver() const;

    UFUNCTION(BlueprintPure, Category = "Ashen|State")
    bool DidLocalPlayerWin() const;

private:
    void StartMatch();
    void PrimeOpeningEconomy();
    void DestroyWorldActors();
    void SyncWorldActors();
    bool StoreCommandResult(bool bOk, const FString& FailureMessage);
    FVector ToWorldPosition(int32 CoreX, int32 CoreY) const;

    FAshenSimulationRuntime* Runtime = nullptr;
    float Accumulator = 0.0f;
    bool bGameplayEnabled = false;
    bool bStoryMode = false;
    ashen::core::StoryMissionId ActiveStoryMission = ashen::core::StoryMissionId::BridgeOfNames;
    EAshenAIDifficulty OpponentDifficulty = EAshenAIDifficulty::Standard;
    uint64 SavedCheckpointTick = 0;
    bool bCheckpointAvailable = false;
    FString LastCommandMessage;
    TMap<uint32, TWeakObjectPtr<AAshenEntityActor>> EntityActors;
    TMap<uint32, TWeakObjectPtr<AAshenResourceActor>> ResourceActors;
    TMap<uint32, TWeakObjectPtr<AAshenControlPointActor>> ControlPointActors;
    TMap<uint32, int32> KnownControlPointOwners;
    TMap<uint32, float> KnownControlPointInfluence;
};
