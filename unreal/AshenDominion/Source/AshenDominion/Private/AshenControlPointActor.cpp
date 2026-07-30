#include "AshenControlPointActor.h"

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

const FLinearColor RelicStone(0.105f, 0.12f, 0.105f);
const FLinearColor RelicIron(0.035f, 0.041f, 0.038f);
const FLinearColor NeutralRelic(0.31f, 0.34f, 0.28f);
const FLinearColor CompactRelic(0.92f, 0.48f, 0.08f);
const FLinearColor GloamRelic(0.72f, 0.018f, 0.04f);
const FLinearColor ConcordRelic(0.08f, 0.48f, 0.40f);

FLinearColor FactionRelicColor(const EAshenFaction Faction)
{
    switch (Faction)
    {
    case EAshenFaction::Compact:
        return CompactRelic;
    case EAshenFaction::Ascendancy:
        return GloamRelic;
    case EAshenFaction::Concord:
        return ConcordRelic;
    case EAshenFaction::None:
        return NeutralRelic;
    }
    return NeutralRelic;
}
}

AAshenControlPointActor::AAshenControlPointActor()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Movable);

    ShrineBase = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ShrineBase"));
    ShrineBase->SetupAttachment(SceneRoot);
    ShrineBase->SetMobility(EComponentMobility::Movable);
    ShrineBase->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    CaptureDisc = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CaptureDisc"));
    CaptureDisc->SetupAttachment(SceneRoot);
    CaptureDisc->SetMobility(EComponentMobility::Movable);
    CaptureDisc->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    Reliquary = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Reliquary"));
    Reliquary->SetupAttachment(SceneRoot);
    Reliquary->SetMobility(EComponentMobility::Movable);
    Reliquary->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    RelicLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("RelicLight"));
    RelicLight->SetupAttachment(SceneRoot);
    RelicLight->SetCastShadows(false);
    RelicLight->SetAttenuationRadius(430.0f);
    RelicLight->SetIntensity(620.0f);
}

void AAshenControlPointActor::InitializeControlPoint(const int32 InControlPointId, const float Radius)
{
    ControlPointId = InControlPointId;
    UStaticMesh* Cylinder = LoadShape(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh* Cube = LoadShape(TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh* Cone = LoadShape(TEXT("/Engine/BasicShapes/Cone.Cone"));
    UStaticMesh* Sphere = LoadShape(TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    ShrineBase->SetStaticMesh(Cylinder);
    CaptureDisc->SetStaticMesh(Cylinder);
    Reliquary->SetStaticMesh(Sphere);

    const float Diameter = FMath::Max(180.0f, Radius * 2.0f);
    ShrineBase->SetRelativeLocation({0.0f, 0.0f, 9.0f});
    ShrineBase->SetRelativeScale3D({Diameter * 0.34f / 100.0f, Diameter * 0.34f / 100.0f, 0.18f});
    CaptureDisc->SetRelativeLocation({0.0f, 0.0f, 2.4f});
    CaptureDisc->SetRelativeScale3D({Diameter / 100.0f, Diameter / 100.0f, 0.022f});
    Reliquary->SetRelativeLocation({0.0f, 0.0f, 86.0f});
    Reliquary->SetRelativeScale3D({0.30f, 0.30f, 0.38f});

    Ashen::Materials::Apply(ShrineBase, this, RelicStone, 0.92f);
    Ashen::Materials::Apply(Reliquary, this, NeutralRelic, 0.42f);
    RefreshCaptureMaterial(EAshenFaction::None, EAshenFaction::None, 0.0f);

    for (int32 Index = 0; Index < 6; ++Index)
    {
        const float Angle = 2.0f * PI * static_cast<float>(Index) / 6.0f;
        const FVector Position(FMath::Cos(Angle) * 62.0f, FMath::Sin(Angle) * 62.0f, 38.0f);
        CreatePart(Cone, Position, {0.18f, 0.18f, 0.70f},
                   FRotator(10.0f, FMath::RadiansToDegrees(Angle), 0.0f), RelicStone, 0.9f);
    }
    CreatePart(Cube, {0.0f, 0.0f, 42.0f}, {0.28f, 0.28f, 0.74f}, FRotator(0.0f, 45.0f, 0.0f),
               RelicIron, 0.78f);
    CreatePart(Cone, {0.0f, 0.0f, 139.0f}, {0.34f, 0.34f, 0.72f}, FRotator::ZeroRotator,
               RelicStone, 0.88f);

    RelicLight->SetRelativeLocation({0.0f, 0.0f, 112.0f});
    RelicLight->SetLightColor(NeutralRelic);

#if WITH_EDITOR
    SetActorLabel(FString::Printf(TEXT("RelicShrine_%d"), ControlPointId));
#endif
}

void AAshenControlPointActor::ApplySimulationState(const FVector& GroundPosition,
                                                    const EAshenFaction OwnerFaction,
                                                    const EAshenFaction PressureFaction,
                                                    const float Influence, const int32 RuinTide)
{
    SetActorLocation(GroundPosition);
    RefreshCaptureMaterial(OwnerFaction, PressureFaction, Influence);

    const FLinearColor OwnerColor = FactionRelicColor(OwnerFaction);
    RelicLight->SetLightColor(OwnerColor);
    const float Pressure = FMath::Clamp(FMath::Abs(Influence), 0.0f, 1.0f);
    const float TidePulse = 0.72f + static_cast<float>(RuinTide) / 100.0f * 0.42f;
    RelicLight->SetIntensity(FMath::Lerp(420.0f, 1'380.0f, Pressure) * TidePulse);
    Reliquary->SetRelativeScale3D(FVector(0.30f, 0.30f, 0.38f) * FMath::Lerp(0.86f, 1.16f, Pressure));
}

void AAshenControlPointActor::SetFogState(const EAshenVisibility Visibility)
{
    SetActorHiddenInGame(Visibility == EAshenVisibility::Hidden);
    RelicLight->SetVisibility(Visibility == EAshenVisibility::Visible);
}

UStaticMeshComponent* AAshenControlPointActor::CreatePart(UStaticMesh* Mesh, const FVector& Location,
                                                           const FVector& Scale, const FRotator& Rotation,
                                                           const FLinearColor& Color, const float Roughness)
{
    if (Mesh == nullptr)
    {
        return nullptr;
    }
    UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(
        this, FName(*FString::Printf(TEXT("RelicPart_%d"), DetailMeshes.Num())));
    AddInstanceComponent(Part);
    Part->SetupAttachment(SceneRoot);
    Part->SetMobility(EComponentMobility::Movable);
    Part->SetStaticMesh(Mesh);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetRelativeLocation(Location);
    Part->SetRelativeScale3D(Scale);
    Part->SetRelativeRotation(Rotation);
    Part->RegisterComponent();
    Ashen::Materials::Apply(Part, this, Color, Roughness);
    DetailMeshes.Add(Part);
    return Part;
}

void AAshenControlPointActor::RefreshCaptureMaterial(const EAshenFaction OwnerFaction,
                                                      const EAshenFaction PressureFaction,
                                                      const float Influence)
{
    const int32 Bucket = FMath::Clamp(FMath::RoundToInt(FMath::Abs(Influence) * 8.0f), 0, 8);
    if (OwnerFaction == LastOwnerFaction && PressureFaction == LastPressureFaction &&
        Bucket == LastInfluenceBucket)
    {
        return;
    }
    LastOwnerFaction = OwnerFaction;
    LastPressureFaction = PressureFaction;
    LastInfluenceBucket = Bucket;

    const FLinearColor CapturingColor = FactionRelicColor(PressureFaction);
    const float Pressure = static_cast<float>(Bucket) / 8.0f;
    const FLinearColor DiscColor = FLinearColor::LerpUsingHSV(RelicIron, CapturingColor, 0.25f + Pressure * 0.75f);
    Ashen::Materials::Apply(CaptureDisc, this, DiscColor, 0.46f);
    Ashen::Materials::Apply(Reliquary, this, DiscColor, 0.38f);
}
