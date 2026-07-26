#pragma once

#include "CoreMinimal.h"
#include "AshenTypes.generated.h"

UENUM(BlueprintType)
enum class EAshenEntityArchetype : uint8
{
    Worker,
    Vanguard,
    Skirmisher,
    Command,
    Barracks,
    Turret,
};

UENUM(BlueprintType)
enum class EAshenResearch : uint8
{
    TierTwo,
    TemperedOaths,
    Wardcraft,
    ChorusOfKnives,
    PitBroods,
    VaultPlate,
    SiegeLiturgy,
};

UENUM(BlueprintType)
enum class EAshenStance : uint8
{
    Aggressive,
    Defensive,
    Hold,
};

UENUM(BlueprintType)
enum class EAshenVisibility : uint8
{
    Hidden,
    Explored,
    Visible,
};

UENUM(BlueprintType)
enum class EAshenAIDifficulty : uint8
{
    Story,
    Standard,
    Veteran,
    Competitive,
};

USTRUCT(BlueprintType)
struct FAshenVisibilityGridView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 Columns = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 Rows = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    float CellWorldSize = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    TArray<EAshenVisibility> Cells;
};

USTRUCT(BlueprintType)
struct FAshenPlayerView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 Ore = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 SupplyUsed = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 SupplyCap = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 Resolve = 100;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 TechTier = 1;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    float PowerCooldownSeconds = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 ControlledRelics = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    FString ActiveResearch;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    float ResearchProgress = 0.0f;
};

USTRUCT(BlueprintType)
struct FAshenEntityView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 EntityId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    EAshenEntityArchetype Archetype = EAshenEntityArchetype::Worker;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 HitPoints = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 MaxHitPoints = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 Resolve = 100;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    bool bUnderConstruction = false;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    float ConstructionProgress = 1.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 QueueCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    float QueueProgress = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    EAshenStance Stance = EAshenStance::Aggressive;
};

USTRUCT(BlueprintType)
struct FAshenControlPointView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 ControlPointId = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    FVector WorldPosition = FVector::ZeroVector;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 OwnerIndex = -1;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    float Influence = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    EAshenVisibility Visibility = EAshenVisibility::Hidden;
};

USTRUCT(BlueprintType)
struct FAshenResearchView
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    EAshenResearch Research = EAshenResearch::TierTwo;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    FString Label;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    int32 Cost = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    bool bAvailable = false;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    bool bCompleted = false;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen", meta = (ScriptName = "is_in_progress"))
    bool bInProgress = false;

    UPROPERTY(BlueprintReadOnly, Category = "Ashen")
    float Progress = 0.0f;
};
