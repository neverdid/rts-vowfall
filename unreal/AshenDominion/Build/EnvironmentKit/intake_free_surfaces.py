"""Normalizes locally acquired free Fab surfaces into Vowfall's private kit.

Run with UnrealEditor-Cmd.exe and -ExecutePythonScript. The script duplicates
only the texture assets Vowfall currently uses, applies the canonical naming
contract, limits ordinary surfaces to 2K, and leaves the acquired source assets
untouched. Content/External is intentionally ignored by Git.
"""

from __future__ import annotations

from dataclasses import dataclass

import unreal


TARGET_ROOT = "/Game/External/VowfallEnvironmentKit"


@dataclass(frozen=True)
class TextureTriplet:
    name: str
    target_prefix: str
    albedo: str
    normal: str
    packed: str


SURFACES = (
    TextureTriplet(
        "Forest Floor",
        "Textures/Ground/T_Moor",
        "/Game/Fab/Megascans/Surfaces/Forest_Floor_sfjmafua/Raw/"
        "sfjmafua_tier_0/Textures/T_sfjmafua_8K_B",
        "/Game/Fab/Megascans/Surfaces/Forest_Floor_sfjmafua/Raw/"
        "sfjmafua_tier_0/Textures/T_sfjmafua_8K_N",
        "/Game/Fab/Megascans/Surfaces/Forest_Floor_sfjmafua/Raw/"
        "sfjmafua_tier_0/Textures/T_sfjmafua_8K_ORM",
    ),
    TextureTriplet(
        "Soil Mud",
        "Textures/Ground/T_Mud",
        "/Game/Fab/Megascans/Surfaces/Soil_Mud_pjuph20/Raw/"
        "pjuph20_tier_0/Textures/T_pjuph20_8K_B",
        "/Game/Fab/Megascans/Surfaces/Soil_Mud_pjuph20/Raw/"
        "pjuph20_tier_0/Textures/T_pjuph20_8K_N",
        "/Game/Fab/Megascans/Surfaces/Soil_Mud_pjuph20/Raw/"
        "pjuph20_tier_0/Textures/T_pjuph20_8K_ORM",
    ),
    TextureTriplet(
        "Weathered Wooden Planks",
        "Textures/Wood/T_WeatheredWood",
        "/Game/Fab/Megascans/Surfaces/Wooden_Planks_vlzhdbwdy/Raw/"
        "vlzhdbwdy_tier_0/Textures/T_vlzhdbwdy_8K_B",
        "/Game/Fab/Megascans/Surfaces/Wooden_Planks_vlzhdbwdy/Raw/"
        "vlzhdbwdy_tier_0/Textures/T_vlzhdbwdy_8K_N",
        "/Game/Fab/Megascans/Surfaces/Wooden_Planks_vlzhdbwdy/Raw/"
        "vlzhdbwdy_tier_0/Textures/T_vlzhdbwdy_8K_ORM",
    ),
    TextureTriplet(
        "Dirty Stone Tiles",
        "Textures/Stone/T_RoadStone",
        "/Game/DirtyStoneTile_material/Textures/T_DirtyStoneTiles_BaseColor",
        "/Game/DirtyStoneTile_material/Textures/T_DirtyStoneTiles_Normal",
        "/Game/DirtyStoneTile_material/Textures/"
        "T_DirtyStoneTiles_OcclusionRoughnessMetallic",
    ),
)


def _destination(prefix: str, suffix: str) -> str:
    return f"{TARGET_ROOT}/{prefix}{suffix}"


def _load_texture(path: str) -> unreal.Texture2D:
    texture = unreal.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Required local texture is missing: {path}")
    return texture


def _duplicate(source_path: str, destination_path: str) -> unreal.Texture2D:
    if not unreal.EditorAssetLibrary.does_asset_exist(destination_path):
        duplicated = unreal.EditorAssetLibrary.duplicate_asset(
            source_path, destination_path
        )
        if not duplicated:
            raise RuntimeError(
                f"Could not duplicate {source_path} to {destination_path}"
            )
    return _load_texture(destination_path)


def _configure(
    texture: unreal.Texture2D,
    *,
    srgb: bool,
    compression: unreal.TextureCompressionSettings,
) -> None:
    texture.set_editor_property("srgb", srgb)
    texture.set_editor_property("compression_settings", compression)
    texture.set_editor_property("max_texture_size", 2048)
    texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
    texture.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(texture, only_if_is_dirty=False)


def _intake(surface: TextureTriplet) -> None:
    sources = (surface.albedo, surface.normal, surface.packed)
    for source in sources:
        _load_texture(source)

    albedo = _duplicate(surface.albedo, _destination(surface.target_prefix, "_BC"))
    normal = _duplicate(surface.normal, _destination(surface.target_prefix, "_N"))
    packed = _duplicate(surface.packed, _destination(surface.target_prefix, "_ORM"))

    _configure(
        albedo,
        srgb=True,
        compression=unreal.TextureCompressionSettings.TC_DEFAULT,
    )
    _configure(
        normal,
        srgb=False,
        compression=unreal.TextureCompressionSettings.TC_NORMALMAP,
    )
    _configure(
        packed,
        srgb=False,
        compression=unreal.TextureCompressionSettings.TC_MASKS,
    )
    unreal.log(f"Vowfall intake complete: {surface.name} -> {surface.target_prefix}")


def main() -> None:
    unreal.EditorAssetLibrary.make_directory(TARGET_ROOT)
    for surface in SURFACES:
        _intake(surface)
    unreal.EditorAssetLibrary.save_directory(TARGET_ROOT, only_if_is_dirty=False)
    unreal.log(f"Vowfall free-surface intake complete: {len(SURFACES)} families")


main()
