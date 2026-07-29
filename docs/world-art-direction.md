# Vowfall world art direction

## Visual target

Vowfall uses **painterly realism**: physically plausible scale, lighting, materials, weather, and surface
response, composed with deliberate silhouettes and restrained color separation for an overhead RTS
camera. It is neither toy-like stylization nor unfiltered photorealism.

Photoreal assets alone create excessive ground noise and hide units at normal play distance. Bright
fantasy stylization improves readability but weakens Vowfall's medieval-horror identity. Painterly
realism keeps believable stone, wood, iron, water, soil, and vegetation while controlling detail where
players make decisions.

The fiction may evoke the dread, bodily cost, and medieval vastness associated with dark fantasy, but
must not reproduce protected characters, costumes, heraldry, creatures, locations, panels, or symbols.
Historical fortification, plague-era material culture, megaliths, battlefield archaeology, and natural
landscapes are the primary visual references.

## Composition rules

- Traversable lanes are quieter, warmer, and smoother than their surroundings. Combat silhouettes must
  remain readable without outlines.
- Forests, cliffs, water, and fortifications communicate gameplay boundaries before decorative detail.
- The river is dark green-black water with soft reflection and restrained movement. Bright cyan water,
  straight ripple stripes, and tropical saturation are outside the palette.
- Terrain detail works at three scales: broad biome variation, route-scale wear, and close material
  breakup. No square decal patches or repeated high-contrast tiles.
- Foliage is dense at the map perimeter and sparse near command spaces, crossings, resource fields, and
  control points. Canopies must not conceal selectable units on legal paths.
- Lighting preserves shadow shape without crushing information. Faction accents are local light, never a
  full-screen color grade.

## Battlefield geography

- Blackridge occupies the northwest. It is a single mine-bearing ridge with a visible cliff spine, two
  concealed adits, and a rear road that supports ambushes. Loose scree supports the silhouette but never
  substitutes for the landform. Broad shelf, tall peak, and broken-block meshes alternate deterministically
  so the massif reads as geology rather than a repeated prop. Its adits signal neutral, contestable reserves;
  they are not the human starter field.
- Gravewood occupies the southeast. Its dense canopy, roots, dead trees, and two spirit caches are the
  rotational gameplay counterpart to Blackridge, while its visual language remains organic rather than rocky.
  Five sparse gnarled-tree anchors and six hero stumps break the conifer mass; low root shapes stitch those
  anchors into the ground without becoming path obstacles. Leaf cards are removed from the deadwood family
  so its silhouette stays skeletal and units remain readable against the canopy.
- The starter cursed-iron fields occupy open, rotationally mirrored pockets: southwest of the human gate and
  northeast of the monster gate. Both fields keep equal base travel distance and remain clear of major
  landmarks, flank roads, and dense sightline blockers.
- The river enters beyond the north terrain overscan and leaves beyond the south overscan. The water mesh,
  terrain cut, bank line, and crossing centers use the same curve.
- The north and south flank lanes cross on timber bridges. The main lane crosses on a broad old stone
  causeway. Bank dressing, reeds, ruins, and shrine props maintain a clear radius around all three crossings.
- Roads are continuous between each base gate and crossing. A visible break is allowed only when an authored
  bridge, ford, collapse, or deliberate terrain transition explains it. Terrain-following ribbons use
  continuous UVs, subdued width variation, and wheel wear rather than repeated rectangular road pieces.
- The Drowned Wayshrine sits beside the central route, never inside it. Its ruins are an orientation landmark,
  not an obstacle or an ambiguous cluster of stretched props.

## Faction shape language

**Cinder Compact**

Load-bearing stone, iron bands, steep roofs, buttresses, ordered crenellation, narrow gates, maintained
roads, and warm brazier light. Repetition and vertical alignment communicate discipline under pressure.
The palette is limestone grey, soot black, oxidized iron, dried red cloth, and furnace amber.

**Hollow Choir**

Asymmetric masses, rib-like supports, stretched surfaces, bone palisades, hooked spires, wounds used as
openings, and low crimson hearths. Structures should feel grown through sacrifice rather than assembled,
without copying any existing dark-fantasy creature or symbol. The palette is coagulated red, bruised
black, old bone, wet umber, and sparse arterial light.

**Veiled Kin**

Weathered megaliths, living roots, carved negative space, impossible balance, and blue-green ritual
light. Their forms should feel older than either army and neither cleanly benevolent nor monstrous. The
palette is lichen stone, peat, moonlit silver, muted jade, and cold mineral light.

## Material contract

The master world surface exposes base, secondary, and accent color; macro and detail scales; detail
strength; roughness; specular response; and ambient occlusion. Static environment pieces use this common
response so a castle, tree, rock, and road still belong to one weather system. Water uses a separate
translucent master with animated normal detail, shallow/deep color separation, and controlled opacity.

Procedural noise is appropriate for iteration, but production surfaces should bake approved variation
into texture sets or runtime virtual textures after the asset kit is stable. This prevents an expensive
shader from being multiplied across the final battlefield.

## Asset intake

Fab, Megascans, or custom DCC assets enter the project only when all of these are true:

- The license permits use in this Unreal project and provenance is recorded.
- Scale, texel density, roughness range, saturation, and weathering match this document.
- Static meshes have deliberate pivots, simple collision, valid lightmap data where needed, and tested
  Nanite or authored LOD behavior.
- Foliage has wind response, distance behavior, shadow cost, and an RTS-safe canopy footprint.
- Material instances derive from the Vowfall masters or pass a side-by-side lighting review.
- An asset has a gameplay role or strengthens a landmark. Decorative density alone is not a reason to
  add it.

Mixing realistic scans with soft cartoon geometry is prohibited. A full category is migrated together:
all rocks, all trees in one biome, or one complete castle kit.

## Initial performance budget

Until the beta minimum PC is locked, the battlefield targets 60 fps at 1080p with a 16.6 ms frame,
including simulation and UI. Environment work should preserve headroom for late-game armies:

- Instancing for repeated foliage and modular pieces
- No collision on decorative terrain, foliage, water detail, or light props
- One invisible deterministic interaction plane until authored terrain navigation is validated
- Shadow-casting reserved for silhouette-scale geometry
- Texture streaming and visible geometry measured in representative late-game captures
- Experimental Nanite foliage excluded from the beta path until it passes hardware coverage testing

## Step 3 delivery slices

### 3A. Visual foundation - complete

- Painterly-realism target and faction visual grammar
- Sculpted overscan terrain that keeps legal routes flat and input collision deterministic
- Reproducible surface and water master materials
- Layered forest silhouettes, riverbanks, reeds, roads, ruts, bridges, faction fortifications, ritual
  landmarks, local accent lighting, fog, and post-processing
- Battle, whole-world, and height-aware Blackridge render capture harnesses
- Unreal automation contract for terrain collision and required material assets

### 3B. Production environment kit - in progress

Delivered foundation:

- Forty original, reproducible Vowfall meshes replace every Engine basic-shape category while
  retaining a final Engine fallback
- Semantic mesh and surface slots prefer a curated local production kit without coupling licensed art
  to the public repository or deterministic collision
- The master surface material accepts albedo, normal, and packed AO/roughness textures with explicit
  blend, tiling, normal-strength, and mask-strength controls
- Project Titan selection policy, Fab license boundary, canonical naming, intake manifest, audit tool,
  and Unreal automation contract
- Free forest-floor, soil-mud, dirty-stone, and weathered-plank families are normalized into a local 2K
  kit with semantic fallbacks; raw licensed assets remain outside public source control
- Three normalized Project Titan cliff silhouettes replace the Blackridge production slot locally while
  retaining five authored LODs, Vowfall material response, and collision-free deterministic separation
- One continuous river surface replaces overlapping water planes, with a narrow wet shoreline, sparse
  bank stone, and authored clearances beneath all three crossings
- Continuous procedural road ribbons distinguish the direct aged-stone causeway from smoother dirt flank
  routes without changing deterministic pathing or collision

Remaining production pass:

- Inspect the already acquired Project Titan source project and migrate only approved free dead-forest,
  debris, and generic structural mesh families
- Normalize pivots, collision, Nanite/LOD settings, foliage rules, texture channels, and source-art records
- Capture and profile each category replacement before accepting it over the original Vowfall mesh

### 3C. Authored competitive map

- Convert the approved terrain composition to an authored Landscape and spline workflow
- Lock spawn geometry, expansion travel time, crossings, attack lanes, vision blockers, camera bounds,
  occlusion behavior, and water/navigation agreement
- Run mirrored bot batches and human camera/readability reviews before adding a second map

### 3D. Final environment pass

- Add decals, wetness, weather, VFX, destruction states, ambient motion, audio zones, final lighting,
  shader precompile, scalability tiers, and minimum-spec performance captures

Relevant engine references: [Landscape](https://dev.epicgames.com/documentation/unreal-engine/landscape-overview),
[Water](https://dev.epicgames.com/documentation/unreal-engine/water-system-in-unreal-engine),
[PCG](https://dev.epicgames.com/documentation/en-us/unreal-engine/procedural-content-generation-framework-in-unreal-engine),
and [Nanite Landscapes](https://dev.epicgames.com/documentation/unreal-engine/using-nanite-with-landscapes-in-unreal-engine).
