#pragma once

#include "AshenTypes.h"
#include "GameFramework/Actor.h"
#include "AshenEntityActor.generated.h"

class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class ASHENDOMINION_API AAshenEntityActor final : public AActor
{
    GENERATED_BODY()

public:
    AAshenEntityActor();
    virtual void Tick(float DeltaSeconds) override;

    void InitializeEntity(int32 InEntityId, uint8 InOwnerIndex, EAshenFaction InFaction,
                          EAshenEntityArchetype InArchetype, float Radius);
    void ApplySimulationState(const FVector& GroundPosition, float HealthFraction, float ResolveFraction,
                              float ConstructionProgress, bool bUnderConstruction);
    void SetSelected(bool bInSelected);
    void SetFogVisible(bool bVisible);

    UFUNCTION(BlueprintPure, Category = "Ashen")
    int32 GetEntityId() const noexcept { return EntityId; }

    UFUNCTION(BlueprintPure, Category = "Ashen")
    uint8 GetOwnerIndex() const noexcept { return OwnerIndex; }

    UFUNCTION(BlueprintPure, Category = "Ashen")
    EAshenFaction GetFaction() const noexcept { return Faction; }

    UFUNCTION(BlueprintPure, Category = "Ashen")
    EAshenEntityArchetype GetArchetype() const noexcept { return Archetype; }

    UFUNCTION(BlueprintPure, Category = "Ashen")
    bool IsFogVisible() const noexcept { return bFogVisible; }

private:
    UStaticMeshComponent* CreatePart(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale,
                                     const FRotator& Rotation, const FLinearColor& Color, float Roughness);
    void BuildHumanVisuals(float Diameter);
    void BuildMonsterVisuals(float Diameter);
    void UpdateHealthDisplay(float HealthFraction);
    void RefreshFactionLight();
    bool IsBuilding() const noexcept;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UStaticMeshComponent> EntityMesh;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> DetailMeshes;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UStaticMeshComponent> SelectionMarker;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UStaticMeshComponent> HealthBack;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UStaticMeshComponent> HealthFill;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UPointLightComponent> FactionLight;

    int32 EntityId = 0;
    uint8 OwnerIndex = 0;
    EAshenFaction Faction = EAshenFaction::None;
    EAshenEntityArchetype Archetype = EAshenEntityArchetype::Worker;
    float VisualHeight = 80.0f;
    float HealthBarWidth = 50.0f;
    float LastHealthFraction = 1.0f;
    float HealthLightIntensity = 380.0f;
    float ConstructionLightScale = 1.0f;
    float HitFlashStrength = 0.0f;
    FLinearColor FactionBaseColor = FLinearColor::White;
    FVector LastGroundPosition = FVector::ZeroVector;
    bool bHasGroundPosition = false;
    bool bHasSimulationState = false;
    bool bSelected = false;
    bool bFogVisible = true;
};
