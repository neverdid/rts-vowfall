#pragma once

#include "Engine/DeveloperSettings.h"
#include "AshenEnvironmentKit.generated.h"

class UStaticMesh;
class UTexture2D;

UENUM()
enum class EAshenEnvironmentMeshSlot : uint8
{
    Roadbed,
    RoadStone,
    RoadRut,
    RiverBank,
    Reed,
    BridgeTimber,
    BridgeIron,
    TreeTrunk,
    TreeCrown,
    TreeCrownShadow,
    TreeCanopy,
    DeadBranch,
    GrassTuft,
    FieldRock,
    MountainRock,
    MineMouth,
    MineTimber,
    ForestRoot,
    HumanWall,
    HumanTower,
    HumanRoof,
    HumanFoundation,
    HumanTrim,
    HumanBanner,
    MonsterMass,
    MonsterSpike,
    MonsterRib,
    MonsterSinew,
    BonePalisade,
    RitualStone,
    MythicArch,
    BrazierBowl,
    EmberCore,
    Count UMETA(Hidden),
};

UENUM()
enum class EAshenEnvironmentSurface : uint8
{
    None,
    Moor,
    MoorPatch,
    Mud,
    RoadStone,
    WetStone,
    WeatheredWood,
    DarkIron,
    MineDark,
    Bark,
    Pine,
    PineShadow,
    HumanStone,
    FoundationStone,
    HumanRoof,
    Flesh,
    Bone,
    MythicStone,
    Count UMETA(Hidden),
};

UCLASS(Config = Game, DefaultConfig, meta = (DisplayName = "Vowfall Environment Kit"))
class ASHENDOMINION_API UAshenEnvironmentKitSettings final : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UPROPERTY(Config, EditAnywhere, Category = "Content")
    bool bEnableProductionMeshes = true;

    UPROPERTY(Config, EditAnywhere, Category = "Content")
    bool bEnableProductionTextures = true;

    UPROPERTY(Config, EditAnywhere, Category = "Content", meta = (ContentDir))
    FString ProductionContentRoot = TEXT("/Game/External/VowfallEnvironmentKit");
};

namespace Ashen::EnvironmentKit
{
struct FMeshSpec
{
    EAshenEnvironmentMeshSlot Slot;
    const TCHAR *DisplayName;
    const TCHAR *RelativePackagePath;
    FVector NominalSizeCm;
    int32 MinimumLodCount;
    bool bNaniteCandidate;
    bool bProjectTitanCandidate;
};

struct FSurfaceSpec
{
    EAshenEnvironmentSurface Slot;
    const TCHAR *DisplayName;
    const TCHAR *RelativeTexturePrefix;
    float NominalTileSizeCm;
    bool bProjectTitanCandidate;
};

struct FSurfaceTextures
{
    UTexture2D *Albedo = nullptr;
    UTexture2D *Normal = nullptr;
    UTexture2D *Packed = nullptr;

    [[nodiscard]] bool HasAny() const
    {
        return Albedo != nullptr || Normal != nullptr || Packed != nullptr;
    }
};

ASHENDOMINION_API TConstArrayView<FMeshSpec> MeshSpecs();
ASHENDOMINION_API TConstArrayView<FSurfaceSpec> SurfaceSpecs();
ASHENDOMINION_API FString ObjectPath(const FMeshSpec &Spec);
ASHENDOMINION_API FString SourceObjectPath(const FMeshSpec &Spec);
ASHENDOMINION_API FString TextureObjectPath(const FSurfaceSpec &Spec, const TCHAR *Suffix);
ASHENDOMINION_API UStaticMesh *FindProductionMesh(EAshenEnvironmentMeshSlot Slot);
ASHENDOMINION_API UStaticMesh *FindSourceMesh(EAshenEnvironmentMeshSlot Slot);
ASHENDOMINION_API UStaticMesh *ResolveMesh(EAshenEnvironmentMeshSlot Slot, UStaticMesh *Fallback);
ASHENDOMINION_API EAshenEnvironmentSurface SurfaceFallback(EAshenEnvironmentSurface Slot) noexcept;
ASHENDOMINION_API FSurfaceTextures ResolveSurfaceTextures(EAshenEnvironmentSurface Slot);
} // namespace Ashen::EnvironmentKit
