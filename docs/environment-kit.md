# Vowfall production environment kit

## Purpose

Step 3B replaces the battlefield's Engine primitives with a coherent production kit without allowing
art assets to change pathing, targeting, selection, or competitive geometry. Visual components keep
collision disabled; the invisible deterministic ground and simulation obstacles remain authoritative.

The runtime resolves semantic slots such as `MountainRock`, `TreeTrunk`, and `BridgeTimber` from
`/Game/External/VowfallEnvironmentKit`. A missing slot falls back quietly to the source-controlled
Vowfall kit under `/Game/Art/Environment/VowfallKit`, so programmers and CI can build the project
without owning every licensed pack. That original kit contains 35 reproducible Geometry Script assets
and is itself preferred over the legacy Engine primitives.

## Project Titan decision

[Project Titan](https://www.fab.com/listings/c05aac82-4c1a-4e42-96b3-be668dc40fca) is an approved
candidate source, not Vowfall's art direction. The Epic Games sample supports Unreal Engine 5.5-5.8,
is distributed as a complete project, and is intentionally much larger than this RTS needs. Importing
the whole project would add unrelated gameplay, plugins, world-partition data, shaders, and recognizable
biomes while making review and performance work harder.

Curate only:

- Faceted cliff and field-rock families from the darker biomes
- Dead trunks, roots, restrained forest floor dressing, and generic grass silhouettes
- Generic fieldstone, rubble, timber, plank, iron, mud, bark, and wet-stone surfaces
- Small structural modules that can be recomposed into original Vowfall silhouettes

Do not import:

- The 8 by 8 km world, gameplay code, Mover character, plugins, maps, or World Partition data
- Characters, signature landmarks, complete buildings, or culturally specific roof silhouettes
- Luminous crystals, giant mushrooms, volcanic emissives, saturated fantasy foliage, or bright palette
- Materials that cannot be reduced to the Vowfall master-material contract

Every selected asset must be renamed, normalized, recolored, and recomposed. Titan supplies production
craft and useful generic building blocks; it must not make Vowfall look like a Titan biome or asset flip.

## License boundary

Project Titan currently uses the Fab Standard License. That license permits use and modification inside
a project and private sharing with collaborators, but prohibits standalone redistribution. Therefore:

- Each collaborator acquires the free listing through their own Fab account.
- Raw and migrated licensed files live under `Content/External`, which Git ignores.
- Licensed source assets are shared only through an approved private project store.
- Public GitHub contains code, canonical names, source records, and original fallback assets only.
- Packaged builds may include approved assets as inseparable game content.

This record is an engineering policy, not legal advice. Recheck the listing and binding license when the
game enters commercial production.

## Canonical content contract

The local root is `/Game/External/VowfallEnvironmentKit` and can be changed in Project Settings under
**Game > Vowfall Environment Kit**. Asset names and paths are defined by `AshenEnvironmentKit.cpp`.

Mesh contract:

- Unreal units are centimeters; the canonical source envelope is 100 by 100 by 100 cm.
- Pivots and forward axes must match the corresponding Engine fallback before migration.
- Rigid rocks, cliffs, masonry, and structural modules need Nanite or at least three useful LODs.
- Foliage stays non-Nanite for the beta path and needs at least four LODs with stable silhouettes.
- Material slots are consolidated before intake; decorative geometry never supplies gameplay collision.
- Shadow casting is reserved for silhouette-scale geometry and disabled for small ground clutter.

Texture contract:

| Suffix | Data | Unreal settings |
| --- | --- | --- |
| `_BC` | Base color | sRGB on, alpha removed unless required |
| `_N` | Tangent-space normal | Normal-map compression, sRGB off |
| `_ORM` | R: AO, G: roughness, B: material variation | Masks compression, sRGB off |

Use 2K textures for ordinary terrain and structures, 1K for small props and foliage, and 4K only for a
small number of broad terrain or cliff surfaces that show measurable improvement at the normal RTS
camera. All textures require mipmaps and streaming. Vowfall material instances remove bright emissive,
lower saturation, preserve mid-value unit contrast, and keep blood-red accents scarce.

## Intake sequence

1. Add Project Titan to the artist's Fab library and create it as a separate UE 5.8 project at
   `C:\UnrealProjects\ProjectTitanSource`.
2. Review its assets in an overview level; do not migrate folders in bulk.
3. Import or migrate one approved dependency-complete asset family into its raw local source folder.
4. Enable the built-in Geometry Scripting plugin in the source project, then run
   `Build/EnvironmentKit/intake_project_titan.py` against it. The script duplicates only the approved
   surface triplet, caps it at 2K, and bakes all five LODs of the three approved cliff meshes into a
   100 cm canonical envelope under `/Game/External/VowfallEnvironmentKit`.
5. Copy the resulting canonical folder into Vowfall's matching ignored `Content/External` folder.
6. Run `Build/EnvironmentKit/intake_free_surfaces.py` inside Vowfall for the other approved free surfaces.
7. Normalize mesh names, 100 cm envelope, pivot, material slots, LOD/Nanite, and collision.
8. Run `Build/EnvironmentKit/audit_environment_kit.py` through `UnrealEditor-Cmd.exe`.
9. Capture both the battle camera and whole battlefield and compare unit readability and frame cost.
10. Record the exact source asset and transformation in `environment-kit.json` before approval.

The intake script path must use forward slashes on the Unreal command line. Backslashes can turn the
`\r` in a folder such as `rts` into a carriage return:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  '<repo>\unreal\AshenDominion\AshenDominion.uproject' `
  -unattended -nop4 -nosplash -nullrhi `
  '-ExecutePythonScript=<repo>/unreal/AshenDominion/Build/EnvironmentKit/intake_free_surfaces.py'
```

The first acceptance group is cliff/rock, dead forest, moor/mud/stone/bark/wood textures, road dressing,
and bridge timbers. Faction castles retain Vowfall-authored silhouettes even when generic source textures
or small modules are reused.

## Current free acquisition set

The first local material pass is integrated and intentionally limited to assets that were free or already
entitled on the project account:

| Acquired source | Canonical target | Current use |
| --- | --- | --- |
| [Forest Floor](https://www.fab.com/listings/3463d6dc-43fb-4bb2-9d54-9590cf23257e) | `T_Moor` | Battlefield terrain; `MoorPatch` falls back to this family |
| [Soil Mud](https://www.fab.com/listings/c33cb641-e9bd-4966-adc3-c0b5a937ab12) | `T_Mud` | Continuous flank trails, main-lane shoulders, ruts, and wet shore |
| Dirty Stone Tiles local import | `T_RoadStone` | Aged central causeway; wet and structural stone slots may fall back to it |
| [Weathered Wooden Planks](https://www.fab.com/listings/943bdc90-e4c2-4a63-8ca0-1b9556f933dd) | `T_WeatheredWood` | Timber bridge decks and mine supports |
| [Project Titan](https://www.fab.com/listings/c05aac82-4c1a-4e42-96b3-be668dc40fca), Marshlands Mossy Rock Medium | `T_Foundation` | Blackridge mountain rock and weathered human foundations; selected as a complete `D/N/ORM` family with no signature-biome dependency |
| Project Titan, Marshlands Rocks 2-4 | `SM_MountainCliff_A/B/C` | Three complementary Blackridge shelf, peak, and broken-block silhouettes; all retain five source LODs while collision and source materials are removed |

The raw Fab folders and the normalized `/Game/External/VowfallEnvironmentKit` copies stay local and are
ignored by Git. Public source contains only the intake script, provenance, canonical contract, material
bindings, and deterministic fallbacks.

The UE 5.8 launcher manifest observed during this pass required 65.62 GB of free installation space for
the complete Titan sample. A workstation without that headroom must use an approved private project store
or selectively retrieve only verified entitled packages; it must not add raw or unverified binaries to
Git. Vowfall's first intake remains deliberately limited to one texture triplet and three reviewed meshes.

## Blackridge cliff selection

The first production mesh pass uses `SM_Marshlands_Rock_2`, `_3`, and `_4`. All three are generic,
approximately 400-vertex LOD0 meshes with five authored LODs, but their broad shelf, tall peak, and
asymmetric block silhouettes serve different compositional roles. A deterministic three-way distribution
breaks repetition along the ridge spine, scatter field, and mine dressing without adding draw-call-heavy
unique actors.

The large chunky cliff and rock candidates were rejected because they expose only one LOD and read as
monolithic set pieces. The medium mossy rock was rejected as a mountain mesh because its flat profile
duplicates the field-rock role. The approved meshes are baked through Unreal Geometry Scripting, scaled
to a 100 cm maximum dimension, stripped of source material dependencies, and assigned Vowfall's
`FoundationStone` texture family through a lifted, low-specular Blackridge style at runtime. Human
foundations retain their own darker style. Nanite remains disabled: five LODs on roughly 400 source
vertices are cheaper and easier to profile at the RTS camera.

Do not purchase replacement packs for this pass. Future candidates are added only when they are free or
already entitled, visually coherent with painterly realism, and useful enough to justify their shader,
storage, and review cost.
