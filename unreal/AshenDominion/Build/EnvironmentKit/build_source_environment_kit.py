"""Builds Vowfall's original source-controlled environment fallback meshes.

These compact meshes replace Engine primitives immediately and remain available when
licensed production content is absent. Run with UnrealEditor-Cmd.exe and
-ExecutePythonScript after changing a factory. The GeometryScripting plugin is editor-only.
"""

from __future__ import annotations

import os
from collections.abc import Callable

import unreal


SOURCE_ROOT = "/Game/Art/Environment/VowfallKit"
PRIMITIVE_OPTIONS = unreal.GeometryScriptPrimitiveOptions()
CENTER = unreal.GeometryScriptPrimitiveOriginMode.CENTER


def _transform(
    location: tuple[float, float, float] = (0.0, 0.0, 0.0),
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0),
) -> unreal.Transform:
    return unreal.Transform(
        location=unreal.Vector(*location),
        rotation=unreal.Rotator(*rotation),
        scale=unreal.Vector(*scale),
    )


def _mesh() -> unreal.DynamicMesh:
    return unreal.DynamicMesh()


def _box(
    mesh: unreal.DynamicMesh,
    location: tuple[float, float, float] = (0.0, 0.0, 0.0),
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
    dimensions: tuple[float, float, float] = (100.0, 100.0, 100.0),
) -> None:
    unreal.GeometryScript_Primitives.append_box(
        mesh,
        PRIMITIVE_OPTIONS,
        _transform(location, rotation),
        dimensions[0],
        dimensions[1],
        dimensions[2],
        0,
        0,
        0,
        CENTER,
    )


def _rock(
    mesh: unreal.DynamicMesh,
    location: tuple[float, float, float] = (0.0, 0.0, 0.0),
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
    scale: tuple[float, float, float] = (1.0, 1.0, 1.0),
    steps: int = 2,
) -> None:
    unreal.GeometryScript_Primitives.append_sphere_box(
        mesh,
        PRIMITIVE_OPTIONS,
        _transform(location, rotation, scale),
        50.0,
        steps,
        steps,
        steps,
        CENTER,
    )


def _cone(
    mesh: unreal.DynamicMesh,
    base_radius: float = 50.0,
    top_radius: float = 5.0,
    height: float = 100.0,
    location: tuple[float, float, float] = (0.0, 0.0, 0.0),
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
    radial_steps: int = 9,
) -> None:
    unreal.GeometryScript_Primitives.append_cone(
        mesh,
        PRIMITIVE_OPTIONS,
        _transform(location, rotation),
        base_radius,
        top_radius,
        height,
        radial_steps,
        2,
        True,
        CENTER,
    )


def _cylinder(
    mesh: unreal.DynamicMesh,
    radius: float = 50.0,
    height: float = 100.0,
    location: tuple[float, float, float] = (0.0, 0.0, 0.0),
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0),
    radial_steps: int = 9,
) -> None:
    unreal.GeometryScript_Primitives.append_cylinder(
        mesh,
        PRIMITIVE_OPTIONS,
        _transform(location, rotation),
        radius,
        height,
        radial_steps,
        1,
        True,
        CENTER,
    )


def _roadbed() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, dimensions=(100.0, 100.0, 100.0))
    return result


def _road_stone() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, dimensions=(100.0, 84.0, 100.0))
    return result


def _road_rut() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, dimensions=(100.0, 68.0, 100.0))
    return result


def _river_bank() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, dimensions=(100.0, 100.0, 72.0))
    _box(result, location=(-29.0, 6.0, 10.0), dimensions=(30.0, 78.0, 42.0))
    return result


def _reed() -> unreal.DynamicMesh:
    result = _mesh()
    for x, y, pitch, height in (
        (-22.0, -8.0, -8.0, 82.0),
        (-9.0, 12.0, 6.0, 96.0),
        (5.0, -10.0, -4.0, 88.0),
        (18.0, 8.0, 9.0, 76.0),
        (28.0, -4.0, -11.0, 68.0),
    ):
        _cone(result, 7.0, 1.5, height, (x, y, (height - 100.0) * 0.5), (pitch, 0.0, 0.0), 6)
    return result


def _timber() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, dimensions=(91.0, 91.0, 100.0), rotation=(0.7, 0.0, -0.8))
    _rock(result, location=(42.0, 5.0, -18.0), scale=(0.16, 0.22, 0.17))
    _rock(result, location=(-38.0, -7.0, 23.0), scale=(0.18, 0.20, 0.15))
    return result


def _iron_beam() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, dimensions=(76.0, 76.0, 100.0), rotation=(0.0, 0.0, 1.2))
    _box(result, location=(0.0, 0.0, 39.0), dimensions=(100.0, 91.0, 10.0))
    return result


def _tree_trunk() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 50.0, 31.0, 100.0, rotation=(1.5, 4.0, -1.0), radial_steps=8)
    _rock(result, location=(31.0, -3.0, -20.0), scale=(0.22, 0.24, 0.17))
    _rock(result, location=(-25.0, 5.0, 16.0), scale=(0.18, 0.19, 0.14))
    return result


def _conifer_crown(shadow: bool = False) -> unreal.DynamicMesh:
    result = _mesh()
    width = 1.08 if shadow else 1.0
    _cone(result, 48.0 * width, 9.0, 55.0, location=(0.0, 0.0, -22.0), radial_steps=9)
    _cone(result, 39.0 * width, 7.0, 52.0, location=(0.0, 0.0, 2.0), rotation=(0.0, 13.0, 0.0), radial_steps=9)
    _cone(result, 29.0 * width, 3.0, 45.0, location=(0.0, 0.0, 28.0), rotation=(0.0, 29.0, 0.0), radial_steps=8)
    return result


def _broadleaf_canopy() -> unreal.DynamicMesh:
    result = _mesh()
    for location, scale, yaw in (
        ((-23.0, -9.0, -4.0), (0.62, 0.58, 0.52), 8.0),
        ((20.0, -12.0, 3.0), (0.58, 0.54, 0.50), 61.0),
        ((-4.0, 20.0, 10.0), (0.68, 0.58, 0.57), 117.0),
        ((8.0, 3.0, 31.0), (0.52, 0.48, 0.45), 174.0),
    ):
        _rock(result, location=location, rotation=(7.0, yaw, -5.0), scale=scale, steps=3)
    return result


def _dead_branch() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 45.0, 24.0, 100.0, rotation=(1.0, 0.0, -2.0), radial_steps=7)
    _cone(result, 19.0, 3.0, 64.0, (26.0, 0.0, 12.0), (0.0, 41.0, 0.0), 6)
    _cone(result, 16.0, 2.0, 54.0, (-22.0, 5.0, -4.0), (0.0, -49.0, 12.0), 6)
    return result


def _grass_tuft() -> unreal.DynamicMesh:
    result = _mesh()
    for x, y, pitch, yaw, height in (
        (-24.0, -7.0, -13.0, 0.0, 90.0),
        (-10.0, 10.0, 9.0, 34.0, 98.0),
        (3.0, -12.0, -7.0, 72.0, 86.0),
        (16.0, 8.0, 11.0, 115.0, 78.0),
        (27.0, -2.0, -10.0, 151.0, 69.0),
    ):
        _cone(result, 12.0, 0.8, height, (x, y, (height - 100.0) * 0.5), (pitch, yaw, 0.0), 5)
    return result


def _field_rock() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, rotation=(15.0, 31.0, -9.0), scale=(0.94, 0.79, 0.66), steps=3)
    _rock(result, location=(31.0, -19.0, -7.0), rotation=(-12.0, 76.0, 8.0), scale=(0.42, 0.46, 0.35))
    return result


def _mountain_cliff() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, location=(-22.0, 4.0, -13.0), rotation=(11.0, 18.0, -7.0), scale=(0.88, 0.72, 0.86), steps=3)
    _rock(result, location=(22.0, -7.0, 13.0), rotation=(-14.0, 69.0, 9.0), scale=(0.72, 0.62, 0.92), steps=3)
    _rock(result, location=(4.0, 18.0, 31.0), rotation=(8.0, 112.0, -11.0), scale=(0.55, 0.48, 0.58))
    return result


def _mountain_cliff_secondary() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(
        result,
        location=(-13.0, -5.0, -9.0),
        rotation=(-7.0, 37.0, 11.0),
        scale=(0.63, 0.55, 0.96),
        steps=3,
    )
    _rock(
        result,
        location=(22.0, 11.0, -27.0),
        rotation=(18.0, 103.0, -6.0),
        scale=(0.58, 0.47, 0.48),
    )
    _rock(
        result,
        location=(-25.0, 16.0, 24.0),
        rotation=(-13.0, 148.0, 8.0),
        scale=(0.38, 0.42, 0.52),
    )
    return result


def _mountain_cliff_tertiary() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(
        result,
        location=(-8.0, 2.0, -18.0),
        rotation=(8.0, 54.0, -5.0),
        scale=(0.96, 0.62, 0.64),
        steps=3,
    )
    _rock(
        result,
        location=(27.0, -11.0, 13.0),
        rotation=(-16.0, 121.0, 12.0),
        scale=(0.48, 0.58, 0.74),
        steps=3,
    )
    _rock(
        result,
        location=(-29.0, 12.0, 21.0),
        rotation=(11.0, 174.0, -9.0),
        scale=(0.36, 0.43, 0.48),
    )
    return result


def _mine_mouth() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, (-36.0, 0.0, -8.0), dimensions=(22.0, 72.0, 84.0), rotation=(0.0, 0.0, -4.0))
    _box(result, (36.0, 0.0, -8.0), dimensions=(22.0, 72.0, 84.0), rotation=(0.0, 0.0, 4.0))
    _box(result, (-22.0, 0.0, 37.0), (0.0, 0.0, -23.0), (58.0, 72.0, 20.0))
    _box(result, (22.0, 0.0, 37.0), (0.0, 0.0, 23.0), (58.0, 72.0, 20.0))
    return result


def _forest_root() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 48.0, 20.0, 100.0, rotation=(0.0, 0.0, 0.0), radial_steps=7)
    _cone(result, 19.0, 2.0, 64.0, (23.0, 3.0, 6.0), (0.0, 48.0, -7.0), 6)
    _cone(result, 17.0, 2.0, 57.0, (-20.0, -4.0, -9.0), (0.0, -52.0, 11.0), 6)
    return result


def _gravewood_tree_a() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 16.0, 8.0, 100.0, location=(0.0, 0.0, 50.0), rotation=(1.0, 3.0, -2.0), radial_steps=8)
    _cone(result, 9.0, 1.5, 61.0, location=(18.0, 0.0, 73.0), rotation=(0.0, 38.0, 3.0), radial_steps=7)
    _cone(result, 8.0, 1.5, 53.0, location=(-18.0, 3.0, 66.0), rotation=(0.0, -47.0, -7.0), radial_steps=7)
    _cone(result, 6.0, 1.0, 42.0, location=(3.0, -16.0, 86.0), rotation=(31.0, 6.0, 0.0), radial_steps=6)
    _rock(result, location=(-11.0, 8.0, 35.0), rotation=(7.0, 41.0, -4.0), scale=(0.18, 0.13, 0.12))
    return result


def _gravewood_tree_b() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 18.0, 9.0, 76.0, location=(0.0, 0.0, 38.0), rotation=(-2.0, -4.0, 3.0), radial_steps=9)
    _cone(result, 10.0, 2.0, 72.0, location=(17.0, 0.0, 70.0), rotation=(0.0, 28.0, 6.0), radial_steps=7)
    _cone(result, 11.0, 2.0, 68.0, location=(-16.0, 3.0, 69.0), rotation=(0.0, -34.0, -5.0), radial_steps=7)
    _cone(result, 5.0, 1.0, 39.0, location=(30.0, -8.0, 87.0), rotation=(18.0, 57.0, 0.0), radial_steps=6)
    _cone(result, 5.0, 1.0, 36.0, location=(-28.0, 9.0, 84.0), rotation=(-21.0, -62.0, 0.0), radial_steps=6)
    return result


def _gravewood_stump() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 35.0, 25.0, 63.0, location=(0.0, 0.0, 31.5), rotation=(2.0, 5.0, -3.0), radial_steps=9)
    for location, yaw, scale in (
        ((34.0, 1.0, 7.0), 11.0, (0.48, 0.16, 0.11)),
        ((-30.0, -8.0, 6.0), 171.0, (0.42, 0.15, 0.10)),
        ((5.0, 31.0, 5.0), 88.0, (0.37, 0.14, 0.09)),
    ):
        _rock(result, location=location, rotation=(4.0, yaw, -3.0), scale=scale)
    return result


def _gravewood_root_a() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, location=(-8.0, 0.0, 7.0), rotation=(4.0, 17.0, -5.0), scale=(0.94, 0.17, 0.12), steps=3)
    _rock(result, location=(27.0, 18.0, 6.0), rotation=(-3.0, 52.0, 4.0), scale=(0.46, 0.13, 0.10))
    return result


def _gravewood_root_b() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, location=(-7.0, 0.0, 8.0), rotation=(3.0, -13.0, -4.0), scale=(0.88, 0.18, 0.13), steps=3)
    _rock(result, location=(23.0, 19.0, 6.0), rotation=(-4.0, 49.0, 3.0), scale=(0.51, 0.14, 0.10))
    _rock(result, location=(20.0, -19.0, 5.0), rotation=(5.0, -54.0, -2.0), scale=(0.42, 0.13, 0.09))
    return result


def _human_wall() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, location=(0.0, 0.0, -10.0), dimensions=(100.0, 88.0, 62.0))
    for x in (-41.0, -20.5, 0.0, 20.5, 41.0):
        _box(result, location=(x, 0.0, 31.0), dimensions=(14.0, 88.0, 32.0))
    return result


def _human_tower() -> unreal.DynamicMesh:
    result = _mesh()
    _cylinder(result, 43.0, 78.0, location=(0.0, 0.0, -9.0), radial_steps=10)
    for yaw in range(0, 360, 45):
        direction = unreal.MathLibrary.get_forward_vector(unreal.Rotator(0.0, float(yaw), 0.0))
        _box(
            result,
            location=(direction.x * 41.0, direction.y * 41.0, 36.0),
            rotation=(0.0, float(yaw), 0.0),
            dimensions=(18.0, 18.0, 28.0),
        )
    return result


def _human_roof() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 50.0, 1.5, 88.0, location=(0.0, 0.0, -4.0), radial_steps=10)
    _cylinder(result, 50.0, 9.0, location=(0.0, 0.0, -48.0), radial_steps=10)
    return result


def _human_foundation() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, rotation=(8.0, 18.0, -5.0), scale=(0.98, 0.91, 0.78), steps=3)
    _rock(result, location=(-31.0, 24.0, -19.0), rotation=(-9.0, 68.0, 7.0), scale=(0.36, 0.40, 0.31))
    return result


def _human_banner() -> unreal.DynamicMesh:
    result = _mesh()
    _box(result, location=(0.0, 0.0, 7.0), dimensions=(94.0, 12.0, 78.0))
    _box(result, location=(-25.0, 0.0, -38.0), rotation=(0.0, 0.0, -18.0), dimensions=(42.0, 12.0, 38.0))
    _box(result, location=(25.0, 0.0, -38.0), rotation=(0.0, 0.0, 18.0), dimensions=(42.0, 12.0, 38.0))
    return result


def _monster_mass() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, rotation=(8.0, 13.0, -10.0), scale=(0.93, 0.78, 0.72), steps=3)
    _rock(result, location=(24.0, 8.0, 17.0), rotation=(-13.0, 71.0, 8.0), scale=(0.47, 0.43, 0.52))
    _rock(result, location=(-27.0, -10.0, -13.0), rotation=(11.0, 126.0, -7.0), scale=(0.38, 0.46, 0.41))
    return result


def _spike() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 48.0, 0.5, 100.0, rotation=(3.0, 7.0, -4.0), radial_steps=7)
    _rock(result, location=(0.0, 0.0, -37.0), scale=(0.58, 0.52, 0.24))
    return result


def _rib() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 42.0, 19.0, 100.0, rotation=(0.0, 0.0, -2.0), radial_steps=7)
    _rock(result, location=(0.0, 0.0, 39.0), scale=(0.31, 0.35, 0.20))
    return result


def _sinew() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, rotation=(9.0, 21.0, -8.0), scale=(0.90, 0.72, 0.80), steps=3)
    _rock(result, location=(18.0, -21.0, 16.0), rotation=(-7.0, 79.0, 11.0), scale=(0.48, 0.38, 0.53))
    return result


def _bone_palisade() -> unreal.DynamicMesh:
    result = _mesh()
    _cone(result, 43.0, 2.0, 100.0, radial_steps=7)
    _rock(result, location=(0.0, 0.0, -37.0), scale=(0.58, 0.50, 0.25))
    return result


def _ritual_stone() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, rotation=(4.0, 17.0, -6.0), scale=(0.55, 0.45, 1.0), steps=3)
    _rock(result, location=(0.0, 0.0, -38.0), rotation=(-8.0, 71.0, 5.0), scale=(0.68, 0.58, 0.25))
    return result


def _mythic_arch() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, rotation=(3.0, 13.0, -3.0), scale=(0.72, 0.67, 1.0), steps=3)
    _box(result, location=(0.0, 0.0, -31.0), dimensions=(84.0, 78.0, 26.0))
    return result


def _brazier() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, location=(0.0, 0.0, 8.0), scale=(0.88, 0.88, 0.33), steps=3)
    _cone(result, 30.0, 18.0, 62.0, location=(0.0, 0.0, -19.0), radial_steps=8)
    return result


def _ember() -> unreal.DynamicMesh:
    result = _mesh()
    _rock(result, rotation=(11.0, 28.0, -8.0), scale=(0.74, 0.68, 1.0), steps=3)
    return result


FACTORIES: dict[str, Callable[[], unreal.DynamicMesh]] = {
    "Meshes/Road/SM_RoadBed_A": _roadbed,
    "Meshes/Road/SM_RoadStone_A": _road_stone,
    "Meshes/Road/SM_RoadRut_A": _road_rut,
    "Meshes/Rock/SM_RiverBank_A": _river_bank,
    "Meshes/Foliage/SM_Reed_A": _reed,
    "Meshes/Architecture/SM_BridgeTimber_A": _timber,
    "Meshes/Architecture/SM_BridgeIron_A": _iron_beam,
    "Meshes/Foliage/SM_TreeTrunk_A": _tree_trunk,
    "Meshes/Foliage/SM_ConiferCrown_A": _conifer_crown,
    "Meshes/Foliage/SM_ConiferCrownShadow_A": lambda: _conifer_crown(True),
    "Meshes/Foliage/SM_BroadleafCanopy_A": _broadleaf_canopy,
    "Meshes/Foliage/SM_DeadBranch_A": _dead_branch,
    "Meshes/Foliage/SM_GrassTuft_A": _grass_tuft,
    "Meshes/Rock/SM_FieldRock_A": _field_rock,
    "Meshes/Rock/SM_MountainCliff_A": _mountain_cliff,
    "Meshes/Rock/SM_MountainCliff_B": _mountain_cliff_secondary,
    "Meshes/Rock/SM_MountainCliff_C": _mountain_cliff_tertiary,
    "Meshes/Architecture/SM_MineMouth_A": _mine_mouth,
    "Meshes/Architecture/SM_MineTimber_A": _timber,
    "Meshes/Foliage/SM_ForestRoot_A": _forest_root,
    "Meshes/Foliage/SM_GravewoodTree_A": _gravewood_tree_a,
    "Meshes/Foliage/SM_GravewoodTree_B": _gravewood_tree_b,
    "Meshes/Foliage/SM_GravewoodStump_A": _gravewood_stump,
    "Meshes/Foliage/SM_GravewoodRoot_A": _gravewood_root_a,
    "Meshes/Foliage/SM_GravewoodRoot_B": _gravewood_root_b,
    "Meshes/Factions/Human/SM_HumanWall_A": _human_wall,
    "Meshes/Factions/Human/SM_HumanTower_A": _human_tower,
    "Meshes/Factions/Human/SM_HumanRoof_A": _human_roof,
    "Meshes/Factions/Human/SM_HumanFoundation_A": _human_foundation,
    "Meshes/Factions/Human/SM_HumanTrim_A": _iron_beam,
    "Meshes/Factions/Human/SM_HumanBanner_A": _human_banner,
    "Meshes/Factions/Veiled/SM_Mass_A": _monster_mass,
    "Meshes/Factions/Veiled/SM_Spike_A": _spike,
    "Meshes/Factions/Veiled/SM_Rib_A": _rib,
    "Meshes/Factions/Veiled/SM_Sinew_A": _sinew,
    "Meshes/Factions/Veiled/SM_BonePalisade_A": _bone_palisade,
    "Meshes/Mythic/SM_RitualStone_A": _ritual_stone,
    "Meshes/Mythic/SM_MythicArch_A": _mythic_arch,
    "Meshes/Props/SM_BrazierBowl_A": _brazier,
    "Meshes/VFX/SM_EmberCore_A": _ember,
}


def _build_asset(relative_path: str, factory: Callable[[], unreal.DynamicMesh]) -> None:
    asset_path = f"{SOURCE_ROOT}/{relative_path}"
    if unreal.EditorAssetLibrary.does_asset_exist(asset_path):
        if not unreal.EditorAssetLibrary.delete_asset(asset_path):
            raise RuntimeError(f"Could not replace {asset_path}")

    options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    options.set_editor_property("enable_recompute_normals", True)
    options.set_editor_property("enable_recompute_tangents", True)
    options.set_editor_property("enable_nanite", False)
    options.set_editor_property("enable_collision", False)
    created = unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(
        factory(), asset_path, options
    )
    asset = created[0] if isinstance(created, tuple) else created
    if asset is None:
        raise RuntimeError(f"Could not create {asset_path}")
    if not unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False):
        raise RuntimeError(f"Could not save {asset_path}")
    unreal.log(f"Built source environment mesh {asset_path}")


def main() -> None:
    requested = {
        path.strip()
        for path in os.environ.get("VOWFALL_ENVIRONMENT_ASSETS", "").split(";")
        if path.strip()
    }
    unknown = requested.difference(FACTORIES)
    if unknown:
        raise RuntimeError(f"Unknown environment mesh factories: {sorted(unknown)}")

    selected = {
        relative_path: factory
        for relative_path, factory in FACTORIES.items()
        if not requested or relative_path in requested
    }
    for relative_path, factory in selected.items():
        _build_asset(relative_path, factory)
    unreal.log(f"Built {len(selected)} Vowfall source environment meshes")


main()
