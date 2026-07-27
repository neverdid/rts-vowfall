"""Normalizes the approved Project Titan rock surface in its source project.

Run this script against the separate ProjectTitanSource project. It duplicates
only the reviewed texture triplet, applies Vowfall's canonical naming and
streaming settings, and never migrates Titan maps, code, plugins, or biomes.
The resulting Content/External/VowfallEnvironmentKit folder can then be copied
to the same ignored folder inside the Vowfall project.
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


FOUNDATION_STONE = TextureTriplet(
    "Project Titan Marshlands Mossy Rock Medium",
    "Textures/Stone/T_Foundation",
    "/Game/Environment/Marshland/Textures/Rocks/"
    "T_Marshlands_MossyRock_Medium_01_D",
    "/Game/Environment/Marshland/Textures/Rocks/"
    "T_Marshlands_MossyRock_Medium_01_N",
    "/Game/Environment/Marshland/Textures/Rocks/"
    "T_Marshlands_MossyRock_Medium_01_ORM",
)


def _destination(prefix: str, suffix: str) -> str:
    return f"{TARGET_ROOT}/{prefix}{suffix}"


def _load_texture(path: str) -> unreal.Texture2D:
    texture = unreal.load_asset(path)
    if not isinstance(texture, unreal.Texture2D):
        raise RuntimeError(f"Required Project Titan texture is missing: {path}")
    return texture


def _duplicate(source_path: str, destination_path: str) -> unreal.Texture2D:
    if not unreal.EditorAssetLibrary.does_asset_exist(destination_path):
        if not unreal.EditorAssetLibrary.duplicate_asset(
            source_path, destination_path
        ):
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


def main() -> None:
    if "titan" not in unreal.SystemLibrary.get_game_name().lower():
        raise RuntimeError(
            "Run intake_project_titan.py against the separate ProjectTitanSource "
            "project, not Vowfall."
        )

    for source in (
        FOUNDATION_STONE.albedo,
        FOUNDATION_STONE.normal,
        FOUNDATION_STONE.packed,
    ):
        _load_texture(source)

    unreal.EditorAssetLibrary.make_directory(TARGET_ROOT)
    albedo = _duplicate(
        FOUNDATION_STONE.albedo,
        _destination(FOUNDATION_STONE.target_prefix, "_BC"),
    )
    normal = _duplicate(
        FOUNDATION_STONE.normal,
        _destination(FOUNDATION_STONE.target_prefix, "_N"),
    )
    packed = _duplicate(
        FOUNDATION_STONE.packed,
        _destination(FOUNDATION_STONE.target_prefix, "_ORM"),
    )

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
    unreal.EditorAssetLibrary.save_directory(TARGET_ROOT, only_if_is_dirty=False)
    unreal.log(
        "Vowfall Project Titan intake complete: "
        f"{FOUNDATION_STONE.name} -> {FOUNDATION_STONE.target_prefix}"
    )


main()
