#pragma once

#include "AshenTypes.h"
#include "GameFramework/Actor.h"
#include "AshenControlPointActor.generated.h"

class UPointLightComponent;
class USceneComponent;
class UStaticMesh;
class UStaticMeshComponent;

UCLASS()
class ASHENDOMINION_API AAshenControlPointActor final : public AActor
{
    GENERATED_BODY()

public:
    AAshenControlPointActor();

    void InitializeControlPoint(int32 InControlPointId, float Radius);
    void ApplySimulationState(const FVector& GroundPosition, EAshenFaction OwnerFaction,
                              EAshenFaction PressureFaction, float Influence, int32 RuinTide);
    void SetFogState(EAshenVisibility Visibility);

    UFUNCTION(BlueprintPure, Category = "Ashen")
    int32 GetControlPointId() const noexcept { return ControlPointId; }

private:
    UStaticMeshComponent* CreatePart(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale,
                                     const FRotator& Rotation, const FLinearColor& Color, float Roughness);
    void RefreshCaptureMaterial(EAshenFaction OwnerFaction, EAshenFaction PressureFaction, float Influence);

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UStaticMeshComponent> ShrineBase;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UStaticMeshComponent> CaptureDisc;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UStaticMeshComponent> Reliquary;

    UPROPERTY(VisibleAnywhere, Category = "Ashen")
    TObjectPtr<UPointLightComponent> RelicLight;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UStaticMeshComponent>> DetailMeshes;

    int32 ControlPointId = 0;
    EAshenFaction LastOwnerFaction = EAshenFaction::None;
    EAshenFaction LastPressureFaction = EAshenFaction::None;
    int32 LastInfluenceBucket = -1;
};
