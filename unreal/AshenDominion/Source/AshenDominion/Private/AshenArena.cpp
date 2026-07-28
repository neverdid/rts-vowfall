#include "AshenArena.h"

#include "AshenEnvironmentKit.h"
#include "AshenMaterials.h"
#include "AshenWorldLayout.h"

#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Math/RandomStream.h"
#include "ProceduralMeshComponent.h"
#include "UObject/UObjectGlobals.h"

namespace
{
constexpr float MapWidth = Ashen::WorldLayout::Width;
constexpr float MapHeight = Ashen::WorldLayout::Height;
constexpr float RiverCenterX = Ashen::WorldLayout::CenterX;
constexpr float RiverWidth = 270.0f;

float SmoothRange(const float Minimum, const float Maximum, const float Value)
{
    const float Alpha =
        FMath::Clamp((Value - Minimum) / FMath::Max(Maximum - Minimum, UE_KINDA_SMALL_NUMBER), 0.0f, 1.0f);
    return Alpha * Alpha * (3.0f - 2.0f * Alpha);
}

float RiverXAt(const float Y)
{
    const float Alpha = FMath::Clamp(Y / MapHeight, 0.0f, 1.0f);
    return RiverCenterX + FMath::Sin(Alpha * 2.0f * PI * 1.10f - 0.35f) * 66.0f +
           FMath::Sin(Alpha * 2.0f * PI * 2.20f + 0.60f) * 17.0f;
}

bool RiverSegmentOverlapsCrossing(const float StartY, const float EndY, const float Clearance)
{
    for (const float CrossingY : {Ashen::WorldLayout::NorthCrossingY, Ashen::WorldLayout::CentralCrossingY,
                                  Ashen::WorldLayout::SouthCrossingY})
    {
        if (EndY >= CrossingY - Clearance && StartY <= CrossingY + Clearance)
        {
            return true;
        }
    }
    return false;
}

const TArray<FVector2D> &DirectRoute()
{
    static const TArray<FVector2D> Route{{600.0f, 1'400.0f}, {1'200.0f, 1'400.0f}, {1'900.0f, 1'400.0f},
                                         {2'400.0f, 1'400.0f}, {3'100.0f, 1'400.0f}, {4'200.0f, 1'400.0f}};
    return Route;
}

const TArray<FVector2D> &NorthRoute()
{
    static const TArray<FVector2D> Route{
        {600.0f, 1'400.0f}, {780.0f, 1'180.0f}, {900.0f, 930.0f}, {900.0f, 650.0f}, {940.0f, 420.0f},
        {1'180.0f, 340.0f}, {1'680.0f, 340.0f}, {2'050.0f, 600.0f}, {2'240.0f, 760.0f},
        {2'560.0f, 760.0f}, {2'780.0f, 650.0f}, {3'140.0f, 760.0f}, {3'580.0f, 960.0f},
        {4'020.0f, 1'180.0f}, {4'200.0f, 1'400.0f},
    };
    return Route;
}

const TArray<FVector2D> &SouthRoute()
{
    static const TArray<FVector2D> Route{
        {600.0f, 1'400.0f}, {780.0f, 1'620.0f}, {1'220.0f, 1'840.0f}, {1'660.0f, 2'040.0f},
        {2'020.0f, 2'150.0f}, {2'240.0f, 2'040.0f}, {2'560.0f, 2'040.0f}, {2'750.0f, 2'200.0f},
        {3'120.0f, 2'460.0f}, {3'620.0f, 2'460.0f}, {3'860.0f, 2'380.0f}, {3'900.0f, 2'150.0f},
        {3'900.0f, 1'870.0f}, {4'020.0f, 1'620.0f}, {4'200.0f, 1'400.0f},
    };
    return Route;
}

float DistanceToSegment(const FVector2D &Point, const FVector2D &Start, const FVector2D &End)
{
    const FVector2D Segment = End - Start;
    const float SegmentLengthSquared = Segment.SizeSquared();
    if (SegmentLengthSquared <= UE_KINDA_SMALL_NUMBER)
    {
        return FVector2D::Distance(Point, Start);
    }
    const float Alpha = FMath::Clamp(FVector2D::DotProduct(Point - Start, Segment) / SegmentLengthSquared, 0.0f, 1.0f);
    return FVector2D::Distance(Point, Start + Segment * Alpha);
}

float DistanceToRoute(const FVector2D &Point, const TArray<FVector2D> &Route)
{
    float Distance = TNumericLimits<float>::Max();
    for (int32 Index = 1; Index < Route.Num(); ++Index)
    {
        Distance = FMath::Min(Distance, DistanceToSegment(Point, Route[Index - 1], Route[Index]));
    }
    return Distance;
}

float EllipseFalloff(const FVector2D &Point, const FVector2D &Center, const FVector2D &Radius)
{
    const float NormalizedX = (Point.X - Center.X) / Radius.X;
    const float NormalizedY = (Point.Y - Center.Y) / Radius.Y;
    return FMath::Exp(-(NormalizedX * NormalizedX + NormalizedY * NormalizedY) * 1.45f);
}

float TerrainHeightAt(const float X, const float Y)
{
    const float ClampedX = FMath::Clamp(X, 0.0f, MapWidth);
    const float ClampedY = FMath::Clamp(Y, 0.0f, MapHeight);
    const FVector2D Point(ClampedX, ClampedY);
    const float DirectDistance = DistanceToRoute(Point, DirectRoute());
    const float FlankDistance = FMath::Min(DistanceToRoute(Point, NorthRoute()), DistanceToRoute(Point, SouthRoute()));
    const float HumanBaseDistance = FVector2D::Distance(Point, {Ashen::WorldLayout::HumanBaseX, MapHeight * 0.5f});
    const float MonsterBaseDistance = FVector2D::Distance(Point, {Ashen::WorldLayout::MonsterBaseX, MapHeight * 0.5f});
    const float ClearingMask =
        FMath::Max(FMath::Max(1.0f - SmoothRange(165.0f, 330.0f, DirectDistance),
                             1.0f - SmoothRange(105.0f, 235.0f, FlankDistance)),
                   FMath::Max(1.0f - SmoothRange(390.0f, 610.0f, HumanBaseDistance),
                              1.0f - SmoothRange(390.0f, 610.0f, MonsterBaseDistance)));

    const float RiverX = RiverXAt(ClampedY);
    const float RiverDistance = FMath::Abs(ClampedX - RiverX);
    const float RiverOutlet =
        1.0f - SmoothRange(RiverWidth * 0.62f, RiverWidth * 1.60f, RiverDistance);
    const float BorderDistance =
        FMath::Min(FMath::Min(ClampedX, MapWidth - ClampedX), FMath::Min(ClampedY, MapHeight - ClampedY));
    const float EdgeRise =
        (1.0f - SmoothRange(95.0f, 520.0f, BorderDistance)) * 155.0f * (1.0f - RiverOutlet * 0.94f);
    const float BroadUndulation = FMath::Sin(ClampedX * 0.0031f + ClampedY * 0.0023f) * 17.0f +
                                  FMath::Sin(ClampedX * 0.0074f - ClampedY * 0.0048f) * 9.0f;
    const float Mountain = EllipseFalloff(Point, {1'140.0f, 760.0f}, {430.0f, 420.0f}) * 245.0f +
                           EllipseFalloff(Point, {1'430.0f, 820.0f}, {510.0f, 470.0f}) * 315.0f +
                           EllipseFalloff(Point, {1'730.0f, 900.0f}, {430.0f, 390.0f}) * 235.0f;
    const float GravewoodRise = EllipseFalloff(Point, {3'380.0f, 2'020.0f}, {700.0f, 540.0f}) * 46.0f;

    const float RiverCut = (1.0f - SmoothRange(RiverWidth * 0.40f, RiverWidth * 0.98f, RiverDistance)) * 46.0f;
    const float Terrain = EdgeRise + (BroadUndulation + Mountain + GravewoodRise) * (1.0f - ClearingMask * 0.94f);
    return Terrain - RiverCut;
}

float RenderTerrainHeightAt(const float X, const float Y)
{
    const float OutsideDistance = FMath::Max(FMath::Max(-X, X - MapWidth), FMath::Max(-Y, Y - MapHeight));
    const float RiverOutlet =
        1.0f - SmoothRange(RiverWidth * 0.62f, RiverWidth * 1.60f, FMath::Abs(X - RiverXAt(Y)));
    const float OutsideRise = SmoothRange(0.0f, 1'050.0f, OutsideDistance) * 310.0f;
    return TerrainHeightAt(X, Y) + OutsideRise * (1.0f - RiverOutlet * 0.94f);
}

Ashen::Materials::FSurfaceStyle SurfaceStyle(const FLinearColor &BaseColor, const FLinearColor &SecondaryColor,
                                             const FLinearColor &AccentColor, const float Roughness,
                                             const float MacroScale = 360.0f, const float DetailScale = 72.0f,
                                             const float DetailStrength = 0.16f, const float Specular = 0.25f)
{
    return {BaseColor,   SecondaryColor, AccentColor, Roughness, MacroScale,
            DetailScale, DetailStrength, Specular,    0.92f};
}

struct FRibbonMeshData
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;
};

TArray<FVector2D> SampleRoute(const TArray<FVector2D> &ControlPoints, const float Spacing)
{
    TArray<FVector2D> Samples;
    if (ControlPoints.IsEmpty())
    {
        return Samples;
    }

    Samples.Add(ControlPoints[0]);
    for (int32 Index = 1; Index < ControlPoints.Num(); ++Index)
    {
        const FVector2D Start = ControlPoints[Index - 1];
        const FVector2D End = ControlPoints[Index];
        const int32 PieceCount =
            FMath::Max(1, FMath::CeilToInt(FVector2D::Distance(Start, End) / Spacing));
        for (int32 Piece = 1; Piece <= PieceCount; ++Piece)
        {
            Samples.Add(FMath::Lerp(Start, End, static_cast<float>(Piece) / static_cast<float>(PieceCount)));
        }
    }

    // A restrained corner pass turns route waypoints into worn bends without moving the
    // strategically authored centerline far enough to alter pathing or bridge approaches.
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        TArray<FVector2D> Smoothed = Samples;
        for (int32 Index = 1; Index < Samples.Num() - 1; ++Index)
        {
            Smoothed[Index] = (Samples[Index - 1] + Samples[Index] * 2.0f + Samples[Index + 1]) * 0.25f;
        }
        Samples = MoveTemp(Smoothed);
    }
    return Samples;
}

bool IsRiverCrossingGap(const FVector2D &Point)
{
    for (const float CrossingY : {Ashen::WorldLayout::NorthCrossingY, Ashen::WorldLayout::CentralCrossingY,
                                  Ashen::WorldLayout::SouthCrossingY})
    {
        if (FMath::Abs(Point.Y - CrossingY) < 118.0f)
        {
            return true;
        }
    }
    return false;
}

template <typename FHeightAt, typename FShouldSkip>
void AppendRibbon(FRibbonMeshData &Mesh, const TArray<FVector2D> &Centerline, const float Width,
                  const float LateralOffset, const float TextureRepeat, FHeightAt HeightAt,
                  FShouldSkip ShouldSkip)
{
    if (Centerline.Num() < 2)
    {
        return;
    }

    const int32 VertexBase = Mesh.Vertices.Num();
    float AccumulatedDistance = 0.0f;
    for (int32 Index = 0; Index < Centerline.Num(); ++Index)
    {
        if (Index > 0)
        {
            AccumulatedDistance += FVector2D::Distance(Centerline[Index - 1], Centerline[Index]);
        }

        const FVector2D BeforeDirection =
            (Centerline[Index] - Centerline[FMath::Max(0, Index - 1)]).GetSafeNormal();
        const FVector2D AfterDirection =
            (Centerline[FMath::Min(Centerline.Num() - 1, Index + 1)] - Centerline[Index]).GetSafeNormal();
        FVector2D Along = (BeforeDirection + AfterDirection).GetSafeNormal();
        if (Along.IsNearlyZero())
        {
            Along = Index == 0 ? AfterDirection : BeforeDirection;
        }

        const FVector2D BeforeNormal(-BeforeDirection.Y, BeforeDirection.X);
        const FVector2D AfterNormal(-AfterDirection.Y, AfterDirection.X);
        FVector2D JoinNormal = (BeforeNormal + AfterNormal).GetSafeNormal();
        if (JoinNormal.IsNearlyZero())
        {
            JoinNormal = FVector2D(-Along.Y, Along.X);
        }

        const float LocalWidth =
            Width * (1.0f + FMath::Sin(static_cast<float>(Index) * 0.73f) * 0.026f +
                     FMath::Sin(static_cast<float>(Index) * 0.21f + 1.4f) * 0.014f);
        const float Denominator =
            FMath::Max(FMath::Abs(FVector2D::DotProduct(JoinNormal, AfterNormal)), 0.48f);
        FVector2D HalfWidth = JoinNormal * (LocalWidth * 0.5f / Denominator);
        HalfWidth = HalfWidth.GetClampedToMaxSize(LocalWidth * 0.72f);
        const FVector2D Center = Centerline[Index] + JoinNormal * LateralOffset;
        const FVector2D Left = Center + HalfWidth;
        const FVector2D Right = Center - HalfWidth;
        const float V = AccumulatedDistance / TextureRepeat;
        const FProcMeshTangent Tangent(FVector(Along.X, Along.Y, 0.0f), false);

        Mesh.Vertices.Emplace(Left.X, Left.Y, HeightAt(Left));
        Mesh.Vertices.Emplace(Right.X, Right.Y, HeightAt(Right));
        Mesh.Normals.Append({FVector::UpVector, FVector::UpVector});
        Mesh.UVs.Append({FVector2D(0.0f, V), FVector2D(1.0f, V)});
        Mesh.VertexColors.Append({FLinearColor::White, FLinearColor::White});
        Mesh.Tangents.Append({Tangent, Tangent});

        if (Index < Centerline.Num() - 1)
        {
            const FVector2D Midpoint = (Centerline[Index] + Centerline[Index + 1]) * 0.5f;
            if (!ShouldSkip(Midpoint))
            {
                const int32 LeftIndex = VertexBase + Index * 2;
                const int32 RightIndex = LeftIndex + 1;
                const int32 NextLeft = LeftIndex + 2;
                const int32 NextRight = LeftIndex + 3;
                Mesh.Triangles.Append(
                    {LeftIndex, NextLeft, RightIndex, RightIndex, NextLeft, NextRight});
            }
        }
    }
}

void CreateRibbonSection(UProceduralMeshComponent *Component, FRibbonMeshData &Mesh)
{
    Component->ClearAllMeshSections();
    Component->CreateMeshSection_LinearColor(0, Mesh.Vertices, Mesh.Triangles, Mesh.Normals, Mesh.UVs,
                                             Mesh.VertexColors, Mesh.Tangents, false, false);
}

void ConfigureInstances(UInstancedStaticMeshComponent *Component, USceneComponent *Parent,
                        const EAshenEnvironmentMeshSlot Slot, UStaticMesh *FallbackMesh,
                        const bool bCastShadow = true)
{
    Component->SetupAttachment(Parent);
    Component->SetMobility(EComponentMobility::Static);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Component->SetCastShadow(bCastShadow);
    Component->SetStaticMesh(Ashen::EnvironmentKit::ResolveMesh(Slot, FallbackMesh));
}

void AddCylinderBetween(UInstancedStaticMeshComponent *Component, const FVector &Start, const FVector &End,
                        const float Radius)
{
    const FVector Delta = End - Start;
    const float Length = Delta.Size();
    if (Length <= UE_KINDA_SMALL_NUMBER)
    {
        return;
    }

    const FQuat Rotation = FQuat::FindBetweenNormals(FVector::UpVector, Delta / Length);
    Component->AddInstance(
        FTransform(Rotation, (Start + End) * 0.5f, FVector(Radius / 50.0f, Radius / 50.0f, Length / 100.0f)));
}

bool IsInGameplayClearing(const FVector2D &Point)
{
    const FVector2D HumanBase(Ashen::WorldLayout::HumanBaseX, Ashen::WorldLayout::CenterY);
    const FVector2D MonsterBase(Ashen::WorldLayout::MonsterBaseX, Ashen::WorldLayout::CenterY);
    if (FVector2D::Distance(Point, HumanBase) < 530.0f || FVector2D::Distance(Point, MonsterBase) < 530.0f)
    {
        return true;
    }

    return DistanceToRoute(Point, DirectRoute()) < 235.0f || DistanceToRoute(Point, NorthRoute()) < 145.0f ||
           DistanceToRoute(Point, SouthRoute()) < 145.0f;
}
} // namespace

AAshenArena::AAshenArena()
{
    PrimaryActorTick.bCanEverTick = false;

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Static);

    UStaticMesh *Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
    UStaticMesh *Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMesh *Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
    UStaticMesh *Cone = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
    UStaticMesh *Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));

    Ground = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Ground"));
    Ground->SetupAttachment(SceneRoot);
    Ground->SetMobility(EComponentMobility::Static);
    Ground->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Ground->SetCollisionResponseToAllChannels(ECR_Block);
    Ground->SetRelativeLocation({MapWidth * 0.5f, MapHeight * 0.5f, 0.0f});
    Ground->SetRelativeScale3D({MapWidth / 100.0f, MapHeight / 100.0f, 1.0f});
    Ground->SetStaticMesh(Plane);
    Ground->SetCastShadow(false);
    Ground->SetVisibility(false);

    Terrain = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainGeometry"));
    Terrain->SetupAttachment(SceneRoot);
    Terrain->SetMobility(EComponentMobility::Static);
    Terrain->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Terrain->SetCastShadow(true);

    RoadSurface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadSurface"));
    RoadSurface->SetupAttachment(SceneRoot);
    RoadSurface->SetMobility(EComponentMobility::Static);
    RoadSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RoadSurface->SetCastShadow(false);

    RoadStoneSurface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadStoneSurface"));
    RoadStoneSurface->SetupAttachment(SceneRoot);
    RoadStoneSurface->SetMobility(EComponentMobility::Static);
    RoadStoneSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RoadStoneSurface->SetCastShadow(false);

    RoadRutSurface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RoadRutSurface"));
    RoadRutSurface->SetupAttachment(SceneRoot);
    RoadRutSurface->SetMobility(EComponentMobility::Static);
    RoadRutSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RoadRutSurface->SetCastShadow(false);

    RiverSurface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RiverSurface"));
    RiverSurface->SetupAttachment(SceneRoot);
    RiverSurface->SetMobility(EComponentMobility::Static);
    RiverSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RiverSurface->SetCastShadow(false);

    RiverShoreSurface = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("RiverShoreSurface"));
    RiverShoreSurface->SetupAttachment(SceneRoot);
    RiverShoreSurface->SetMobility(EComponentMobility::Static);
    RiverShoreSurface->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    RiverShoreSurface->SetCastShadow(false);

    RiverBanks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RiverBanks"));
    Reeds = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Reeds"));
    BridgeTimbers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BridgeTimbers"));
    BridgeIron = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BridgeIron"));
    TreeTrunks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeTrunks"));
    TreeCrowns = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeCrowns"));
    TreeCrownsShadow = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeCrownsShadow"));
    TreeCanopies = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("TreeCanopies"));
    DeadBranches = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("DeadBranches"));
    GrassTufts = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("GrassTufts"));
    Rocks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Rocks"));
    MountainRocks = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MountainRocks"));
    MountainRocksSecondary =
        CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MountainRocksSecondary"));
    MountainRocksTertiary =
        CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MountainRocksTertiary"));
    MineMouths = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MineMouths"));
    MineTimbers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MineTimbers"));
    ForestRoots = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("ForestRoots"));
    HumanWalls = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HumanWalls"));
    HumanTowers = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HumanTowers"));
    HumanRoofs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HumanRoofs"));
    HumanFoundations = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HumanFoundations"));
    HumanTrim = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HumanTrim"));
    HumanBanners = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("HumanBanners"));
    MonsterMasses = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MonsterMasses"));
    MonsterSpikes = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MonsterSpikes"));
    MonsterRibs = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MonsterRibs"));
    MonsterSinew = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MonsterSinew"));
    BonePalisade = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BonePalisade"));
    RitualStones = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("RitualStones"));
    MythicArches = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("MythicArches"));
    BrazierBowls = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("BrazierBowls"));
    EmberCores = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("EmberCores"));

    ConfigureInstances(RiverBanks, SceneRoot, EAshenEnvironmentMeshSlot::RiverBank, Sphere);
    ConfigureInstances(Reeds, SceneRoot, EAshenEnvironmentMeshSlot::Reed, Cone, false);
    ConfigureInstances(BridgeTimbers, SceneRoot, EAshenEnvironmentMeshSlot::BridgeTimber, Cube);
    ConfigureInstances(BridgeIron, SceneRoot, EAshenEnvironmentMeshSlot::BridgeIron, Cube);
    ConfigureInstances(TreeTrunks, SceneRoot, EAshenEnvironmentMeshSlot::TreeTrunk, Cylinder);
    ConfigureInstances(TreeCrowns, SceneRoot, EAshenEnvironmentMeshSlot::TreeCrown, Cone);
    ConfigureInstances(TreeCrownsShadow, SceneRoot, EAshenEnvironmentMeshSlot::TreeCrownShadow, Cone);
    ConfigureInstances(TreeCanopies, SceneRoot, EAshenEnvironmentMeshSlot::TreeCanopy, Sphere);
    ConfigureInstances(DeadBranches, SceneRoot, EAshenEnvironmentMeshSlot::DeadBranch, Cylinder);
    ConfigureInstances(GrassTufts, SceneRoot, EAshenEnvironmentMeshSlot::GrassTuft, Cone, false);
    ConfigureInstances(Rocks, SceneRoot, EAshenEnvironmentMeshSlot::FieldRock, Sphere);
    ConfigureInstances(MountainRocks, SceneRoot, EAshenEnvironmentMeshSlot::MountainRock, Sphere);
    ConfigureInstances(MountainRocksSecondary, SceneRoot,
                       EAshenEnvironmentMeshSlot::MountainRockSecondary, Sphere);
    ConfigureInstances(MountainRocksTertiary, SceneRoot,
                       EAshenEnvironmentMeshSlot::MountainRockTertiary, Sphere);
    ConfigureInstances(MineMouths, SceneRoot, EAshenEnvironmentMeshSlot::MineMouth, Cube);
    ConfigureInstances(MineTimbers, SceneRoot, EAshenEnvironmentMeshSlot::MineTimber, Cylinder);
    ConfigureInstances(ForestRoots, SceneRoot, EAshenEnvironmentMeshSlot::ForestRoot, Cylinder);
    ConfigureInstances(HumanWalls, SceneRoot, EAshenEnvironmentMeshSlot::HumanWall, Cube);
    ConfigureInstances(HumanTowers, SceneRoot, EAshenEnvironmentMeshSlot::HumanTower, Cylinder);
    ConfigureInstances(HumanRoofs, SceneRoot, EAshenEnvironmentMeshSlot::HumanRoof, Cone);
    ConfigureInstances(HumanFoundations, SceneRoot, EAshenEnvironmentMeshSlot::HumanFoundation, Cube);
    ConfigureInstances(HumanTrim, SceneRoot, EAshenEnvironmentMeshSlot::HumanTrim, Cube);
    ConfigureInstances(HumanBanners, SceneRoot, EAshenEnvironmentMeshSlot::HumanBanner, Cube, false);
    ConfigureInstances(MonsterMasses, SceneRoot, EAshenEnvironmentMeshSlot::MonsterMass, Sphere);
    ConfigureInstances(MonsterSpikes, SceneRoot, EAshenEnvironmentMeshSlot::MonsterSpike, Cone);
    ConfigureInstances(MonsterRibs, SceneRoot, EAshenEnvironmentMeshSlot::MonsterRib, Cylinder);
    ConfigureInstances(MonsterSinew, SceneRoot, EAshenEnvironmentMeshSlot::MonsterSinew, Sphere);
    ConfigureInstances(BonePalisade, SceneRoot, EAshenEnvironmentMeshSlot::BonePalisade, Cone);
    ConfigureInstances(RitualStones, SceneRoot, EAshenEnvironmentMeshSlot::RitualStone, Cylinder);
    ConfigureInstances(MythicArches, SceneRoot, EAshenEnvironmentMeshSlot::MythicArch, Cylinder);
    ConfigureInstances(BrazierBowls, SceneRoot, EAshenEnvironmentMeshSlot::BrazierBowl, Cylinder);
    ConfigureInstances(EmberCores, SceneRoot, EAshenEnvironmentMeshSlot::EmberCore, Sphere, false);

    BuildRiver();
    BuildRoadsAndBridges();
    BuildFortifications();
    BuildVegetation();
    BuildLandmarks();

    const TArray<FVector> LightLocations{
        {895.0f, 1'280.0f, 102.0f}, {895.0f, 1'520.0f, 102.0f}, {3'905.0f, 1'290.0f, 92.0f},
        {3'905.0f, 1'510.0f, 92.0f}, {RiverCenterX, Ashen::WorldLayout::CenterY, 84.0f},
    };
    for (int32 Index = 0; Index < LightLocations.Num(); ++Index)
    {
        UPointLightComponent *Light =
            CreateDefaultSubobject<UPointLightComponent>(FName(*FString::Printf(TEXT("AccentLight_%02d"), Index)));
        Light->SetupAttachment(SceneRoot);
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetRelativeLocation(LightLocations[Index]);
        Light->SetAttenuationRadius(Index == 4 ? 410.0f : 285.0f);
        Light->SetIntensity(Index == 4 ? 760.0f : 1'050.0f);
        Light->SetSourceRadius(9.0f);
        Light->SetCastShadows(false);
        if (Index < 2)
        {
            Light->SetLightColor(FLinearColor(1.0f, 0.47f, 0.12f));
        }
        else if (Index < 4)
        {
            Light->SetLightColor(FLinearColor(0.86f, 0.025f, 0.018f));
        }
        else
        {
            Light->SetLightColor(FLinearColor(0.12f, 0.56f, 0.52f));
        }
        AccentLights.Add(Light);
    }

    MoonLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("MoonLight"));
    MoonLight->SetupAttachment(SceneRoot);
    MoonLight->SetRelativeRotation({-56.0f, -32.0f, 0.0f});
    MoonLight->SetLightColor(FLinearColor(0.68f, 0.75f, 0.88f));
    MoonLight->SetIntensity(6.6f);
    MoonLight->SetCastShadows(true);
    MoonLight->SetForwardShadingPriority(1);
    MoonLight->SetLightSourceAngle(4.0f);
    MoonLight->SetShadowAmount(0.52f);
    MoonLight->SetIndirectLightingIntensity(1.25f);
    MoonLight->SetVolumetricScatteringIntensity(0.72f);
    MoonLight->bAtmosphereSunLight = true;

    FillLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("OvercastFillLight"));
    FillLight->SetupAttachment(SceneRoot);
    FillLight->SetRelativeRotation({-52.0f, 146.0f, 0.0f});
    FillLight->SetLightColor(FLinearColor(0.42f, 0.48f, 0.46f));
    FillLight->SetIntensity(1.8f);
    FillLight->SetCastShadows(false);
    FillLight->SetForwardShadingPriority(0);
    FillLight->SetVolumetricScatteringIntensity(0.18f);

    Atmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("Atmosphere"));
    Atmosphere->SetupAttachment(SceneRoot);

    SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
    SkyLight->SetupAttachment(SceneRoot);
    SkyLight->SetMobility(EComponentMobility::Movable);
    SkyLight->SetIntensity(1.42f);
    SkyLight->SetLightColor(FLinearColor(0.66f, 0.70f, 0.72f));

    Fog = CreateDefaultSubobject<UExponentialHeightFogComponent>(TEXT("Fog"));
    Fog->SetupAttachment(SceneRoot);
    Fog->SetFogDensity(0.0085f);
    Fog->SetFogHeightFalloff(0.18f);
    Fog->SetFogInscatteringColor(FLinearColor(0.07f, 0.085f, 0.085f));
    Fog->SetStartDistance(360.0f);
    Fog->SetVolumetricFog(true);

    PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
    PostProcess->SetupAttachment(SceneRoot);
    PostProcess->bUnbound = true;
    PostProcess->Settings.bOverride_VignetteIntensity = true;
    PostProcess->Settings.VignetteIntensity = 0.18f;
    PostProcess->Settings.bOverride_ColorSaturation = true;
    PostProcess->Settings.ColorSaturation = FVector4(0.88f, 0.92f, 0.88f, 1.0f);
    PostProcess->Settings.bOverride_ColorContrast = true;
    PostProcess->Settings.ColorContrast = FVector4(1.04f, 1.04f, 1.03f, 1.0f);
    PostProcess->Settings.bOverride_AutoExposureBias = true;
    PostProcess->Settings.AutoExposureBias = 1.55f;
    PostProcess->Settings.bOverride_BloomIntensity = true;
    PostProcess->Settings.BloomIntensity = 0.24f;
    PostProcess->Settings.bOverride_AmbientOcclusionIntensity = true;
    PostProcess->Settings.AmbientOcclusionIntensity = 0.72f;
    PostProcess->Settings.bOverride_AmbientOcclusionQuality = true;
    PostProcess->Settings.AmbientOcclusionQuality = 85.0f;
}

void AAshenArena::BuildTerrain()
{
    constexpr int32 Columns = 177;
    constexpr int32 Rows = 113;
    constexpr float TerrainMargin = 1'300.0f;
    constexpr float SampleOffset = 18.0f;

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;
    Vertices.Reserve(Columns * Rows);
    Normals.Reserve(Columns * Rows);
    UVs.Reserve(Columns * Rows);
    VertexColors.Reserve(Columns * Rows);
    Tangents.Reserve(Columns * Rows);
    Triangles.Reserve((Columns - 1) * (Rows - 1) * 6);

    for (int32 Row = 0; Row < Rows; ++Row)
    {
        const float Y = -TerrainMargin +
                        static_cast<float>(Row) * (MapHeight + TerrainMargin * 2.0f) / static_cast<float>(Rows - 1);
        for (int32 Column = 0; Column < Columns; ++Column)
        {
            const float X = -TerrainMargin + static_cast<float>(Column) * (MapWidth + TerrainMargin * 2.0f) /
                                                 static_cast<float>(Columns - 1);
            const float Height = RenderTerrainHeightAt(X, Y);
            const float DeltaX =
                RenderTerrainHeightAt(X + SampleOffset, Y) - RenderTerrainHeightAt(X - SampleOffset, Y);
            const float DeltaY =
                RenderTerrainHeightAt(X, Y + SampleOffset) - RenderTerrainHeightAt(X, Y - SampleOffset);
            const FVector Normal(-DeltaX, -DeltaY, SampleOffset * 2.0f);

            Vertices.Emplace(X, Y, Height);
            Normals.Add(Normal.GetSafeNormal());
            UVs.Emplace(X / 520.0f, Y / 520.0f);
            VertexColors.Emplace(0.78f + FMath::Clamp(Height / 320.0f, 0.0f, 0.18f), 0.82f, 0.76f, 1.0f);
            Tangents.Emplace(FVector(1.0f, 0.0f, DeltaX / (SampleOffset * 2.0f)), false);
        }
    }

    for (int32 Row = 0; Row < Rows - 1; ++Row)
    {
        for (int32 Column = 0; Column < Columns - 1; ++Column)
        {
            const int32 A = Row * Columns + Column;
            const int32 B = A + 1;
            const int32 C = A + Columns;
            const int32 D = C + 1;
            Triangles.Append({A, C, B, B, C, D});
        }
    }

    Terrain->ClearAllMeshSections();
    Terrain->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents, false, false);
}

void AAshenArena::BuildRiver()
{
    constexpr int32 SegmentCount = 64;
    constexpr float VisualMargin = 1'300.0f;
    constexpr float RiverStartY = -VisualMargin;
    constexpr float RiverEndY = MapHeight + VisualMargin;
    constexpr float ShoreWidth = 24.0f;
    FRandomStream Random(0x71A3B4C2);

    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    TArray<FLinearColor> VertexColors;
    TArray<FProcMeshTangent> Tangents;
    Vertices.Reserve((SegmentCount + 1) * 2);
    Normals.Reserve((SegmentCount + 1) * 2);
    UVs.Reserve((SegmentCount + 1) * 2);
    VertexColors.Reserve((SegmentCount + 1) * 2);
    Tangents.Reserve((SegmentCount + 1) * 2);
    Triangles.Reserve(SegmentCount * 6);
    TArray<FVector2D> LeftShore;
    TArray<FVector2D> RightShore;
    LeftShore.Reserve(SegmentCount + 1);
    RightShore.Reserve(SegmentCount + 1);

    FVector2D PreviousCenter;
    float AccumulatedDistance = 0.0f;
    for (int32 Index = 0; Index <= SegmentCount; ++Index)
    {
        const float Alpha = static_cast<float>(Index) / static_cast<float>(SegmentCount);
        const float Y = FMath::Lerp(RiverStartY, RiverEndY, Alpha);
        const FVector2D Center(RiverXAt(Y), Y);
        if (Index > 0)
        {
            AccumulatedDistance += FVector2D::Distance(PreviousCenter, Center);
        }
        PreviousCenter = Center;

        constexpr float TangentSample = 8.0f;
        const FVector2D Direction(RiverXAt(Y + TangentSample) - RiverXAt(Y - TangentSample),
                                  TangentSample * 2.0f);
        const FVector2D Along = Direction.GetSafeNormal();
        const FVector2D Across(-Along.Y, Along.X);
        const float Width = RiverWidth * (0.96f + FMath::Sin(Alpha * 2.0f * PI * 3.0f) * 0.045f);
        const FVector2D Left = Center + Across * Width * 0.5f;
        const FVector2D Right = Center - Across * Width * 0.5f;
        LeftShore.Add(Center + Across * (Width * 0.5f + ShoreWidth * 0.5f));
        RightShore.Add(Center - Across * (Width * 0.5f + ShoreWidth * 0.5f));
        const float V = AccumulatedDistance / 420.0f;
        const FProcMeshTangent Tangent(FVector(Along.X, Along.Y, 0.0f), false);

        Vertices.Emplace(Left.X, Left.Y, 4.0f);
        Vertices.Emplace(Right.X, Right.Y, 4.0f);
        Normals.Append({FVector::UpVector, FVector::UpVector});
        UVs.Append({FVector2D(0.0f, V), FVector2D(1.0f, V)});
        VertexColors.Append({FLinearColor::White, FLinearColor::White});
        Tangents.Append({Tangent, Tangent});

        if (Index < SegmentCount)
        {
            const int32 LeftIndex = Index * 2;
            const int32 RightIndex = LeftIndex + 1;
            const int32 NextLeft = LeftIndex + 2;
            const int32 NextRight = LeftIndex + 3;
            Triangles.Append({LeftIndex, RightIndex, NextLeft, RightIndex, NextRight, NextLeft});
        }
    }
    RiverSurface->ClearAllMeshSections();
    RiverSurface->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UVs, VertexColors, Tangents,
                                                false, false);

    FRibbonMeshData ShoreMesh;
    const auto ShoreHeight = [](const FVector2D &Point)
    {
        return FMath::Max(4.35f, TerrainHeightAt(Point.X, Point.Y) + 2.8f);
    };
    AppendRibbon(ShoreMesh, LeftShore, ShoreWidth, 0.0f, 250.0f, ShoreHeight, IsRiverCrossingGap);
    AppendRibbon(ShoreMesh, RightShore, ShoreWidth, 0.0f, 250.0f, ShoreHeight, IsRiverCrossingGap);
    CreateRibbonSection(RiverShoreSurface, ShoreMesh);

    for (int32 Index = 0; Index < SegmentCount; ++Index)
    {
        const float Alpha0 = static_cast<float>(Index) / static_cast<float>(SegmentCount);
        const float Alpha1 = static_cast<float>(Index + 1) / static_cast<float>(SegmentCount);
        const float Y0 = FMath::Lerp(RiverStartY, RiverEndY, Alpha0);
        const float Y1 = FMath::Lerp(RiverStartY, RiverEndY, Alpha1);
        const float X0 = RiverXAt(Y0);
        const float X1 = RiverXAt(Y1);
        const float SegmentWidth = RiverWidth * (0.96f + FMath::Sin(Alpha0 * 2.0f * PI * 3.0f) * 0.045f);
        const FVector2D WaterDelta(X1 - X0, Y1 - Y0);
        const float WaterYaw = FMath::RadiansToDegrees(FMath::Atan2(WaterDelta.Y, WaterDelta.X));
        const FVector2D SegmentDirection = FVector2D(X1 - X0, Y1 - Y0).GetSafeNormal();
        const FVector2D SegmentNormal(-SegmentDirection.Y, SegmentDirection.X);
        const bool bCrossingClearance = RiverSegmentOverlapsCrossing(Y0, Y1, 205.0f);
        const bool bPlayableBank = Y1 >= -80.0f && Y0 <= MapHeight + 80.0f;
        for (int32 Bank = -1; Bank <= 1; Bank += 2)
        {
            const float BankOffset =
                (SegmentWidth * 0.5f + ShoreWidth * 0.76f) * static_cast<float>(Bank);
            const FVector2D BankStart = FVector2D(X0, Y0) + SegmentNormal * BankOffset;
            const FVector2D BankEnd = FVector2D(X1, Y1) + SegmentNormal * BankOffset;
            if (bPlayableBank && !bCrossingClearance)
            {
                if ((Index + (Bank > 0 ? 1 : 0)) % 3 == 0)
                {
                    const FVector2D RockPoint =
                        FMath::Lerp(BankStart, BankEnd, Random.FRandRange(0.24f, 0.76f)) +
                        SegmentNormal * Random.FRandRange(-18.0f, 18.0f);
                    const float RockScale = Random.FRandRange(0.16f, 0.31f);
                    RiverBanks->AddInstance(
                        FTransform(FRotator(Random.FRandRange(-8.0f, 8.0f),
                                            Random.FRandRange(-180.0f, 180.0f),
                                            Random.FRandRange(-6.0f, 6.0f)),
                                   FVector(RockPoint.X, RockPoint.Y,
                                           TerrainHeightAt(RockPoint.X, RockPoint.Y) + 4.0f),
                                   FVector(RockScale, RockScale * Random.FRandRange(0.68f, 1.16f),
                                           RockScale * Random.FRandRange(0.45f, 0.76f))));
                }

                if ((Index + Bank + 1) % 3 == 0)
                {
                    const float Along = Random.FRandRange(0.28f, 0.72f);
                    const FVector2D ReedPoint = FVector2D(X0, Y0) + WaterDelta * Along +
                                                SegmentNormal * (SegmentWidth * 0.53f * static_cast<float>(Bank));
                    Reeds->AddInstance(
                        FTransform(FRotator(0.0f, WaterYaw + Random.FRandRange(-18.0f, 18.0f), 0.0f),
                                   FVector(ReedPoint.X, ReedPoint.Y,
                                           TerrainHeightAt(ReedPoint.X, ReedPoint.Y) + 14.0f),
                                   FVector(0.07f, 0.07f, Random.FRandRange(0.24f, 0.34f))));
                }
            }
        }
    }
}

void AAshenArena::BuildRoadsAndBridges()
{
    const TArray<FVector2D> DirectSamples = SampleRoute(DirectRoute(), 58.0f);
    const TArray<FVector2D> NorthSamples = SampleRoute(NorthRoute(), 42.0f);
    const TArray<FVector2D> SouthSamples = SampleRoute(SouthRoute(), 42.0f);
    const auto NeverSkip = [](const FVector2D &) { return false; };
    const auto DirectHeight = [](const FVector2D &Point, const float GroundOffset,
                                 const float CausewayHeight)
    {
        const float GroundHeight = TerrainHeightAt(Point.X, Point.Y) + GroundOffset;
        const float RiverDistance =
            FMath::Abs(Point.X - RiverXAt(Ashen::WorldLayout::CentralCrossingY));
        const float RiverBlend =
            1.0f - SmoothRange(RiverWidth * 0.58f, RiverWidth * 0.82f, RiverDistance);
        const float LaneBlend =
            1.0f - SmoothRange(82.0f, 158.0f,
                               FMath::Abs(Point.Y - Ashen::WorldLayout::CentralCrossingY));
        return FMath::Lerp(GroundHeight, FMath::Max(GroundHeight, CausewayHeight),
                           RiverBlend * LaneBlend);
    };

    FRibbonMeshData RoadMesh;
    AppendRibbon(
        RoadMesh, DirectSamples, 184.0f, 0.0f, 285.0f,
        [&DirectHeight](const FVector2D &Point) { return DirectHeight(Point, 6.2f, 10.0f); },
        NeverSkip);
    const auto FlankRoadHeight = [](const FVector2D &Point)
    {
        return TerrainHeightAt(Point.X, Point.Y) + 6.2f;
    };
    AppendRibbon(RoadMesh, NorthSamples, 124.0f, 0.0f, 255.0f, FlankRoadHeight,
                 NeverSkip);
    AppendRibbon(RoadMesh, SouthSamples, 124.0f, 0.0f, 255.0f, FlankRoadHeight,
                 NeverSkip);
    CreateRibbonSection(RoadSurface, RoadMesh);

    FRibbonMeshData StoneMesh;
    AppendRibbon(
        StoneMesh, DirectSamples, 132.0f, 0.0f, 220.0f,
        [&DirectHeight](const FVector2D &Point) { return DirectHeight(Point, 7.0f, 15.0f); },
        NeverSkip);
    CreateRibbonSection(RoadStoneSurface, StoneMesh);

    FRibbonMeshData RutMesh;
    const auto RutHeight = [](const FVector2D &Point)
    {
        return TerrainHeightAt(Point.X, Point.Y) + 8.0f;
    };
    for (const float Side : {-1.0f, 1.0f})
    {
        AppendRibbon(RutMesh, NorthSamples, 5.5f, Side * 27.0f, 150.0f, RutHeight,
                     NeverSkip);
        AppendRibbon(RutMesh, SouthSamples, 5.5f, Side * 27.0f, 150.0f, RutHeight,
                     NeverSkip);
    }
    CreateRibbonSection(RoadRutSurface, RutMesh);

    for (const float BridgeY : {Ashen::WorldLayout::NorthCrossingY, Ashen::WorldLayout::SouthCrossingY})
    {
        const float BridgeX = RiverXAt(BridgeY);
        for (int32 Plank = -6; Plank <= 6; ++Plank)
        {
            const float X = BridgeX + static_cast<float>(Plank) * 23.0f;
            BridgeTimbers->AddInstance(
                FTransform(FRotator(0.0f, 0.0f, 0.0f), FVector(X, BridgeY, 18.0f), FVector(0.205f, 1.62f, 0.12f)));
        }
        // Low timber curbs read cleanly from the RTS camera; tall rails looked like spikes through the deck.
        for (const float CurbOffset : {-79.0f, 79.0f})
        {
            BridgeTimbers->AddInstance(FTransform(FRotator::ZeroRotator,
                                                  FVector(BridgeX, BridgeY + CurbOffset, 29.0f),
                                                  FVector(3.35f, 0.055f, 0.08f)));
            for (const float PostOffset : {-142.0f, 142.0f})
            {
                BridgeTimbers->AddInstance(FTransform(FRotator::ZeroRotator,
                                                      FVector(BridgeX + PostOffset, BridgeY + CurbOffset, 35.0f),
                                                      FVector(0.10f, 0.10f, 0.28f)));
            }
        }
    }

    // The main lane crosses on an old low causeway; the flank lanes retain vulnerable timber bridges.
    const float CausewayX = RiverXAt(Ashen::WorldLayout::CentralCrossingY);
    for (const float Side : {-1.0f, 1.0f})
    {
        for (const float Bank : {-1.0f, 1.0f})
        {
            const FVector Marker(CausewayX + Side * RiverWidth * 0.61f,
                                 Ashen::WorldLayout::CentralCrossingY + Bank * 103.0f, 40.0f);
            RitualStones->AddInstance(FTransform(FRotator(0.0f, Side * Bank * 12.0f, 0.0f), Marker,
                                                 FVector(0.16f, 0.16f, 0.52f)));
        }
    }
}

void AAshenArena::BuildFortifications()
{
    constexpr float HumanX = Ashen::WorldLayout::HumanBaseX;
    constexpr float BaseY = Ashen::WorldLayout::CenterY;
    for (const float Y : {1'110.0f, 1'690.0f})
    {
        HumanWalls->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(HumanX - 15.0f, Y, 62.0f), FVector(4.8f, 0.28f, 1.22f)));
        HumanFoundations->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(HumanX - 15.0f, Y, 19.0f), FVector(5.02f, 0.38f, 0.38f)));
        HumanTrim->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(HumanX - 15.0f, Y, 126.0f), FVector(4.9f, 0.34f, 0.10f)));
    }
    HumanWalls->AddInstance(
        FTransform(FRotator::ZeroRotator, FVector(330.0f, BaseY, 62.0f), FVector(0.28f, 5.55f, 1.22f)));
    HumanFoundations->AddInstance(
        FTransform(FRotator::ZeroRotator, FVector(330.0f, BaseY, 19.0f), FVector(0.38f, 5.72f, 0.38f)));
    HumanTrim->AddInstance(
        FTransform(FRotator::ZeroRotator, FVector(330.0f, BaseY, 126.0f), FVector(0.34f, 5.64f, 0.10f)));
    for (const float Y : {1'175.0f, 1'625.0f})
    {
        HumanWalls->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(870.0f, Y, 62.0f), FVector(0.28f, 1.35f, 1.22f)));
        HumanFoundations->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(870.0f, Y, 19.0f), FVector(0.38f, 1.48f, 0.38f)));
    }
    for (const FVector2D Corner : {FVector2D(330.0f, 1'110.0f), FVector2D(330.0f, 1'690.0f),
                                   FVector2D(870.0f, 1'110.0f), FVector2D(870.0f, 1'690.0f)})
    {
        HumanTowers->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(Corner.X, Corner.Y, 95.0f), FVector(0.58f, 0.58f, 1.9f)));
        HumanRoofs->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(Corner.X, Corner.Y, 222.0f), FVector(0.78f, 0.78f, 0.82f)));
        HumanFoundations->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(Corner.X, Corner.Y, 22.0f), FVector(0.72f, 0.72f, 0.44f)));
        HumanTrim->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(Corner.X, Corner.Y, 158.0f), FVector(0.72f, 0.72f, 0.12f)));
    }
    for (int32 Crenel = 0; Crenel < 9; ++Crenel)
    {
        const float X = 210.0f + static_cast<float>(Crenel) * 96.0f;
        for (const float Y : {1'110.0f, 1'690.0f})
        {
            HumanWalls->AddInstance(
                FTransform(FRotator::ZeroRotator, FVector(X, Y, 137.0f), FVector(0.24f, 0.36f, 0.28f)));
        }
    }

    for (const float GateY : {1'275.0f, 1'525.0f})
    {
        HumanTowers->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(870.0f, GateY, 102.0f), FVector(0.49f, 0.49f, 2.04f)));
        HumanRoofs->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(870.0f, GateY, 232.0f), FVector(0.66f, 0.66f, 0.74f)));
        HumanTrim->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(870.0f, GateY, 167.0f), FVector(0.61f, 0.61f, 0.11f)));
        HumanBanners->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(902.0f, GateY, 136.0f), FVector(0.035f, 0.28f, 0.62f)));
        BrazierBowls->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(895.0f, GateY, 72.0f), FVector(0.23f, 0.23f, 0.14f)));
        EmberCores->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(895.0f, GateY, 88.0f), FVector(0.14f, 0.14f, 0.18f)));
    }
    HumanWalls->AddInstance(
        FTransform(FRotator::ZeroRotator, FVector(870.0f, BaseY, 184.0f), FVector(0.33f, 1.02f, 0.18f)));
    HumanTrim->AddInstance(
        FTransform(FRotator::ZeroRotator, FVector(870.0f, BaseY, 171.0f), FVector(0.38f, 1.12f, 0.10f)));

    const FVector2D MonsterBase(Ashen::WorldLayout::MonsterBaseX, BaseY);
    for (int32 Index = 0; Index < 18; ++Index)
    {
        const float Angle = 2.0f * PI * static_cast<float>(Index) / 18.0f;
        const float Radius = Index % 2 == 0 ? 320.0f : 286.0f;
        const FVector Position(MonsterBase.X + FMath::Cos(Angle) * Radius, MonsterBase.Y + FMath::Sin(Angle) * Radius,
                               62.0f + static_cast<float>(Index % 3) * 8.0f);
        const float Facing = FMath::RadiansToDegrees(Angle) + 90.0f;
        BonePalisade->AddInstance(FTransform(FRotator(-8.0f, Facing, 0.0f), Position,
                                             FVector(0.26f, 0.26f, 1.35f + static_cast<float>(Index % 4) * 0.18f)));
    }
    for (int32 Index = 0; Index < 7; ++Index)
    {
        const float Angle = 2.0f * PI * static_cast<float>(Index) / 7.0f;
        const FVector Position(MonsterBase.X + FMath::Cos(Angle) * 210.0f, MonsterBase.Y + FMath::Sin(Angle) * 205.0f,
                               46.0f);
        MonsterMasses->AddInstance(
            FTransform(FRotator(0.0f, Index * 31.0f, 0.0f), Position, FVector(0.9f, 0.62f, 0.48f)));
        MonsterSpikes->AddInstance(FTransform(FRotator(-12.0f, Index * 51.0f, 0.0f),
                                              Position + FVector(0.0f, 0.0f, 100.0f), FVector(0.32f, 0.32f, 1.28f)));
        const FVector RibTop(MonsterBase.X - 38.0f + FMath::Cos(Angle) * 92.0f,
                             MonsterBase.Y + FMath::Sin(Angle) * 78.0f, 176.0f + static_cast<float>(Index % 2) * 24.0f);
        AddCylinderBetween(MonsterRibs, Position + FVector(0.0f, 0.0f, 34.0f), RibTop, 8.0f);
        MonsterSinew->AddInstance(FTransform(FRotator(0.0f, Index * 23.0f, 0.0f), RibTop - FVector(0.0f, 0.0f, 23.0f),
                                             FVector(0.42f, 0.30f, 0.28f)));
    }
    MonsterSinew->AddInstance(FTransform(FRotator::ZeroRotator, FVector(MonsterBase.X - 35.0f, MonsterBase.Y, 132.0f),
                                         FVector(1.18f, 0.86f, 0.72f)));
    for (const float HearthY : {1'290.0f, 1'510.0f})
    {
        BrazierBowls->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(3'905.0f, HearthY, 65.0f), FVector(0.28f, 0.28f, 0.18f)));
        EmberCores->AddInstance(
            FTransform(FRotator::ZeroRotator, FVector(3'905.0f, HearthY, 84.0f), FVector(0.17f, 0.17f, 0.22f)));
    }
}

void AAshenArena::BuildVegetation()
{
    FRandomStream Random(0xA51E2026);

    for (int32 Index = 0; Index < 205; ++Index)
    {
        const FVector2D Point(Random.FRandRange(90.0f, MapWidth - 90.0f), Random.FRandRange(90.0f, MapHeight - 90.0f));
        if (IsInGameplayClearing(Point) || FMath::Abs(Point.X - RiverXAt(Point.Y)) < 230.0f)
        {
            continue;
        }

        const float Height = Random.FRandRange(125.0f, 235.0f);
        const float TrunkRadius = Random.FRandRange(9.0f, 16.0f);
        const float GroundHeight = TerrainHeightAt(Point.X, Point.Y);
        TreeTrunks->AddInstance(
            FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), Random.FRandRange(-5.0f, 5.0f)),
                       FVector(Point.X, Point.Y, GroundHeight + Height * 0.5f),
                       FVector(TrunkRadius / 50.0f, TrunkRadius / 50.0f, Height / 100.0f)));

        const bool bDeadTree = Index % 5 == 0;
        if (!bDeadTree)
        {
            const bool bBroadleaf = Index % 7 == 0;
            if (bBroadleaf)
            {
                const FVector CrownCenter(Point.X, Point.Y, GroundHeight + Height + 24.0f);
                for (int32 Lobe = 0; Lobe < 4; ++Lobe)
                {
                    const float Angle = static_cast<float>(Lobe) * PI * 0.5f + Random.FRandRange(-0.2f, 0.2f);
                    TreeCanopies->AddInstance(
                        FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), Random.FRandRange(-9.0f, 9.0f)),
                                   CrownCenter + FVector(FMath::Cos(Angle) * 34.0f, FMath::Sin(Angle) * 31.0f,
                                                         static_cast<float>(Lobe % 2) * 17.0f),
                                   FVector(Random.FRandRange(0.62f, 0.88f), Random.FRandRange(0.56f, 0.82f),
                                           Random.FRandRange(0.48f, 0.72f))));
                }
            }
            else
            {
                const float CrownWidth = Random.FRandRange(0.76f, 1.12f);
                TreeCrownsShadow->AddInstance(
                    FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f),
                               FVector(Point.X, Point.Y, GroundHeight + Height * 0.73f + 34.0f),
                               FVector(CrownWidth * 1.14f, CrownWidth, Random.FRandRange(1.15f, 1.46f))));
                TreeCrowns->AddInstance(
                    FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f),
                               FVector(Point.X + Random.FRandRange(-5.0f, 5.0f),
                                       Point.Y + Random.FRandRange(-5.0f, 5.0f), GroundHeight + Height + 20.0f),
                               FVector(CrownWidth * 0.88f, CrownWidth * 0.82f, Random.FRandRange(1.22f, 1.62f))));
                TreeCrowns->AddInstance(
                    FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f),
                               FVector(Point.X, Point.Y, GroundHeight + Height + 78.0f),
                               FVector(CrownWidth * 0.58f, CrownWidth * 0.56f, Random.FRandRange(0.72f, 1.02f))));
            }
        }
        else
        {
            const FVector Crown(Point.X, Point.Y, GroundHeight + Height * 0.78f);
            AddCylinderBetween(DeadBranches, Crown, Crown + FVector(52.0f, 22.0f, 55.0f), TrunkRadius * 0.42f);
            AddCylinderBetween(DeadBranches, Crown, Crown + FVector(-44.0f, -30.0f, 42.0f), TrunkRadius * 0.36f);
            AddCylinderBetween(DeadBranches, Crown + FVector(0.0f, 0.0f, 24.0f), Crown + FVector(16.0f, -48.0f, 78.0f),
                               TrunkRadius * 0.28f);
        }
    }

    // Gravewood is a dense sightline landmark, while the authored route remains clear enough for unit reading.
    for (int32 Index = 0; Index < 165; ++Index)
    {
        const FVector2D Point(Random.FRandRange(2'820.0f, 4'120.0f), Random.FRandRange(1'560.0f, 2'660.0f));
        const float ForestMask = FMath::Square((Point.X - 3'430.0f) / 720.0f) +
                                 FMath::Square((Point.Y - 2'080.0f) / 590.0f);
        if (ForestMask > 1.0f || IsInGameplayClearing(Point))
        {
            continue;
        }

        const float GroundHeight = TerrainHeightAt(Point.X, Point.Y);
        const float Height = Random.FRandRange(165.0f, 285.0f);
        const float TrunkRadius = Random.FRandRange(11.0f, 19.0f);
        TreeTrunks->AddInstance(
            FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), Random.FRandRange(-7.0f, 7.0f)),
                       FVector(Point.X, Point.Y, GroundHeight + Height * 0.5f),
                       FVector(TrunkRadius / 50.0f, TrunkRadius / 50.0f, Height / 100.0f)));

        if (Index % 4 == 0)
        {
            const FVector Crown(Point.X, Point.Y, GroundHeight + Height * 0.72f);
            AddCylinderBetween(DeadBranches, Crown, Crown + FVector(62.0f, 28.0f, 66.0f), TrunkRadius * 0.42f);
            AddCylinderBetween(DeadBranches, Crown, Crown + FVector(-54.0f, -36.0f, 51.0f), TrunkRadius * 0.34f);
        }
        else
        {
            const float CrownWidth = Random.FRandRange(0.88f, 1.32f);
            TreeCrownsShadow->AddInstance(
                FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f),
                           FVector(Point.X, Point.Y, GroundHeight + Height * 0.72f + 35.0f),
                           FVector(CrownWidth * 1.20f, CrownWidth * 1.08f, Random.FRandRange(1.28f, 1.70f))));
            TreeCrowns->AddInstance(
                FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), 0.0f),
                           FVector(Point.X, Point.Y, GroundHeight + Height + 38.0f),
                           FVector(CrownWidth, CrownWidth * 0.92f, Random.FRandRange(1.42f, 1.92f))));
        }

        if (Index % 3 == 0)
        {
            const FVector Root(Point.X, Point.Y, GroundHeight + 8.0f);
            for (int32 RootIndex = 0; RootIndex < 3; ++RootIndex)
            {
                const float Angle = Random.FRandRange(0.0f, 2.0f * PI);
                const FVector End = Root + FVector(FMath::Cos(Angle) * Random.FRandRange(48.0f, 82.0f),
                                                   FMath::Sin(Angle) * Random.FRandRange(48.0f, 82.0f), -3.0f);
                AddCylinderBetween(ForestRoots, Root, End, Random.FRandRange(4.0f, 7.0f));
            }
        }
    }

    for (int32 Index = 0; Index < 520; ++Index)
    {
        const FVector2D Point(Random.FRandRange(45.0f, MapWidth - 45.0f), Random.FRandRange(45.0f, MapHeight - 45.0f));
        if (IsInGameplayClearing(Point) || FMath::Abs(Point.X - RiverXAt(Point.Y)) < 175.0f)
        {
            continue;
        }
        GrassTufts->AddInstance(
            FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), Random.FRandRange(-7.0f, 7.0f)),
                       FVector(Point.X, Point.Y, TerrainHeightAt(Point.X, Point.Y) + 10.0f),
                       FVector(Random.FRandRange(0.055f, 0.11f), Random.FRandRange(0.055f, 0.11f),
                               Random.FRandRange(0.16f, 0.34f))));
    }

    for (int32 Index = 0; Index < 70; ++Index)
    {
        const FVector2D Point(Random.FRandRange(60.0f, MapWidth - 60.0f), Random.FRandRange(60.0f, MapHeight - 60.0f));
        if (IsInGameplayClearing(Point) || FMath::Abs(Point.X - RiverXAt(Point.Y)) < 190.0f)
        {
            continue;
        }
        Rocks->AddInstance(
            FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), Random.FRandRange(-18.0f, 18.0f)),
                       FVector(Point.X, Point.Y, TerrainHeightAt(Point.X, Point.Y) + Random.FRandRange(9.0f, 20.0f)),
                       FVector(Random.FRandRange(0.28f, 0.72f), Random.FRandRange(0.22f, 0.58f),
                               Random.FRandRange(0.18f, 0.42f))));
    }
}

void AAshenArena::BuildLandmarks()
{
    FRandomStream Random(0xB1AC4F0D);
    const auto MountainVariant = [this](const int32 Index)
    {
        switch (Index % 3)
        {
        case 1:
            return MountainRocksSecondary.Get();
        case 2:
            return MountainRocksTertiary.Get();
        default:
            return MountainRocks.Get();
        }
    };
    // Blackridge is one continuous landform with a readable cliff spine, not a loose boulder field.
    const TArray<FVector2D> RidgeSpine{{1'030.0f, 770.0f}, {1'150.0f, 805.0f}, {1'275.0f, 830.0f},
                                       {1'405.0f, 850.0f}, {1'535.0f, 875.0f}, {1'665.0f, 905.0f},
                                       {1'790.0f, 940.0f}, {1'900.0f, 980.0f}};
    for (int32 Index = 0; Index < RidgeSpine.Num(); ++Index)
    {
        const FVector2D Point = RidgeSpine[Index];
        const int32 Variant = Index % 3;
        const float Scale = 1.42f + static_cast<float>(Index % 3) * 0.20f;
        FVector InstanceScale(Scale * 1.55f, Scale * 0.95f, Scale * 0.62f);
        float HeightOffset = Scale * 12.0f;
        if (Variant == 1)
        {
            InstanceScale = FVector(Scale, Scale * 0.90f, Scale * 0.68f);
            HeightOffset = Scale * 28.0f;
        }
        else if (Variant == 2)
        {
            InstanceScale = FVector(Scale * 1.15f, Scale * 0.90f, Scale * 0.58f);
            HeightOffset = Scale * 24.0f;
        }
        MountainVariant(Index)->AddInstance(
            FTransform(FRotator(Random.FRandRange(-7.0f, 7.0f), 18.0f + Index * 7.0f,
                                Random.FRandRange(-7.0f, 7.0f)),
                       FVector(Point.X, Point.Y, TerrainHeightAt(Point.X, Point.Y) + HeightOffset),
                       InstanceScale));
    }

    for (int32 Index = 0; Index < 38; ++Index)
    {
        const FVector2D Point(Random.FRandRange(880.0f, 2'040.0f), Random.FRandRange(390.0f, 1'300.0f));
        const float MountainMask = FMath::Square((Point.X - 1'440.0f) / 660.0f) +
                                   FMath::Square((Point.Y - 820.0f) / 520.0f);
        if (MountainMask > 1.0f || IsInGameplayClearing(Point))
        {
            continue;
        }

        const float Scale = Random.FRandRange(0.34f, 0.82f);
        MountainVariant(Index)->AddInstance(
            FTransform(FRotator(Random.FRandRange(-15.0f, 15.0f), Random.FRandRange(0.0f, 180.0f),
                                Random.FRandRange(-10.0f, 10.0f)),
                       FVector(Point.X, Point.Y, TerrainHeightAt(Point.X, Point.Y) + Scale * 28.0f),
                       FVector(Scale, Scale * Random.FRandRange(0.60f, 0.88f),
                               Scale * Random.FRandRange(0.42f, 0.72f))));
    }

    // Two concealed iron adits make the mountain route valuable without opening a free path into either base.
    for (const float MineX : {1'240.0f, 1'580.0f})
    {
        constexpr float MineY = 525.0f;
        const float GroundHeight = TerrainHeightAt(MineX, MineY);
        MineMouths->AddInstance(FTransform(FRotator::ZeroRotator, FVector(MineX, MineY, GroundHeight + 48.0f),
                                           FVector(0.72f, 0.18f, 0.92f)));
        const FVector Left(MineX - 48.0f, MineY - 13.0f, GroundHeight + 8.0f);
        const FVector Right(MineX + 48.0f, MineY - 13.0f, GroundHeight + 8.0f);
        AddCylinderBetween(MineTimbers, Left, Left + FVector(0.0f, 0.0f, 112.0f), 8.0f);
        AddCylinderBetween(MineTimbers, Right, Right + FVector(0.0f, 0.0f, 112.0f), 8.0f);
        AddCylinderBetween(MineTimbers, Left + FVector(0.0f, 0.0f, 106.0f),
                           Right + FVector(0.0f, 0.0f, 106.0f), 9.0f);
        for (int32 RockIndex = 0; RockIndex < 3; ++RockIndex)
        {
            const float Side = RockIndex % 2 == 0 ? -1.0f : 1.0f;
            MountainVariant(RockIndex)->AddInstance(
                FTransform(FRotator(0.0f, Random.FRandRange(0.0f, 180.0f), Random.FRandRange(-18.0f, 18.0f)),
                           FVector(MineX + Side * Random.FRandRange(62.0f, 108.0f),
                                   MineY + Random.FRandRange(-15.0f, 55.0f), GroundHeight + Random.FRandRange(18.0f, 36.0f)),
                           FVector(Random.FRandRange(0.38f, 0.78f), Random.FRandRange(0.30f, 0.62f),
                                   Random.FRandRange(0.28f, 0.56f))));
        }
    }

    // Gravewood's spirit caches are the rotational counterpart to the mountain mines.
    for (const FVector2D Cache : {FVector2D(3'220.0f, 2'440.0f), FVector2D(3'560.0f, 2'460.0f)})
    {
        for (int32 Index = 0; Index < 6; ++Index)
        {
            const float Angle = 2.0f * PI * static_cast<float>(Index) / 6.0f;
            const FVector Position(Cache.X + FMath::Cos(Angle) * 62.0f, Cache.Y + FMath::Sin(Angle) * 48.0f,
                                   TerrainHeightAt(Cache.X, Cache.Y) + 35.0f);
            RitualStones->AddInstance(
                FTransform(FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f), Position, FVector(0.16f, 0.16f, 0.70f)));
        }
    }

    // The Drowned Wayshrine sits off the main lane; no ritual geometry intersects a crossing.
    const FVector2D Shrine(RiverXAt(Ashen::WorldLayout::CenterY) + 330.0f,
                           Ashen::WorldLayout::CenterY - 310.0f);
    const float ShrineGround = TerrainHeightAt(Shrine.X, Shrine.Y);
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const float Angle = 2.0f * PI * static_cast<float>(Index) / 4.0f;
        const FVector Position(Shrine.X + FMath::Cos(Angle) * 72.0f, Shrine.Y + FMath::Sin(Angle) * 54.0f,
                               ShrineGround + 35.0f);
        RitualStones->AddInstance(
            FTransform(FRotator(0.0f, FMath::RadiansToDegrees(Angle), 0.0f), Position, FVector(0.16f, 0.16f, 0.58f)));
    }
    MythicArches->AddInstance(FTransform(FRotator(0.0f, 18.0f, -4.0f),
                                         FVector(Shrine.X - 44.0f, Shrine.Y, ShrineGround + 55.0f),
                                         FVector(0.36f, 0.30f, 1.04f)));
    MythicArches->AddInstance(FTransform(FRotator(0.0f, -12.0f, 6.0f),
                                         FVector(Shrine.X + 46.0f, Shrine.Y + 8.0f, ShrineGround + 48.0f),
                                         FVector(0.32f, 0.28f, 0.92f)));
    BrazierBowls->AddInstance(
        FTransform(FRotator::ZeroRotator, FVector(Shrine.X, Shrine.Y, ShrineGround + 29.0f),
                   FVector(0.36f, 0.36f, 0.20f)));
    EmberCores->AddInstance(
        FTransform(FRotator::ZeroRotator, FVector(Shrine.X, Shrine.Y, ShrineGround + 51.0f),
                   FVector(0.19f, 0.19f, 0.25f)));
}

void AAshenArena::BeginPlay()
{
    Super::BeginPlay();

    BuildTerrain();
    SkyLight->RecaptureSky();

    auto Moor = SurfaceStyle({0.050f, 0.074f, 0.050f}, {0.105f, 0.118f, 0.078f}, {0.160f, 0.176f, 0.105f}, 0.96f,
                             510.0f, 88.0f, 0.14f, 0.18f);
    auto MoorPatch = SurfaceStyle({0.074f, 0.105f, 0.064f}, {0.13f, 0.15f, 0.085f}, {0.19f, 0.20f, 0.11f}, 0.98f,
                                  260.0f, 54.0f, 0.17f, 0.14f);
    auto Mud = SurfaceStyle({0.095f, 0.066f, 0.040f}, {0.17f, 0.12f, 0.070f}, {0.25f, 0.205f, 0.135f}, 0.94f,
                            220.0f, 42.0f, 0.22f, 0.18f);
    auto RoadStone = SurfaceStyle({0.090f, 0.088f, 0.076f}, {0.155f, 0.145f, 0.118f},
                                  {0.225f, 0.205f, 0.160f}, 0.94f, 120.0f, 30.0f, 0.20f, 0.20f);
    auto WetStone = SurfaceStyle({0.075f, 0.086f, 0.080f}, {0.145f, 0.15f, 0.135f}, {0.23f, 0.23f, 0.195f}, 0.80f,
                                 135.0f, 32.0f, 0.19f, 0.34f);
    auto WeatheredWood = SurfaceStyle({0.090f, 0.047f, 0.024f}, {0.17f, 0.095f, 0.045f},
                                      {0.28f, 0.18f, 0.095f}, 0.86f, 90.0f, 21.0f, 0.18f, 0.22f);
    const auto DarkIron = SurfaceStyle({0.055f, 0.060f, 0.058f}, {0.14f, 0.145f, 0.135f}, {0.24f, 0.23f, 0.20f}, 0.44f,
                                       95.0f, 24.0f, 0.12f, 0.58f);
    auto MineDark = SurfaceStyle({0.004f, 0.005f, 0.005f}, {0.012f, 0.014f, 0.013f}, {0.025f, 0.026f, 0.023f},
                                 0.98f, 80.0f, 19.0f, 0.08f, 0.08f);
    const auto Bark = SurfaceStyle({0.050f, 0.028f, 0.015f}, {0.105f, 0.060f, 0.030f}, {0.18f, 0.115f, 0.055f}, 0.98f,
                                   75.0f, 18.0f, 0.20f, 0.14f);
    const auto Pine = SurfaceStyle({0.020f, 0.058f, 0.030f}, {0.045f, 0.105f, 0.052f}, {0.10f, 0.16f, 0.085f}, 0.99f,
                                   105.0f, 25.0f, 0.16f, 0.12f);
    const auto PineShadow = SurfaceStyle({0.012f, 0.030f, 0.018f}, {0.025f, 0.067f, 0.034f}, {0.055f, 0.105f, 0.055f},
                                         0.99f, 115.0f, 28.0f, 0.13f, 0.10f);
    auto HumanStone = SurfaceStyle({0.16f, 0.17f, 0.16f}, {0.27f, 0.27f, 0.235f}, {0.38f, 0.36f, 0.29f}, 0.90f,
                                   130.0f, 29.0f, 0.19f, 0.25f);
    auto FoundationStone = SurfaceStyle({0.075f, 0.078f, 0.073f}, {0.15f, 0.15f, 0.135f},
                                        {0.23f, 0.22f, 0.18f}, 0.95f, 110.0f, 27.0f, 0.22f, 0.18f);
    const auto HumanRoof = SurfaceStyle({0.14f, 0.025f, 0.018f}, {0.26f, 0.050f, 0.025f}, {0.40f, 0.095f, 0.040f},
                                        0.76f, 95.0f, 22.0f, 0.16f, 0.30f);
    const auto Flesh = SurfaceStyle({0.09f, 0.008f, 0.014f}, {0.19f, 0.018f, 0.025f}, {0.34f, 0.035f, 0.042f}, 0.68f,
                                    115.0f, 24.0f, 0.24f, 0.30f);
    const auto Bone = SurfaceStyle({0.29f, 0.25f, 0.18f}, {0.45f, 0.39f, 0.28f}, {0.61f, 0.54f, 0.40f}, 0.88f, 100.0f,
                                   22.0f, 0.17f, 0.20f);
    auto MythicStone = SurfaceStyle({0.030f, 0.042f, 0.042f}, {0.075f, 0.105f, 0.095f},
                                    {0.13f, 0.205f, 0.17f}, 0.82f, 145.0f, 31.0f, 0.19f, 0.36f);

    Moor.TextureTint = {0.58f, 0.66f, 0.53f, 1.0f};
    Moor.TextureBlend = 0.68f;
    Moor.TextureTiling = 0.92f;
    Moor.NormalStrength = 0.48f;
    MoorPatch.TextureTint = {0.48f, 0.58f, 0.42f, 1.0f};
    MoorPatch.TextureBlend = 0.62f;
    Mud.TextureTint = {0.47f, 0.43f, 0.36f, 1.0f};
    Mud.TextureBlend = 0.56f;
    Mud.TextureTiling = 0.88f;
    Mud.NormalStrength = 0.58f;
    RoadStone.TextureTint = {0.56f, 0.58f, 0.54f, 1.0f};
    RoadStone.TextureBlend = 0.64f;
    RoadStone.NormalStrength = 0.72f;
    WetStone.TextureTint = {0.48f, 0.57f, 0.54f, 1.0f};
    WetStone.TextureBlend = 0.72f;
    WeatheredWood.TextureTint = {0.64f, 0.50f, 0.34f, 1.0f};
    WeatheredWood.TextureBlend = 0.82f;
    WeatheredWood.NormalStrength = 0.74f;
    MineDark.TextureTint = {0.20f, 0.22f, 0.20f, 1.0f};
    MineDark.TextureBlend = 0.46f;
    HumanStone.TextureTint = {0.78f, 0.77f, 0.68f, 1.0f};
    HumanStone.TextureBlend = 0.67f;
    FoundationStone.TextureTint = {0.53f, 0.55f, 0.50f, 1.0f};
    FoundationStone.TextureBlend = 0.72f;
    auto BlackridgeStone = FoundationStone;
    BlackridgeStone.BaseColor = {0.105f, 0.11f, 0.105f, 1.0f};
    BlackridgeStone.SecondaryColor = {0.19f, 0.19f, 0.17f, 1.0f};
    BlackridgeStone.AccentColor = {0.30f, 0.285f, 0.23f, 1.0f};
    BlackridgeStone.Specular = 0.12f;
    BlackridgeStone.TextureTint = {0.72f, 0.74f, 0.68f, 1.0f};
    BlackridgeStone.TextureBlend = 0.60f;
    BlackridgeStone.NormalStrength = 0.76f;
    BlackridgeStone.PackedStrength = 0.62f;
    MythicStone.TextureTint = {0.42f, 0.56f, 0.52f, 1.0f};
    MythicStone.TextureBlend = 0.58f;
    auto RiverMud = Mud;
    RiverMud.Roughness = 0.88f;
    RiverMud.TextureTint = {0.23f, 0.24f, 0.20f, 1.0f};
    RiverMud.TextureBlend = 0.28f;
    RiverMud.TextureTiling = 0.65f;
    auto RutMud = Mud;
    RutMud.TextureTint = {0.23f, 0.19f, 0.15f, 1.0f};
    RutMud.TextureBlend = 0.58f;
    RutMud.NormalStrength = 0.42f;

    Ashen::Materials::ApplySurface(Terrain, this, Moor, EAshenEnvironmentSurface::Moor);
    Ashen::Materials::ApplySurface(RoadSurface, this, Mud, EAshenEnvironmentSurface::Mud);
    Ashen::Materials::ApplySurface(RoadStoneSurface, this, RoadStone,
                                   EAshenEnvironmentSurface::RoadStone);
    Ashen::Materials::ApplySurface(RoadRutSurface, this, RutMud, EAshenEnvironmentSurface::Mud);
    Ashen::Materials::ApplySurface(RiverShoreSurface, this, RiverMud,
                                   EAshenEnvironmentSurface::Mud);
    Ashen::Materials::ApplySurface(RiverBanks, this, WetStone,
                                   EAshenEnvironmentSurface::WetStone);
    Ashen::Materials::ApplySurface(Reeds, this, Pine, EAshenEnvironmentSurface::Pine);
    Ashen::Materials::ApplySurface(BridgeTimbers, this, WeatheredWood,
                                   EAshenEnvironmentSurface::WeatheredWood);
    Ashen::Materials::ApplySurface(BridgeIron, this, DarkIron, EAshenEnvironmentSurface::DarkIron);
    Ashen::Materials::ApplySurface(TreeTrunks, this, Bark, EAshenEnvironmentSurface::Bark);
    Ashen::Materials::ApplySurface(TreeCrowns, this, Pine, EAshenEnvironmentSurface::Pine);
    Ashen::Materials::ApplySurface(TreeCrownsShadow, this, PineShadow, EAshenEnvironmentSurface::PineShadow);
    Ashen::Materials::ApplySurface(TreeCanopies, this, Pine, EAshenEnvironmentSurface::Pine);
    Ashen::Materials::ApplySurface(DeadBranches, this, Bark, EAshenEnvironmentSurface::Bark);
    Ashen::Materials::ApplySurface(GrassTufts, this, MoorPatch, EAshenEnvironmentSurface::MoorPatch);
    Ashen::Materials::ApplySurface(Rocks, this, WetStone, EAshenEnvironmentSurface::WetStone);
    Ashen::Materials::ApplySurface(MountainRocks, this, BlackridgeStone,
                                   EAshenEnvironmentSurface::FoundationStone);
    Ashen::Materials::ApplySurface(MountainRocksSecondary, this, BlackridgeStone,
                                   EAshenEnvironmentSurface::FoundationStone);
    Ashen::Materials::ApplySurface(MountainRocksTertiary, this, BlackridgeStone,
                                   EAshenEnvironmentSurface::FoundationStone);
    Ashen::Materials::ApplySurface(MineMouths, this, MineDark, EAshenEnvironmentSurface::MineDark);
    Ashen::Materials::ApplySurface(MineTimbers, this, WeatheredWood,
                                   EAshenEnvironmentSurface::WeatheredWood);
    Ashen::Materials::ApplySurface(ForestRoots, this, Bark, EAshenEnvironmentSurface::Bark);
    Ashen::Materials::ApplySurface(HumanWalls, this, HumanStone, EAshenEnvironmentSurface::HumanStone);
    Ashen::Materials::ApplySurface(HumanTowers, this, HumanStone, EAshenEnvironmentSurface::HumanStone);
    Ashen::Materials::ApplySurface(HumanRoofs, this, HumanRoof, EAshenEnvironmentSurface::HumanRoof);
    Ashen::Materials::ApplySurface(HumanFoundations, this, FoundationStone,
                                   EAshenEnvironmentSurface::FoundationStone);
    Ashen::Materials::ApplySurface(HumanTrim, this, DarkIron, EAshenEnvironmentSurface::DarkIron);
    Ashen::Materials::ApplySurface(HumanBanners, this, HumanRoof, EAshenEnvironmentSurface::HumanRoof);
    Ashen::Materials::ApplySurface(MonsterMasses, this, Flesh, EAshenEnvironmentSurface::Flesh);
    Ashen::Materials::ApplySurface(MonsterSpikes, this, Flesh, EAshenEnvironmentSurface::Flesh);
    Ashen::Materials::ApplySurface(MonsterRibs, this, Bone, EAshenEnvironmentSurface::Bone);
    Ashen::Materials::ApplySurface(MonsterSinew, this, Flesh, EAshenEnvironmentSurface::Flesh);
    Ashen::Materials::ApplySurface(BonePalisade, this, Bone, EAshenEnvironmentSurface::Bone);
    Ashen::Materials::ApplySurface(RitualStones, this, MythicStone, EAshenEnvironmentSurface::MythicStone);
    Ashen::Materials::ApplySurface(MythicArches, this, MythicStone, EAshenEnvironmentSurface::MythicStone);
    Ashen::Materials::ApplySurface(BrazierBowls, this, DarkIron, EAshenEnvironmentSurface::DarkIron);
    Ashen::Materials::Apply(EmberCores, this, FLinearColor(0.78f, 0.09f, 0.025f), 0.16f);

    Ashen::Materials::ApplyWater(RiverSurface, this, FLinearColor(0.045f, 0.19f, 0.16f),
                                 FLinearColor(0.006f, 0.048f, 0.050f), 0.72f);
}
