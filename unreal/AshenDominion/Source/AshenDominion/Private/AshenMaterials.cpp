#include "AshenMaterials.h"

#include "Components/PrimitiveComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/Texture2D.h"
#include "UObject/UObjectGlobals.h"

namespace
{
UMaterialInstanceDynamic *CreateMaterial(UPrimitiveComponent *Component, UObject *Outer, const TCHAR *Path)
{
    if (Component == nullptr || Outer == nullptr)
    {
        return nullptr;
    }

    UMaterialInterface *BaseMaterial = LoadObject<UMaterialInterface>(nullptr, Path);
    return BaseMaterial != nullptr ? UMaterialInstanceDynamic::Create(BaseMaterial, Outer) : nullptr;
}
} // namespace

void Ashen::Materials::Apply(UPrimitiveComponent *Component, UObject *Outer, const FLinearColor &Color,
                             const float Roughness)
{
    UMaterialInterface *BaseMaterial =
        LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (Component == nullptr || BaseMaterial == nullptr)
    {
        return;
    }

    UMaterialInstanceDynamic *Material = UMaterialInstanceDynamic::Create(BaseMaterial, Outer);

    TArray<FMaterialParameterInfo> VectorParameters;
    TArray<FGuid> VectorIds;
    BaseMaterial->GetAllVectorParameterInfo(VectorParameters, VectorIds);
    for (const FMaterialParameterInfo &Parameter : VectorParameters)
    {
        Material->SetVectorParameterValueByInfo(Parameter, Color);
    }

    TArray<FMaterialParameterInfo> ScalarParameters;
    TArray<FGuid> ScalarIds;
    BaseMaterial->GetAllScalarParameterInfo(ScalarParameters, ScalarIds);
    for (const FMaterialParameterInfo &Parameter : ScalarParameters)
    {
        if (Parameter.Name == TEXT("Roughness"))
        {
            Material->SetScalarParameterValueByInfo(Parameter, Roughness);
        }
    }

    Component->SetMaterial(0, Material);
}

void Ashen::Materials::ApplySurface(UPrimitiveComponent *Component, UObject *Outer, const FSurfaceStyle &Style,
                                    const EAshenEnvironmentSurface Surface)
{
    UMaterialInstanceDynamic *Material =
        CreateMaterial(Component, Outer, TEXT("/Game/Art/Materials/M_VowfallSurface.M_VowfallSurface"));
    if (Material == nullptr)
    {
        Apply(Component, Outer, Style.BaseColor, Style.Roughness);
        return;
    }

    Material->SetVectorParameterValue(TEXT("BaseColor"), Style.BaseColor);
    Material->SetVectorParameterValue(TEXT("SecondaryColor"), Style.SecondaryColor);
    Material->SetVectorParameterValue(TEXT("AccentColor"), Style.AccentColor);
    Material->SetScalarParameterValue(TEXT("Roughness"), Style.Roughness);
    Material->SetScalarParameterValue(TEXT("MacroScale"), Style.MacroScale);
    Material->SetScalarParameterValue(TEXT("DetailScale"), Style.DetailScale);
    Material->SetScalarParameterValue(TEXT("DetailStrength"), Style.DetailStrength);
    Material->SetScalarParameterValue(TEXT("Specular"), Style.Specular);
    Material->SetScalarParameterValue(TEXT("AmbientOcclusion"), Style.AmbientOcclusion);
    Material->SetScalarParameterValue(TEXT("TextureTiling"), Style.TextureTiling);
    Material->SetVectorParameterValue(TEXT("TextureTint"), Style.TextureTint);

    const Ashen::EnvironmentKit::FSurfaceTextures Textures =
        Ashen::EnvironmentKit::ResolveSurfaceTextures(Surface);
    if (Textures.Albedo != nullptr)
    {
        Material->SetTextureParameterValue(TEXT("AlbedoTexture"), Textures.Albedo);
        Material->SetScalarParameterValue(TEXT("TextureBlend"), Style.TextureBlend);
    }
    else
    {
        Material->SetScalarParameterValue(TEXT("TextureBlend"), 0.0f);
    }
    if (Textures.Normal != nullptr)
    {
        Material->SetTextureParameterValue(TEXT("NormalTexture"), Textures.Normal);
        Material->SetScalarParameterValue(TEXT("NormalStrength"), Style.NormalStrength);
    }
    else
    {
        Material->SetScalarParameterValue(TEXT("NormalStrength"), 0.0f);
    }
    if (Textures.Packed != nullptr)
    {
        Material->SetTextureParameterValue(TEXT("PackedTexture"), Textures.Packed);
        Material->SetScalarParameterValue(TEXT("PackedStrength"), Style.PackedStrength);
    }
    else
    {
        Material->SetScalarParameterValue(TEXT("PackedStrength"), 0.0f);
    }
    Component->SetMaterial(0, Material);
}

void Ashen::Materials::ApplyWater(UPrimitiveComponent *Component, UObject *Outer, const FLinearColor &ShallowColor,
                                  const FLinearColor &DeepColor, const float Opacity)
{
    UMaterialInstanceDynamic *Material =
        CreateMaterial(Component, Outer, TEXT("/Game/Art/Materials/M_VowfallWater.M_VowfallWater"));
    if (Material == nullptr)
    {
        Apply(Component, Outer, DeepColor, 0.08f);
        return;
    }

    Material->SetVectorParameterValue(TEXT("ShallowColor"), ShallowColor);
    Material->SetVectorParameterValue(TEXT("DeepColor"), DeepColor);
    Material->SetScalarParameterValue(TEXT("Opacity"), Opacity);
    Material->SetScalarParameterValue(TEXT("Roughness"), 0.08f);
    Material->SetScalarParameterValue(TEXT("Specular"), 0.72f);
    Material->SetScalarParameterValue(TEXT("NormalTiling"), 5.2f);
    Component->SetMaterial(0, Material);
}
