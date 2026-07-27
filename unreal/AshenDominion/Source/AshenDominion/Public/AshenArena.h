#pragma once

#include "GameFramework/Actor.h"
#include "AshenArena.generated.h"

class UDirectionalLightComponent;
class UExponentialHeightFogComponent;
class UInstancedStaticMeshComponent;
class UPointLightComponent;
class UPostProcessComponent;
class UProceduralMeshComponent;
class USceneComponent;
class USkyAtmosphereComponent;
class USkyLightComponent;
class UStaticMeshComponent;

UCLASS()
class ASHENDOMINION_API AAshenArena final : public AActor
{
    GENERATED_BODY()

public:
    AAshenArena();

protected:
    virtual void BeginPlay() override;

private:
    void BuildTerrain();
    void BuildRiver();
    void BuildRoadsAndBridges();
    void BuildFortifications();
    void BuildVegetation();
    void BuildLandmarks();

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UStaticMeshComponent> Ground;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UProceduralMeshComponent> Terrain;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UProceduralMeshComponent> RoadSurface;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UProceduralMeshComponent> RoadStoneSurface;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UProceduralMeshComponent> RoadRutSurface;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UProceduralMeshComponent> RiverSurface;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UProceduralMeshComponent> RiverShoreSurface;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> RiverBanks;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> Reeds;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> BridgeTimbers;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> BridgeIron;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> TreeTrunks;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> TreeCrowns;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> TreeCrownsShadow;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> TreeCanopies;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> DeadBranches;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> GrassTufts;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> Rocks;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MountainRocks;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MineMouths;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MineTimbers;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> ForestRoots;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> HumanWalls;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> HumanTowers;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> HumanRoofs;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> HumanFoundations;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> HumanTrim;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> HumanBanners;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MonsterMasses;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MonsterSpikes;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MonsterRibs;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MonsterSinew;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> BonePalisade;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> RitualStones;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> MythicArches;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> BrazierBowls;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|World")
    TObjectPtr<UInstancedStaticMeshComponent> EmberCores;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|Atmosphere")
    TObjectPtr<UDirectionalLightComponent> MoonLight;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|Atmosphere")
    TObjectPtr<UDirectionalLightComponent> FillLight;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|Atmosphere")
    TObjectPtr<USkyAtmosphereComponent> Atmosphere;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|Atmosphere")
    TObjectPtr<USkyLightComponent> SkyLight;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|Atmosphere")
    TObjectPtr<UExponentialHeightFogComponent> Fog;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|Atmosphere")
    TObjectPtr<UPostProcessComponent> PostProcess;

    UPROPERTY(VisibleAnywhere, Category = "Ashen|Atmosphere")
    TArray<TObjectPtr<UPointLightComponent>> AccentLights;
};
