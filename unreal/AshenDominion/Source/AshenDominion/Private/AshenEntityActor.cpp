#include "AshenEntityActor.h"

#include "AshenMaterials.h"

#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/UObjectGlobals.h"

namespace
{
UStaticMesh* LoadShape(const TCHAR* Path)
{
    return LoadObject<UStaticMesh>(nullptr, Path);
}

const FLinearColor HumanIron(0.15f, 0.17f, 0.18f);
const FLinearColor HumanCloth(0.30f, 0.045f, 0.025f);
const FLinearColor HumanBronze(0.48f, 0.25f, 0.055f);
const FLinearColor HumanStone(0.27f, 0.28f, 0.27f);
const FLinearColor HumanSkin(0.38f, 0.25f, 0.18f);
const FLinearColor MonsterFlesh(0.24f, 0.018f, 0.028f);
const FLinearColor MonsterDark(0.055f, 0.018f, 0.025f);
const FLinearColor MonsterBone(0.49f, 0.42f, 0.31f);
const FLinearColor MonsterBlood(0.52f, 0.015f, 0.022f);
const FLinearColor ConcordVerdigris(0.08f, 0.46f, 0.39f);

FLinearColor FactionColor(const EAshenFaction Faction)
{
    switch (Faction)
    {
    case EAshenFaction::Compact:
        return HumanBronze;
    case EAshenFaction::Ascendancy:
        return MonsterBlood;
    case EAshenFaction::Concord:
        return ConcordVerdigris;
    case EAshenFaction::None:
        return FLinearColor::White;
    }
    return FLinearColor::White;
}
}

AAshenEntityActor::AAshenEntityActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bStartWithTickEnabled = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Movable);

    EntityMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EntityMesh"));
    EntityMesh->SetupAttachment(SceneRoot);
    EntityMesh->SetMobility(EComponentMobility::Movable);
    EntityMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    EntityMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
    EntityMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);

    SelectionMarker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("SelectionMarker"));
    SelectionMarker->SetupAttachment(SceneRoot);
    SelectionMarker->SetMobility(EComponentMobility::Movable);
    SelectionMarker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    SelectionMarker->SetVisibility(false);

    HealthBack = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HealthBack"));
    HealthBack->SetupAttachment(SceneRoot);
    HealthBack->SetMobility(EComponentMobility::Movable);
    HealthBack->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HealthBack->SetVisibility(false);

    HealthFill = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HealthFill"));
    HealthFill->SetupAttachment(SceneRoot);
    HealthFill->SetMobility(EComponentMobility::Movable);
    HealthFill->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HealthFill->SetVisibility(false);

    FactionLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FactionLight"));
    FactionLight->SetupAttachment(SceneRoot);
    FactionLight->SetCastShadows(false);
    FactionLight->SetAttenuationRadius(165.0f);
    FactionLight->SetIntensity(380.0f);
}

void AAshenEntityActor::InitializeEntity(const int32 InEntityId, const uint8 InOwnerIndex,
                                         const EAshenFaction InFaction,
                                         const EAshenEntityArchetype InArchetype, const float Radius)
{
    EntityId = InEntityId;
    OwnerIndex = InOwnerIndex;
    Faction = InFaction;
    Archetype = InArchetype;
    const float Diameter = FMath::Max(20.0f, Radius * 2.0f);

    if (UStaticMesh* MarkerMesh = LoadShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder")))
    {
        SelectionMarker->SetStaticMesh(MarkerMesh);
    }
    if (UStaticMesh* BarMesh = LoadShape(TEXT("/Engine/BasicShapes/Cube.Cube")))
    {
        HealthBack->SetStaticMesh(BarMesh);
        HealthFill->SetStaticMesh(BarMesh);
    }

    if (Faction != EAshenFaction::Ascendancy)
    {
        BuildHumanVisuals(Diameter);
    }
    else
    {
        BuildMonsterVisuals(Diameter);
    }

    const FLinearColor ResolvedFactionColor = FactionColor(Faction);
    FactionBaseColor = ResolvedFactionColor;
    Ashen::Materials::Apply(SelectionMarker, this, ResolvedFactionColor, 0.32f);
    Ashen::Materials::Apply(HealthBack, this, FLinearColor(0.012f, 0.014f, 0.014f), 0.88f);
    Ashen::Materials::Apply(HealthFill, this, FLinearColor(0.18f, 0.68f, 0.26f), 0.46f);

    SelectionMarker->SetRelativeScale3D({Diameter * 1.45f / 100.0f, Diameter * 1.45f / 100.0f, 0.024f});
    SelectionMarker->SetRelativeLocation({0.0f, 0.0f, 2.0f});

    HealthBarWidth = FMath::Clamp(Diameter * 0.72f, 42.0f, 128.0f);
    HealthBack->SetRelativeLocation({0.0f, 0.0f, VisualHeight + 24.0f});
    HealthBack->SetRelativeScale3D({HealthBarWidth / 100.0f, 0.075f, 0.035f});
    HealthFill->SetRelativeLocation({0.0f, 0.0f, VisualHeight + 24.5f});

    FactionLight->SetRelativeLocation({0.0f, 0.0f, VisualHeight * 0.62f});
    FactionLight->SetLightColor(ResolvedFactionColor);
    FactionLight->SetAttenuationRadius(FMath::Clamp(Diameter * 2.5f, 145.0f, 430.0f));
    SetActorRotation(FRotator(0.0f, OwnerIndex == 0 ? 0.0f : 180.0f, 0.0f));
    EntityMesh->SetCustomDepthStencilValue(OwnerIndex == 0 ? 1 : 2);
    for (UStaticMeshComponent* Part : DetailMeshes)
    {
        Part->SetCustomDepthStencilValue(OwnerIndex == 0 ? 1 : 2);
    }
    UpdateHealthDisplay(1.0f);

#if WITH_EDITOR
    SetActorLabel(FString::Printf(TEXT("AshenEntity_%d_%d"), EntityId, static_cast<int32>(Archetype)));
#endif
}

void AAshenEntityActor::Tick(const float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    HitFlashStrength = FMath::Max(0.0f, HitFlashStrength - DeltaSeconds / 0.22f);
    RefreshFactionLight();
    if (HitFlashStrength <= 0.0f)
    {
        SetActorTickEnabled(false);
    }
}

void AAshenEntityActor::BuildHumanVisuals(const float Diameter)
{
    UStaticMesh* Cube = LoadShape(TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cylinder = LoadShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Cone = LoadShape(TEXT("/Engine/BasicShapes/Cone.Cone"));
    UStaticMesh* Sphere = LoadShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    switch (Archetype)
    {
    case EAshenEntityArchetype::Worker:
        VisualHeight = 90.0f;
        EntityMesh->SetStaticMesh(Cylinder);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 36.0f});
        EntityMesh->SetRelativeScale3D({0.25f, 0.22f, 0.62f});
        Ashen::Materials::Apply(EntityMesh, this, HumanCloth, 0.82f);
        CreatePart(Sphere, {0.0f, 0.0f, 76.0f}, {0.17f, 0.17f, 0.19f}, FRotator::ZeroRotator, HumanSkin, 0.72f);
        CreatePart(Cone, {-2.0f, 0.0f, 87.0f}, {0.22f, 0.22f, 0.22f}, FRotator::ZeroRotator, HumanIron, 0.54f);
        CreatePart(Cube, {-13.0f, 0.0f, 44.0f}, {0.18f, 0.34f, 0.38f}, FRotator::ZeroRotator, HumanBronze, 0.7f);
        CreatePart(Cylinder, {5.0f, 24.0f, 48.0f}, {0.045f, 0.045f, 0.70f}, FRotator(18.0f, 0.0f, -18.0f),
                   HumanIron, 0.48f);
        break;

    case EAshenEntityArchetype::Vanguard:
        VisualHeight = 118.0f;
        EntityMesh->SetStaticMesh(Cube);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 50.0f});
        EntityMesh->SetRelativeScale3D({0.48f, 0.34f, 0.82f});
        Ashen::Materials::Apply(EntityMesh, this, HumanIron, 0.42f);
        CreatePart(Sphere, {0.0f, 0.0f, 102.0f}, {0.20f, 0.20f, 0.22f}, FRotator::ZeroRotator, HumanIron, 0.38f);
        CreatePart(Cone, {0.0f, 0.0f, 118.0f}, {0.24f, 0.24f, 0.30f}, FRotator::ZeroRotator, HumanBronze, 0.44f);
        CreatePart(Cube, {0.0f, -32.0f, 60.0f}, {0.12f, 0.46f, 0.58f}, FRotator::ZeroRotator, HumanCloth, 0.64f);
        CreatePart(Cylinder, {5.0f, -37.0f, 61.0f}, {0.43f, 0.43f, 0.085f}, FRotator(90.0f, 0.0f, 0.0f),
                   HumanBronze, 0.38f);
        CreatePart(Cylinder, {9.0f, 31.0f, 69.0f}, {0.05f, 0.05f, 0.82f}, FRotator(20.0f, 0.0f, -13.0f),
                   HumanIron, 0.3f);
        CreatePart(Cube, {0.0f, -23.0f, 83.0f}, {0.34f, 0.12f, 0.20f}, FRotator::ZeroRotator, HumanBronze, 0.4f);
        CreatePart(Cube, {0.0f, 23.0f, 83.0f}, {0.34f, 0.12f, 0.20f}, FRotator::ZeroRotator, HumanBronze, 0.4f);
        break;

    case EAshenEntityArchetype::Skirmisher:
        VisualHeight = 106.0f;
        EntityMesh->SetStaticMesh(Cylinder);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 47.0f});
        EntityMesh->SetRelativeScale3D({0.30f, 0.25f, 0.76f});
        Ashen::Materials::Apply(EntityMesh, this, HumanCloth, 0.76f);
        CreatePart(Sphere, {0.0f, 0.0f, 94.0f}, {0.18f, 0.18f, 0.20f}, FRotator::ZeroRotator, HumanSkin, 0.7f);
        CreatePart(Cone, {-2.0f, 0.0f, 105.0f}, {0.25f, 0.25f, 0.28f}, FRotator::ZeroRotator, HumanIron, 0.5f);
        CreatePart(Cube, {22.0f, 0.0f, 61.0f}, {0.68f, 0.075f, 0.075f}, FRotator::ZeroRotator, HumanIron, 0.38f);
        CreatePart(Cube, {25.0f, 0.0f, 61.0f}, {0.10f, 0.58f, 0.055f}, FRotator::ZeroRotator, HumanBronze, 0.48f);
        CreatePart(Cube, {-13.0f, 0.0f, 46.0f}, {0.18f, 0.30f, 0.42f}, FRotator::ZeroRotator, HumanIron, 0.54f);
        break;

    case EAshenEntityArchetype::Command:
        VisualHeight = 268.0f;
        EntityMesh->SetStaticMesh(Cube);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 88.0f});
        EntityMesh->SetRelativeScale3D({1.42f, 1.16f, 1.70f});
        Ashen::Materials::Apply(EntityMesh, this, HumanStone, 0.88f);
        CreatePart(Cube, {0.0f, 0.0f, 188.0f}, {0.82f, 0.72f, 0.58f}, FRotator::ZeroRotator, HumanStone, 0.84f);
        for (const FVector2D Corner : {FVector2D(-72.0f, -61.0f), FVector2D(-72.0f, 61.0f),
                                       FVector2D(72.0f, -61.0f), FVector2D(72.0f, 61.0f)})
        {
            CreatePart(Cylinder, {Corner.X, Corner.Y, 111.0f}, {0.38f, 0.38f, 2.12f}, FRotator::ZeroRotator,
                       HumanStone, 0.86f);
            CreatePart(Cone, {Corner.X, Corner.Y, 242.0f}, {0.53f, 0.53f, 0.92f}, FRotator::ZeroRotator,
                       HumanCloth, 0.7f);
        }
        CreatePart(Cone, {0.0f, 0.0f, 249.0f}, {0.74f, 0.74f, 0.86f}, FRotator::ZeroRotator, HumanCloth, 0.68f);
        CreatePart(Cube, {74.0f, 0.0f, 53.0f}, {0.12f, 0.42f, 0.76f}, FRotator::ZeroRotator,
                   FLinearColor(0.025f, 0.025f, 0.025f), 0.92f);
        CreatePart(Cube, {78.0f, 0.0f, 116.0f}, {0.08f, 0.48f, 0.09f}, FRotator::ZeroRotator, HumanBronze, 0.38f);
        break;

    case EAshenEntityArchetype::Barracks:
        VisualHeight = 168.0f;
        EntityMesh->SetStaticMesh(Cube);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 56.0f});
        EntityMesh->SetRelativeScale3D({1.28f, 0.88f, 1.06f});
        Ashen::Materials::Apply(EntityMesh, this, HumanStone, 0.9f);
        CreatePart(Cube, {0.0f, -42.0f, 121.0f}, {1.42f, 0.62f, 0.16f}, FRotator(38.0f, 0.0f, 0.0f),
                   HumanCloth, 0.72f);
        CreatePart(Cube, {0.0f, 42.0f, 121.0f}, {1.42f, 0.62f, 0.16f}, FRotator(-38.0f, 0.0f, 0.0f),
                   HumanCloth, 0.72f);
        CreatePart(Cylinder, {-56.0f, -42.0f, 94.0f}, {0.28f, 0.28f, 1.48f}, FRotator::ZeroRotator,
                   HumanStone, 0.88f);
        CreatePart(Cylinder, {-56.0f, 42.0f, 94.0f}, {0.28f, 0.28f, 1.48f}, FRotator::ZeroRotator,
                   HumanStone, 0.88f);
        CreatePart(Cube, {66.0f, 0.0f, 44.0f}, {0.10f, 0.38f, 0.64f}, FRotator::ZeroRotator,
                   FLinearColor(0.025f, 0.025f, 0.025f), 0.92f);
        break;

    case EAshenEntityArchetype::Turret:
        VisualHeight = 190.0f;
        EntityMesh->SetStaticMesh(Cylinder);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 82.0f});
        EntityMesh->SetRelativeScale3D({Diameter / 125.0f, Diameter / 125.0f, 1.55f});
        Ashen::Materials::Apply(EntityMesh, this, HumanStone, 0.88f);
        CreatePart(Cone, {0.0f, 0.0f, 180.0f}, {0.58f, 0.58f, 0.88f}, FRotator::ZeroRotator, HumanCloth, 0.66f);
        CreatePart(Cylinder, {0.0f, 0.0f, 128.0f}, {0.42f, 0.42f, 0.16f}, FRotator::ZeroRotator,
                   HumanBronze, 0.38f);
        break;
    }
}

void AAshenEntityActor::BuildMonsterVisuals(const float Diameter)
{
    UStaticMesh* Cube = LoadShape(TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cylinder = LoadShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Cone = LoadShape(TEXT("/Engine/BasicShapes/Cone.Cone"));
    UStaticMesh* Sphere = LoadShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    switch (Archetype)
    {
    case EAshenEntityArchetype::Worker:
        VisualHeight = 78.0f;
        EntityMesh->SetStaticMesh(Sphere);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 35.0f});
        EntityMesh->SetRelativeScale3D({0.44f, 0.33f, 0.38f});
        Ashen::Materials::Apply(EntityMesh, this, MonsterFlesh, 0.69f);
        CreatePart(Sphere, {22.0f, 0.0f, 57.0f}, {0.18f, 0.17f, 0.19f}, FRotator::ZeroRotator,
                   MonsterDark, 0.76f);
        CreatePart(Cone, {33.0f, -9.0f, 64.0f}, {0.10f, 0.10f, 0.34f}, FRotator(0.0f, -18.0f, 28.0f),
                   MonsterBone, 0.78f);
        CreatePart(Cone, {33.0f, 9.0f, 64.0f}, {0.10f, 0.10f, 0.34f}, FRotator(0.0f, 18.0f, -28.0f),
                   MonsterBone, 0.78f);
        CreatePart(Cylinder, {-8.0f, -20.0f, 28.0f}, {0.075f, 0.075f, 0.54f}, FRotator(22.0f, 0.0f, 38.0f),
                   MonsterFlesh, 0.72f);
        CreatePart(Cylinder, {-8.0f, 20.0f, 28.0f}, {0.075f, 0.075f, 0.54f}, FRotator(-22.0f, 0.0f, -38.0f),
                   MonsterFlesh, 0.72f);
        break;

    case EAshenEntityArchetype::Vanguard:
        VisualHeight = 126.0f;
        EntityMesh->SetStaticMesh(Sphere);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 55.0f});
        EntityMesh->SetRelativeScale3D({0.62f, 0.48f, 0.64f});
        Ashen::Materials::Apply(EntityMesh, this, MonsterFlesh, 0.62f);
        CreatePart(Sphere, {36.0f, 0.0f, 91.0f}, {0.27f, 0.24f, 0.28f}, FRotator::ZeroRotator,
                   MonsterDark, 0.7f);
        for (const float Side : {-1.0f, 1.0f})
        {
            CreatePart(Cylinder, {2.0f, Side * 38.0f, 48.0f}, {0.11f, 0.11f, 0.76f},
                       FRotator(Side * -22.0f, 0.0f, Side * 34.0f), MonsterFlesh, 0.65f);
            CreatePart(Cone, {-8.0f, Side * 28.0f, 101.0f}, {0.16f, 0.16f, 0.58f},
                       FRotator(Side * 15.0f, 0.0f, Side * 22.0f), MonsterBone, 0.78f);
        }
        CreatePart(Cone, {-22.0f, 0.0f, 112.0f}, {0.19f, 0.19f, 0.72f}, FRotator(0.0f, 0.0f, -19.0f),
                   MonsterBone, 0.78f);
        CreatePart(Cube, {18.0f, 0.0f, 48.0f}, {0.20f, 0.56f, 0.16f}, FRotator(0.0f, 16.0f, 0.0f),
                   MonsterBone, 0.76f);
        break;

    case EAshenEntityArchetype::Skirmisher:
        VisualHeight = 120.0f;
        EntityMesh->SetStaticMesh(Cone);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 48.0f});
        EntityMesh->SetRelativeScale3D({0.38f, 0.38f, 0.82f});
        Ashen::Materials::Apply(EntityMesh, this, MonsterDark, 0.76f);
        CreatePart(Sphere, {0.0f, 0.0f, 91.0f}, {0.31f, 0.25f, 0.34f}, FRotator::ZeroRotator,
                   MonsterFlesh, 0.64f);
        CreatePart(Cone, {4.0f, -14.0f, 115.0f}, {0.13f, 0.13f, 0.55f}, FRotator(0.0f, -18.0f, 20.0f),
                   MonsterBone, 0.76f);
        CreatePart(Cone, {4.0f, 14.0f, 115.0f}, {0.13f, 0.13f, 0.55f}, FRotator(0.0f, 18.0f, -20.0f),
                   MonsterBone, 0.76f);
        CreatePart(Cylinder, {28.0f, 0.0f, 74.0f}, {0.085f, 0.085f, 0.88f}, FRotator(0.0f, 90.0f, 90.0f),
                   MonsterBone, 0.68f);
        CreatePart(Sphere, {68.0f, 0.0f, 74.0f}, {0.16f, 0.16f, 0.16f}, FRotator::ZeroRotator,
                   MonsterBlood, 0.34f);
        break;

    case EAshenEntityArchetype::Command:
        VisualHeight = 286.0f;
        EntityMesh->SetStaticMesh(Sphere);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 78.0f});
        EntityMesh->SetRelativeScale3D({1.55f, 1.30f, 0.82f});
        Ashen::Materials::Apply(EntityMesh, this, MonsterFlesh, 0.58f);
        CreatePart(Sphere, {-25.0f, 0.0f, 155.0f}, {0.92f, 0.82f, 0.78f}, FRotator::ZeroRotator,
                   MonsterDark, 0.7f);
        for (int32 Index = 0; Index < 7; ++Index)
        {
            const float Angle = 2.0f * PI * static_cast<float>(Index) / 7.0f;
            const FVector Position(FMath::Cos(Angle) * 82.0f, FMath::Sin(Angle) * 68.0f,
                                   174.0f + static_cast<float>(Index % 2) * 18.0f);
            CreatePart(Cone, Position, {0.28f, 0.28f, 1.35f + static_cast<float>(Index % 3) * 0.22f},
                       FRotator(FMath::Sin(Angle) * 14.0f, FMath::RadiansToDegrees(Angle),
                                FMath::Cos(Angle) * 14.0f),
                       MonsterBone, 0.78f);
        }
        CreatePart(Sphere, {78.0f, 0.0f, 114.0f}, {0.35f, 0.35f, 0.42f}, FRotator::ZeroRotator,
                   MonsterBlood, 0.28f);
        for (const float Side : {-1.0f, 1.0f})
        {
            CreatePart(Cylinder, {35.0f, Side * 63.0f, 111.0f}, {0.10f, 0.10f, 1.25f},
                       FRotator(Side * -30.0f, 0.0f, Side * 24.0f), MonsterBone, 0.8f);
        }
        break;

    case EAshenEntityArchetype::Barracks:
        VisualHeight = 184.0f;
        EntityMesh->SetStaticMesh(Sphere);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 48.0f});
        EntityMesh->SetRelativeScale3D({1.35f, 1.02f, 0.52f});
        Ashen::Materials::Apply(EntityMesh, this, MonsterFlesh, 0.6f);
        CreatePart(Sphere, {-28.0f, 0.0f, 91.0f}, {0.72f, 0.68f, 0.58f}, FRotator::ZeroRotator,
                   MonsterDark, 0.72f);
        for (int32 Index = 0; Index < 5; ++Index)
        {
            const float Side = Index % 2 == 0 ? -1.0f : 1.0f;
            CreatePart(Cone, {-45.0f + static_cast<float>(Index) * 24.0f, Side * 46.0f, 132.0f},
                       {0.22f, 0.22f, 1.05f + static_cast<float>(Index % 3) * 0.16f},
                       FRotator(Side * 10.0f, Index * 21.0f, Side * 12.0f), MonsterBone, 0.8f);
        }
        CreatePart(Sphere, {72.0f, 0.0f, 55.0f}, {0.32f, 0.38f, 0.42f}, FRotator::ZeroRotator,
                   MonsterBlood, 0.3f);
        break;

    case EAshenEntityArchetype::Turret:
        VisualHeight = 205.0f;
        EntityMesh->SetStaticMesh(Cylinder);
        EntityMesh->SetRelativeLocation({0.0f, 0.0f, 78.0f});
        EntityMesh->SetRelativeScale3D({Diameter / 130.0f, Diameter / 130.0f, 1.46f});
        Ashen::Materials::Apply(EntityMesh, this, MonsterFlesh, 0.58f);
        CreatePart(Cone, {0.0f, 0.0f, 181.0f}, {0.46f, 0.46f, 1.15f}, FRotator::ZeroRotator,
                   MonsterBone, 0.74f);
        CreatePart(Sphere, {0.0f, 0.0f, 129.0f}, {0.38f, 0.38f, 0.38f}, FRotator::ZeroRotator,
                   MonsterBlood, 0.3f);
        break;
    }
}

UStaticMeshComponent* AAshenEntityActor::CreatePart(UStaticMesh* Mesh, const FVector& Location, const FVector& Scale,
                                                     const FRotator& Rotation, const FLinearColor& Color,
                                                     const float Roughness)
{
    if (Mesh == nullptr)
    {
        return nullptr;
    }

    const FName PartName(*FString::Printf(TEXT("VisualPart_%d"), DetailMeshes.Num()));
    UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this, PartName);
    AddInstanceComponent(Part);
    Part->SetupAttachment(SceneRoot);
    Part->SetMobility(EComponentMobility::Movable);
    Part->SetStaticMesh(Mesh);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetRelativeLocation(Location);
    Part->SetRelativeRotation(Rotation);
    Part->SetRelativeScale3D(Scale);
    Part->SetCastShadow(true);
    Part->RegisterComponent();
    Ashen::Materials::Apply(Part, this, Color, Roughness);
    DetailMeshes.Add(Part);
    return Part;
}

void AAshenEntityActor::ApplySimulationState(const FVector& GroundPosition, const float HealthFraction,
                                              const float ResolveFraction, const float ConstructionProgress,
                                              const bool bUnderConstruction)
{
    const FVector FlatPosition(GroundPosition.X, GroundPosition.Y, GroundPosition.Z);
    if (!IsBuilding() && bHasGroundPosition)
    {
        const FVector Delta = FlatPosition - LastGroundPosition;
        if (Delta.SizeSquared2D() > 0.25f)
        {
            SetActorRotation(FRotator(0.0f, Delta.Rotation().Yaw, 0.0f));
        }
    }

    SetActorLocation(FlatPosition);
    LastGroundPosition = FlatPosition;
    bHasGroundPosition = true;
    const float NewHealthFraction = FMath::Clamp(HealthFraction, 0.0f, 1.0f);
    if (bHasSimulationState && NewHealthFraction + 0.001f < LastHealthFraction)
    {
        HitFlashStrength = 1.0f;
        SetActorTickEnabled(true);
    }
    bHasSimulationState = true;
    LastHealthFraction = NewHealthFraction;
    const float Resolve = FMath::Clamp(ResolveFraction, 0.0f, 1.0f);
    HealthLightIntensity = FMath::Lerp(90.0f, 420.0f, Resolve) * FMath::Lerp(0.55f, 1.0f, LastHealthFraction);
    ConstructionLightScale = 1.0f;
    if (IsBuilding())
    {
        const float BuildScale = bUnderConstruction
                                     ? FMath::Lerp(0.48f, 1.0f, FMath::Clamp(ConstructionProgress, 0.0f, 1.0f))
                                     : 1.0f;
        SceneRoot->SetRelativeScale3D(FVector(BuildScale));
        ConstructionLightScale = bUnderConstruction ? 0.42f + ConstructionProgress * 0.58f : 1.0f;
    }
    RefreshFactionLight();
    UpdateHealthDisplay(LastHealthFraction);
}

void AAshenEntityActor::SetFogVisible(const bool bVisible)
{
    if (bFogVisible == bVisible)
    {
        return;
    }
    bFogVisible = bVisible;
    SetActorHiddenInGame(!bVisible);
    EntityMesh->SetCollisionEnabled(bVisible ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
    if (!bVisible && bSelected)
    {
        SetSelected(false);
    }
}

void AAshenEntityActor::SetSelected(const bool bInSelected)
{
    bSelected = bInSelected;
    SelectionMarker->SetVisibility(bInSelected && bFogVisible);
    EntityMesh->SetRenderCustomDepth(bInSelected);
    for (UStaticMeshComponent* Part : DetailMeshes)
    {
        Part->SetRenderCustomDepth(bInSelected);
    }
    RefreshFactionLight();
    UpdateHealthDisplay(LastHealthFraction);
}

void AAshenEntityActor::RefreshFactionLight()
{
    const float BaseIntensity = (bSelected ? 1'150.0f : HealthLightIntensity) * ConstructionLightScale;
    FactionLight->SetIntensity(BaseIntensity + HitFlashStrength * 1'450.0f);
    FactionLight->SetLightColor(FLinearColor::LerpUsingHSV(
        FactionBaseColor, FLinearColor(1.0f, 0.72f, 0.32f), HitFlashStrength));
}

void AAshenEntityActor::UpdateHealthDisplay(const float HealthFraction)
{
    const bool bShowHealth = bSelected || HealthFraction < 0.995f;
    HealthBack->SetVisibility(bShowHealth);
    HealthFill->SetVisibility(bShowHealth);
    const float Clamped = FMath::Clamp(HealthFraction, 0.001f, 1.0f);
    HealthFill->SetRelativeScale3D({HealthBarWidth * Clamped / 100.0f, 0.055f, 0.042f});
    HealthFill->SetRelativeLocation({-HealthBarWidth * (1.0f - Clamped) * 0.5f, 0.0f, VisualHeight + 24.5f});
}

bool AAshenEntityActor::IsBuilding() const noexcept
{
    return Archetype == EAshenEntityArchetype::Command || Archetype == EAshenEntityArchetype::Barracks ||
           Archetype == EAshenEntityArchetype::Turret;
}
