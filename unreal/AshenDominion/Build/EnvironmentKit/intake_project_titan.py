"""Normalizes the approved Project Titan environment assets in their source project.

Run this script against the separate ProjectTitanSource project. It duplicates
only the reviewed texture triplet, cliff silhouettes, and Gravewood dead-tree
family. It applies Vowfall's canonical naming and settings and never migrates
Titan maps, code, plugins, materials, or biomes. The resulting
Content/External/VowfallEnvironmentKit folder can then be copied to the same
ignored folder inside Vowfall.
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


@dataclass(frozen=True)
class MeshSelection:
    name: str
    target_path: str
    source_path: str
    target_size_cm: float = 100.0
    minimum_lod_count: int = 5
    removed_material_ids: tuple[int, ...] = ()
    rotation: tuple[float, float, float] = (0.0, 0.0, 0.0)
    grounded_pivot: bool = False


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

MOUNTAIN_CLIFFS = (
    MeshSelection(
        "Project Titan Marshlands Rock 2",
        "Meshes/Rock/SM_MountainCliff_A",
        "/Game/Environment/Marshland/Meshes/Rocks/SM_Marshlands_Rock_2",
    ),
    MeshSelection(
        "Project Titan Marshlands Rock 3",
        "Meshes/Rock/SM_MountainCliff_B",
        "/Game/Environment/Marshland/Meshes/Rocks/SM_Marshlands_Rock_3",
    ),
    MeshSelection(
        "Project Titan Marshlands Rock 4",
        "Meshes/Rock/SM_MountainCliff_C",
        "/Game/Environment/Marshland/Meshes/Rocks/SM_Marshlands_Rock_4",
    ),
)

GRAVEWOOD_MESHES = (
    MeshSelection(
        "Project Titan Marshlands Tree 2 deadwood",
        "Meshes/Foliage/SM_GravewoodTree_A",
        "/Game/Environment/Grassland/Meshes/ShoreLake/Foliage_PCG/"
        "SM_Marshlands_Tree2_LP",
        minimum_lod_count=4,
        removed_material_ids=(1,),
        grounded_pivot=True,
    ),
    MeshSelection(
        "Project Titan Marshlands Tree 4 deadwood",
        "Meshes/Foliage/SM_GravewoodTree_B",
        "/Game/Environment/Grassland/Meshes/ShoreLake/Foliage_PCG/"
        "SM_Marshlands_Tree4_LP",
        minimum_lod_count=4,
        removed_material_ids=(1,),
        grounded_pivot=True,
    ),
    MeshSelection(
        "Project Titan Marshlands dead stump",
        "Meshes/Foliage/SM_GravewoodStump_A",
        "/Game/Environment/Marshland/Meshes/SM_Marshlands_DeadStump1_LP",
        minimum_lod_count=4,
        grounded_pivot=True,
    ),
    MeshSelection(
        "Project Titan curved root chunk",
        "Meshes/Foliage/SM_GravewoodRoot_A",
        "/Game/Environment/Foliage/SM_RootChunk_Curved",
        minimum_lod_count=1,
        grounded_pivot=True,
    ),
    MeshSelection(
        "Project Titan split root chunk",
        "Meshes/Foliage/SM_GravewoodRoot_B",
        "/Game/Environment/Foliage/SM_RootChunk_Split",
        minimum_lod_count=1,
        grounded_pivot=True,
    ),
)

APPROVED_MESHES = MOUNTAIN_CLIFFS + GRAVEWOOD_MESHES


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


def _load_mesh(path: str) -> unreal.StaticMesh:
    mesh = unreal.load_asset(path)
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Required Project Titan mesh is missing: {path}")
    return mesh


def _normalize_mesh(selection: MeshSelection) -> unreal.StaticMesh:
    destination_path = f"{TARGET_ROOT}/{selection.target_path}"
    if unreal.EditorAssetLibrary.does_asset_exist(destination_path):
        if not unreal.EditorAssetLibrary.delete_asset(destination_path):
            raise RuntimeError(f"Could not replace {destination_path}")

    source_mesh = _load_mesh(selection.source_path)
    bounds = source_mesh.get_bounding_box()
    size = bounds.max - bounds.min
    largest_dimension = max(size.x, size.y, size.z)
    if largest_dimension <= 0.0:
        raise RuntimeError(
            f"Project Titan mesh has invalid bounds: {selection.source_path}"
        )

    subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    lod_count = subsystem.get_lod_count(source_mesh)
    if lod_count < selection.minimum_lod_count:
        raise RuntimeError(
            "Project Titan mesh has too few LODs: "
            f"{selection.source_path} has {lod_count}; "
            f"{selection.minimum_lod_count} required"
        )
    copy_options = unreal.GeometryScriptCopyMeshFromAssetOptions()
    copy_options.set_editor_property("apply_build_settings", True)
    copy_options.set_editor_property("request_tangents", True)
    copy_options.set_editor_property("use_build_scale", True)
    dynamic_lods = []
    removed_triangles = 0
    for lod_index in range(lod_count):
        dynamic_mesh = unreal.DynamicMesh()
        requested_lod = unreal.GeometryScriptMeshReadLOD(
            lod_type=unreal.GeometryScriptLODType.SOURCE_MODEL,
            lod_index=lod_index,
        )
        unreal.GeometryScript_AssetUtils.copy_mesh_from_static_mesh_v2(
            source_mesh,
            dynamic_mesh,
            copy_options,
            requested_lod,
            use_section_materials=True,
        )
        for material_id in selection.removed_material_ids:
            deletion = (
                unreal.GeometryScript_Materials
                .delete_triangles_by_material_id(dynamic_mesh, material_id)
            )
            if isinstance(deletion, tuple):
                removed_triangles += deletion[1]
        pitch, yaw, roll = selection.rotation
        if pitch != 0.0 or yaw != 0.0 or roll != 0.0:
            unreal.GeometryScript_MeshTransforms.rotate_mesh(
                dynamic_mesh,
                unreal.Rotator(pitch=pitch, yaw=yaw, roll=roll),
                unreal.Vector(0.0, 0.0, 0.0),
            )
        dynamic_lods.append(dynamic_mesh)

    cleaned_bounds = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(
        dynamic_lods[0]
    )
    cleaned_size = cleaned_bounds.max - cleaned_bounds.min
    cleaned_largest_dimension = max(
        cleaned_size.x, cleaned_size.y, cleaned_size.z
    )
    if cleaned_largest_dimension <= 0.0:
        raise RuntimeError(
            f"Project Titan mesh became empty during cleanup: {selection.source_path}"
        )
    geometry_scale = selection.target_size_cm / cleaned_largest_dimension
    for dynamic_mesh in dynamic_lods:
        unreal.GeometryScript_MeshTransforms.scale_mesh(
            dynamic_mesh,
            unreal.Vector(geometry_scale, geometry_scale, geometry_scale),
            unreal.Vector(0.0, 0.0, 0.0),
        )

    if selection.grounded_pivot:
        canonical_bounds = unreal.GeometryScript_MeshQueries.get_mesh_bounding_box(
            dynamic_lods[0]
        )
        center = (canonical_bounds.min + canonical_bounds.max) * 0.5
        translation = unreal.Vector(-center.x, -center.y, -canonical_bounds.min.z)
        for dynamic_mesh in dynamic_lods:
            unreal.GeometryScript_MeshTransforms.translate_mesh(
                dynamic_mesh, translation
            )

    # Authored LODs cover the detailed trees and stump. The root chunks are
    # already below 500 source triangles, so Nanite would add overhead without
    # improving their normal RTS-camera silhouette.
    create_options = unreal.GeometryScriptCreateNewStaticMeshAssetOptions()
    create_options.set_editor_property("enable_recompute_normals", True)
    create_options.set_editor_property("enable_recompute_tangents", True)
    create_options.set_editor_property("enable_nanite", False)
    create_options.set_editor_property("enable_collision", False)
    created = (
        unreal.GeometryScript_NewAssetUtils
        .create_new_static_mesh_asset_from_mesh_lods(
            dynamic_lods,
            destination_path,
            create_options,
        )
    )
    mesh = created[0] if isinstance(created, tuple) else created
    if not isinstance(mesh, unreal.StaticMesh):
        raise RuntimeError(f"Could not create normalized mesh: {destination_path}")

    default_material = unreal.load_asset(
        "/Engine/EngineMaterials/DefaultMaterial"
    )
    if not isinstance(default_material, unreal.MaterialInterface):
        raise RuntimeError("Could not load Unreal's default material")
    for material_index, _ in enumerate(mesh.get_editor_property("static_materials")):
        mesh.set_material(material_index, default_material)

    mesh.modify()
    unreal.EditorAssetLibrary.save_loaded_asset(mesh, only_if_is_dirty=False)
    if removed_triangles > 0:
        unreal.log(
            "Vowfall Project Titan mesh cleanup: "
            f"{selection.name} removed {removed_triangles} non-deadwood triangles"
        )
    return mesh


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
    for selection in APPROVED_MESHES:
        _load_mesh(selection.source_path)

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
    for selection in APPROVED_MESHES:
        mesh = _normalize_mesh(selection)
        size = mesh.get_bounding_box().max - mesh.get_bounding_box().min
        unreal.log(
            "Vowfall Project Titan mesh intake: "
            f"{selection.name} -> {selection.target_path} "
            f"({size.x:.1f} x {size.y:.1f} x {size.z:.1f} cm)"
        )
    unreal.EditorAssetLibrary.save_directory(TARGET_ROOT, only_if_is_dirty=False)
    unreal.log(
        "Vowfall Project Titan intake complete: "
        f"{FOUNDATION_STONE.name} and {len(APPROVED_MESHES)} environment meshes"
    )


main()
