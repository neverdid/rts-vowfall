#include "AshenEnvironmentKit.h"

#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Misc/PackageName.h"
#include "UObject/UObjectGlobals.h"

namespace
{
using Ashen::EnvironmentKit::FMeshSpec;
using Ashen::EnvironmentKit::FSurfaceSpec;

const TArray<FMeshSpec> &AllMeshSpecs()
{
    static const TArray<FMeshSpec> Specs{
        {EAshenEnvironmentMeshSlot::Roadbed, TEXT("Road bed"), TEXT("Meshes/Road/SM_RoadBed_A"), {100, 100, 100}, 3,
         true, false},
        {EAshenEnvironmentMeshSlot::RoadStone, TEXT("Road stone"), TEXT("Meshes/Road/SM_RoadStone_A"), {100, 100, 100},
         3, true, true},
        {EAshenEnvironmentMeshSlot::RoadRut, TEXT("Road rut"), TEXT("Meshes/Road/SM_RoadRut_A"), {100, 100, 100}, 2,
         false, false},
        {EAshenEnvironmentMeshSlot::RiverBank, TEXT("River-bank rock"), TEXT("Meshes/Rock/SM_RiverBank_A"),
         {100, 100, 100}, 3, true, true},
        {EAshenEnvironmentMeshSlot::Reed, TEXT("River reed"), TEXT("Meshes/Foliage/SM_Reed_A"), {100, 100, 100}, 3,
         false, true},
        {EAshenEnvironmentMeshSlot::BridgeTimber, TEXT("Bridge timber"), TEXT("Meshes/Architecture/SM_BridgeTimber_A"),
         {100, 100, 100}, 3, true, true},
        {EAshenEnvironmentMeshSlot::BridgeIron, TEXT("Bridge iron"), TEXT("Meshes/Architecture/SM_BridgeIron_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::TreeTrunk, TEXT("Tree trunk"), TEXT("Meshes/Foliage/SM_TreeTrunk_A"),
         {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::TreeCrown, TEXT("Conifer crown"), TEXT("Meshes/Foliage/SM_ConiferCrown_A"),
         {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::TreeCrownShadow, TEXT("Conifer shadow crown"),
         TEXT("Meshes/Foliage/SM_ConiferCrownShadow_A"), {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::TreeCanopy, TEXT("Broadleaf canopy"), TEXT("Meshes/Foliage/SM_BroadleafCanopy_A"),
         {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::DeadBranch, TEXT("Dead branch"), TEXT("Meshes/Foliage/SM_DeadBranch_A"),
         {100, 100, 100}, 3, false, true},
        {EAshenEnvironmentMeshSlot::GrassTuft, TEXT("Grass tuft"), TEXT("Meshes/Foliage/SM_GrassTuft_A"),
         {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::FieldRock, TEXT("Field rock"), TEXT("Meshes/Rock/SM_FieldRock_A"), {100, 100, 100},
         3, true, true},
        {EAshenEnvironmentMeshSlot::MountainRock, TEXT("Mountain cliff"), TEXT("Meshes/Rock/SM_MountainCliff_A"),
         {100, 100, 100}, 5, false, true},
        {EAshenEnvironmentMeshSlot::MountainRockSecondary, TEXT("Mountain cliff secondary"),
         TEXT("Meshes/Rock/SM_MountainCliff_B"), {100, 100, 100}, 5, false, true},
        {EAshenEnvironmentMeshSlot::MountainRockTertiary, TEXT("Mountain cliff tertiary"),
         TEXT("Meshes/Rock/SM_MountainCliff_C"), {100, 100, 100}, 5, false, true},
        {EAshenEnvironmentMeshSlot::MineMouth, TEXT("Mine mouth"), TEXT("Meshes/Architecture/SM_MineMouth_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::MineTimber, TEXT("Mine timber"), TEXT("Meshes/Architecture/SM_MineTimber_A"),
         {100, 100, 100}, 3, true, true},
        {EAshenEnvironmentMeshSlot::ForestRoot, TEXT("Forest root"), TEXT("Meshes/Foliage/SM_ForestRoot_A"),
         {100, 100, 100}, 3, true, true},
        {EAshenEnvironmentMeshSlot::GravewoodTreeA, TEXT("Gravewood tree A"),
         TEXT("Meshes/Foliage/SM_GravewoodTree_A"), {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::GravewoodTreeB, TEXT("Gravewood tree B"),
         TEXT("Meshes/Foliage/SM_GravewoodTree_B"), {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::GravewoodStump, TEXT("Gravewood stump"),
         TEXT("Meshes/Foliage/SM_GravewoodStump_A"), {100, 100, 100}, 4, false, true},
        {EAshenEnvironmentMeshSlot::GravewoodRootA, TEXT("Gravewood root A"),
         TEXT("Meshes/Foliage/SM_GravewoodRoot_A"), {100, 100, 100}, 1, false, true},
        {EAshenEnvironmentMeshSlot::GravewoodRootB, TEXT("Gravewood root B"),
         TEXT("Meshes/Foliage/SM_GravewoodRoot_B"), {100, 100, 100}, 1, false, true},
        {EAshenEnvironmentMeshSlot::HumanWall, TEXT("Human wall"), TEXT("Meshes/Factions/Human/SM_HumanWall_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::HumanTower, TEXT("Human tower"), TEXT("Meshes/Factions/Human/SM_HumanTower_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::HumanRoof, TEXT("Human roof"), TEXT("Meshes/Factions/Human/SM_HumanRoof_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::HumanFoundation, TEXT("Human foundation"),
         TEXT("Meshes/Factions/Human/SM_HumanFoundation_A"), {100, 100, 100}, 3, true, true},
        {EAshenEnvironmentMeshSlot::HumanTrim, TEXT("Human iron trim"), TEXT("Meshes/Factions/Human/SM_HumanTrim_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::HumanBanner, TEXT("Human banner"), TEXT("Meshes/Factions/Human/SM_HumanBanner_A"),
         {100, 100, 100}, 4, false, false},
        {EAshenEnvironmentMeshSlot::MonsterMass, TEXT("Monster mass"), TEXT("Meshes/Factions/Veiled/SM_Mass_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::MonsterSpike, TEXT("Monster spike"), TEXT("Meshes/Factions/Veiled/SM_Spike_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::MonsterRib, TEXT("Monster rib"), TEXT("Meshes/Factions/Veiled/SM_Rib_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::MonsterSinew, TEXT("Monster sinew"), TEXT("Meshes/Factions/Veiled/SM_Sinew_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::BonePalisade, TEXT("Bone palisade"),
         TEXT("Meshes/Factions/Veiled/SM_BonePalisade_A"), {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::RitualStone, TEXT("Ritual stone"), TEXT("Meshes/Mythic/SM_RitualStone_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::MythicArch, TEXT("Mythic arch"), TEXT("Meshes/Mythic/SM_MythicArch_A"),
         {100, 100, 100}, 3, true, false},
        {EAshenEnvironmentMeshSlot::BrazierBowl, TEXT("Brazier bowl"), TEXT("Meshes/Props/SM_BrazierBowl_A"),
         {100, 100, 100}, 3, true, true},
        {EAshenEnvironmentMeshSlot::EmberCore, TEXT("Ember core"), TEXT("Meshes/VFX/SM_EmberCore_A"),
         {100, 100, 100}, 2, false, false},
    };
    return Specs;
}

const TArray<FSurfaceSpec> &AllSurfaceSpecs()
{
    static const TArray<FSurfaceSpec> Specs{
        {EAshenEnvironmentSurface::Moor, TEXT("Moor ground"), TEXT("Textures/Ground/T_Moor"), 520.0f, true},
        {EAshenEnvironmentSurface::MoorPatch, TEXT("Moor vegetation"), TEXT("Textures/Ground/T_MoorPatch"), 260.0f,
         true},
        {EAshenEnvironmentSurface::Mud, TEXT("Road mud"), TEXT("Textures/Ground/T_Mud"), 220.0f, true},
        {EAshenEnvironmentSurface::RoadStone, TEXT("Road stone"), TEXT("Textures/Stone/T_RoadStone"), 140.0f, true},
        {EAshenEnvironmentSurface::WetStone, TEXT("Wet stone"), TEXT("Textures/Stone/T_WetStone"), 160.0f, true},
        {EAshenEnvironmentSurface::WeatheredWood, TEXT("Weathered wood"), TEXT("Textures/Wood/T_WeatheredWood"),
         110.0f, true},
        {EAshenEnvironmentSurface::DarkIron, TEXT("Dark iron"), TEXT("Textures/Metal/T_DarkIron"), 100.0f, true},
        {EAshenEnvironmentSurface::MineDark, TEXT("Mine interior"), TEXT("Textures/Stone/T_MineDark"), 110.0f, true},
        {EAshenEnvironmentSurface::Bark, TEXT("Old bark"), TEXT("Textures/Wood/T_OldBark"), 95.0f, true},
        {EAshenEnvironmentSurface::Pine, TEXT("Dark needles"), TEXT("Textures/Foliage/T_DarkNeedles"), 120.0f,
         true},
        {EAshenEnvironmentSurface::PineShadow, TEXT("Dark needle shadow"),
         TEXT("Textures/Foliage/T_DarkNeedlesShadow"), 120.0f, true},
        {EAshenEnvironmentSurface::HumanStone, TEXT("Human castle stone"), TEXT("Textures/Stone/T_HumanCastle"),
         150.0f, true},
        {EAshenEnvironmentSurface::FoundationStone, TEXT("Foundation stone"),
         TEXT("Textures/Stone/T_Foundation"), 140.0f, true},
        {EAshenEnvironmentSurface::HumanRoof, TEXT("Oxide roof"), TEXT("Textures/Architecture/T_OxideRoof"), 120.0f,
         false},
        {EAshenEnvironmentSurface::Flesh, TEXT("Veiled flesh"), TEXT("Textures/Factions/T_VeiledFlesh"), 130.0f,
         false},
        {EAshenEnvironmentSurface::Bone, TEXT("Ancient bone"), TEXT("Textures/Factions/T_AncientBone"), 110.0f,
         false},
        {EAshenEnvironmentSurface::MythicStone, TEXT("Mythic stone"), TEXT("Textures/Mythic/T_MythicStone"),
         150.0f, false},
    };
    return Specs;
}

FString NormalizedRoot()
{
    FString Root = GetDefault<UAshenEnvironmentKitSettings>()->ProductionContentRoot;
    while (Root.EndsWith(TEXT("/")))
    {
        Root.LeftChopInline(1);
    }
    return Root;
}

FString BuildObjectPath(const FString &RelativePackagePath)
{
    const FString PackagePath = NormalizedRoot() + TEXT("/") + RelativePackagePath;
    return PackagePath + TEXT(".") + FPackageName::GetShortName(PackagePath);
}

FString BuildSourceObjectPath(const FString &RelativePackagePath)
{
    const FString PackagePath = TEXT("/Game/Art/Environment/VowfallKit/") + RelativePackagePath;
    return PackagePath + TEXT(".") + FPackageName::GetShortName(PackagePath);
}

template <typename TObjectType> TObjectType *LoadQuietly(const FString &Path)
{
    return Cast<TObjectType>(StaticLoadObject(TObjectType::StaticClass(), nullptr, *Path, nullptr,
                                               LOAD_NoWarn | LOAD_Quiet));
}

const FSurfaceSpec *FindSurfaceSpec(const EAshenEnvironmentSurface Slot)
{
    for (const FSurfaceSpec &Spec : AllSurfaceSpecs())
    {
        if (Spec.Slot == Slot)
        {
            return &Spec;
        }
    }
    return nullptr;
}

Ashen::EnvironmentKit::FSurfaceTextures LoadSurfaceTextures(const FSurfaceSpec *Spec)
{
    Ashen::EnvironmentKit::FSurfaceTextures Result;
    if (Spec == nullptr)
    {
        return Result;
    }
    Result.Albedo =
        LoadQuietly<UTexture2D>(Ashen::EnvironmentKit::TextureObjectPath(*Spec, TEXT("_BC")));
    Result.Normal =
        LoadQuietly<UTexture2D>(Ashen::EnvironmentKit::TextureObjectPath(*Spec, TEXT("_N")));
    Result.Packed =
        LoadQuietly<UTexture2D>(Ashen::EnvironmentKit::TextureObjectPath(*Spec, TEXT("_ORM")));
    return Result;
}
} // namespace

TConstArrayView<Ashen::EnvironmentKit::FMeshSpec> Ashen::EnvironmentKit::MeshSpecs()
{
    return AllMeshSpecs();
}

TConstArrayView<Ashen::EnvironmentKit::FSurfaceSpec> Ashen::EnvironmentKit::SurfaceSpecs()
{
    return AllSurfaceSpecs();
}

FString Ashen::EnvironmentKit::ObjectPath(const FMeshSpec &Spec)
{
    return BuildObjectPath(Spec.RelativePackagePath);
}

FString Ashen::EnvironmentKit::SourceObjectPath(const FMeshSpec &Spec)
{
    return BuildSourceObjectPath(Spec.RelativePackagePath);
}

FString Ashen::EnvironmentKit::TextureObjectPath(const FSurfaceSpec &Spec, const TCHAR *Suffix)
{
    return BuildObjectPath(FString(Spec.RelativeTexturePrefix) + Suffix);
}

UStaticMesh *Ashen::EnvironmentKit::FindProductionMesh(const EAshenEnvironmentMeshSlot Slot)
{
    const UAshenEnvironmentKitSettings *Settings = GetDefault<UAshenEnvironmentKitSettings>();
    if (!Settings->bEnableProductionMeshes)
    {
        return nullptr;
    }

    for (const FMeshSpec &Spec : AllMeshSpecs())
    {
        if (Spec.Slot == Slot)
        {
            return LoadQuietly<UStaticMesh>(ObjectPath(Spec));
        }
    }
    return nullptr;
}

UStaticMesh *Ashen::EnvironmentKit::FindSourceMesh(const EAshenEnvironmentMeshSlot Slot)
{
    for (const FMeshSpec &Spec : AllMeshSpecs())
    {
        if (Spec.Slot == Slot)
        {
            return LoadQuietly<UStaticMesh>(SourceObjectPath(Spec));
        }
    }
    return nullptr;
}

UStaticMesh *Ashen::EnvironmentKit::ResolveMesh(const EAshenEnvironmentMeshSlot Slot, UStaticMesh *Fallback)
{
    if (UStaticMesh *ProductionMesh = FindProductionMesh(Slot))
    {
        return ProductionMesh;
    }
    if (UStaticMesh *SourceMesh = FindSourceMesh(Slot))
    {
        return SourceMesh;
    }
    return Fallback;
}

EAshenEnvironmentSurface Ashen::EnvironmentKit::SurfaceFallback(
    const EAshenEnvironmentSurface Slot) noexcept
{
    switch (Slot)
    {
    case EAshenEnvironmentSurface::MoorPatch:
        return EAshenEnvironmentSurface::Moor;
    case EAshenEnvironmentSurface::WetStone:
    case EAshenEnvironmentSurface::MineDark:
    case EAshenEnvironmentSurface::HumanStone:
    case EAshenEnvironmentSurface::FoundationStone:
    case EAshenEnvironmentSurface::MythicStone:
        return EAshenEnvironmentSurface::RoadStone;
    case EAshenEnvironmentSurface::None:
    case EAshenEnvironmentSurface::Moor:
    case EAshenEnvironmentSurface::Mud:
    case EAshenEnvironmentSurface::RoadStone:
    case EAshenEnvironmentSurface::WeatheredWood:
    case EAshenEnvironmentSurface::DarkIron:
    case EAshenEnvironmentSurface::Bark:
    case EAshenEnvironmentSurface::Pine:
    case EAshenEnvironmentSurface::PineShadow:
    case EAshenEnvironmentSurface::HumanRoof:
    case EAshenEnvironmentSurface::Flesh:
    case EAshenEnvironmentSurface::Bone:
    case EAshenEnvironmentSurface::Count:
        return EAshenEnvironmentSurface::None;
    }
    return EAshenEnvironmentSurface::None;
}

Ashen::EnvironmentKit::FSurfaceTextures
Ashen::EnvironmentKit::ResolveSurfaceTextures(const EAshenEnvironmentSurface Slot)
{
    FSurfaceTextures Result;
    const UAshenEnvironmentKitSettings *Settings = GetDefault<UAshenEnvironmentKitSettings>();
    if (!Settings->bEnableProductionTextures || Slot == EAshenEnvironmentSurface::None)
    {
        return Result;
    }

    Result = LoadSurfaceTextures(FindSurfaceSpec(Slot));
    const EAshenEnvironmentSurface FallbackSlot = SurfaceFallback(Slot);
    if (FallbackSlot != EAshenEnvironmentSurface::None)
    {
        const FSurfaceTextures Fallback = LoadSurfaceTextures(FindSurfaceSpec(FallbackSlot));
        if (Result.Albedo == nullptr)
        {
            Result.Albedo = Fallback.Albedo;
        }
        if (Result.Normal == nullptr)
        {
            Result.Normal = Fallback.Normal;
        }
        if (Result.Packed == nullptr)
        {
            Result.Packed = Fallback.Packed;
        }
    }
    return Result;
}
