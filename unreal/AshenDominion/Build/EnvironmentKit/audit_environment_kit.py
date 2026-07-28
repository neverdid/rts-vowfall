"""Audits locally acquired production environment assets without redistributing them.

Run with UnrealEditor-Cmd.exe and -ExecutePythonScript. Missing or malformed assets are
reported but only fail when -EnvironmentKitStrict is present on the Unreal command line.
"""

from __future__ import annotations

import json
from pathlib import Path

import unreal


SCRIPT_DIR = Path(__file__).resolve().parent
MANIFEST_PATH = SCRIPT_DIR / "environment-kit.json"
REPORT_PATH = SCRIPT_DIR.parent.parent / "Saved" / "EnvironmentKit" / "audit.json"


def _object_path(root: str, package_path: str) -> str:
    asset_name = package_path.rsplit("/", 1)[-1]
    return f"{root}/{package_path}.{asset_name}"


def _texture_paths(root: str, prefix: str) -> list[str]:
    return [_object_path(root, f"{prefix}{suffix}") for suffix in ("_BC", "_N", "_ORM")]


def _audit() -> dict[str, object]:
    manifest = json.loads(MANIFEST_PATH.read_text(encoding="utf-8"))
    root = manifest["content_root"].rstrip("/")
    missing_meshes: list[str] = []
    missing_textures: list[str] = []
    present_meshes: list[str] = []
    present_textures: list[str] = []
    invalid_meshes: list[dict[str, object]] = []
    mesh_subsystem = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)

    for entry in manifest["priority_meshes"]:
        path = _object_path(root, entry["path"])
        if unreal.EditorAssetLibrary.does_asset_exist(path):
            present_meshes.append(path)
            mesh = unreal.load_asset(path)
            issues: list[str] = []
            if not isinstance(mesh, unreal.StaticMesh):
                issues.append("asset is not a StaticMesh")
            else:
                minimum_lods = entry.get("minimum_lods")
                if minimum_lods is not None:
                    lod_count = mesh_subsystem.get_lod_count(mesh)
                    if lod_count < minimum_lods:
                        issues.append(
                            f"has {lod_count} LODs; expected at least {minimum_lods}"
                        )

                expected_max_dimension = entry.get("max_dimension_cm")
                if expected_max_dimension is not None:
                    bounds = mesh.get_bounding_box()
                    size = bounds.max - bounds.min
                    max_dimension = max(size.x, size.y, size.z)
                    if abs(max_dimension - expected_max_dimension) > 0.5:
                        issues.append(
                            f"maximum dimension is {max_dimension:.2f} cm; "
                            f"expected {expected_max_dimension:.2f} cm"
                        )

                expected_nanite = entry.get("nanite")
                if expected_nanite is not None:
                    nanite_enabled = bool(
                        mesh.get_editor_property("nanite_settings").enabled
                    )
                    if nanite_enabled != expected_nanite:
                        issues.append(
                            f"Nanite is {nanite_enabled}; expected {expected_nanite}"
                        )

            if issues:
                invalid_meshes.append({"path": path, "issues": issues})
        else:
            missing_meshes.append(path)

    for entry in manifest["priority_surfaces"]:
        for path in _texture_paths(root, entry["prefix"]):
            if unreal.EditorAssetLibrary.does_asset_exist(path):
                present_textures.append(path)
            else:
                missing_textures.append(path)

    return {
        "kit_id": manifest["kit_id"],
        "content_root": root,
        "present_meshes": present_meshes,
        "missing_meshes": missing_meshes,
        "invalid_meshes": invalid_meshes,
        "present_textures": present_textures,
        "missing_textures": missing_textures,
        "complete": (
            not missing_meshes and not invalid_meshes and not missing_textures
        ),
    }


def main() -> None:
    report = _audit()
    REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
    REPORT_PATH.write_text(json.dumps(report, indent=2), encoding="utf-8")

    unreal.log(
        "Vowfall environment kit: "
        f"{len(report['present_meshes'])} meshes and "
        f"{len(report['present_textures'])} textures present"
    )
    for path in report["missing_meshes"]:
        unreal.log_warning(f"Missing environment mesh: {path}")
    for entry in report["invalid_meshes"]:
        unreal.log_warning(
            f"Invalid environment mesh: {entry['path']} - "
            + "; ".join(entry["issues"])
        )
    for path in report["missing_textures"]:
        unreal.log_warning(f"Missing environment texture: {path}")

    strict = "-EnvironmentKitStrict" in unreal.SystemLibrary.get_command_line()
    if strict and not report["complete"]:
        raise RuntimeError("The production environment kit is incomplete; see audit.json")


main()
