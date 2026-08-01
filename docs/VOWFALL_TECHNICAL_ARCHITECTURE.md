# Vowfall Technical Architecture

## Current architecture

### Portable authority

`unreal/AshenDominion/Source/AshenCore` is the authoritative C++20 simulation. The
same source files are compiled into a native static library by `native/CMakeLists.txt`
and into the Unreal `AshenCore` module. Gameplay code uses integer world coordinates,
integer fixed-point basis values, a 20 Hz tick, ordered vectors, and explicit
tie-breaking.

`Simulation` currently owns:

- command queue ordering, validation, application, and trace provenance;
- player economy, supply, research, construction, and production;
- A* navigation, movement, formation slot assignment, and unit separation;
- instant-hit unit and defensive combat;
- Dread Tide, scalar resolve, control points, fog, observation memory, and mission
  outcomes;
- validated scenario definitions and deterministic mission-objective state/events;
- `CommanderAI` scheduling and AI decision traces;
- state hashing.

Entities are aggregate values in `std::vector<Entity>`. IDs are monotonic. Lookup by ID
is a linear scan; deletion compacts the vector. Resource and control-point lookup is
also linear, but those collections are currently small.

### AI boundary

`CommanderAI` has no `Simulation` dependency. It consumes an immutable
`PlayerObservation` containing owned state, currently visible or remembered hostile
state, public objectives, explored terrain, known resources, and legal command
capabilities. It emits ordinary `Command` values. Difficulty modifies delayed hostile
knowledge, cadence, bounded decision quality, point precision, memory, and command
latency, never resources or vision.

### Unreal boundary

`UAshenSimulationSubsystem` advances `Simulation` with a fixed accumulator, submits
Player One commands, and synchronizes actor proxies. `AAshenEntityActor`,
`AAshenControlPointActor`, `AAshenHUD`, and `AAshenPlayerController` are presentation
and input.

The current violation is visual: `AAshenEntityActor` chooses its faction silhouette,
palette, orientation, health color, and stencil from `OwnerIndex`. HUD and objective
colors repeat the same assumption. Local ownership checks in input code may remain
slot-based; faction appearance may not.

### Data, scenarios, save, and replay

Gameplay definitions are compiled switches in `Catalog.cpp`; campaign definitions are
a static array in `Campaign.cpp`. `Scenario.cpp` now owns validated stable-ID runtime
objective definitions for skirmish, PvP, and the playable Bridge of Names mission.
Definitions can form a validated acyclic prerequisite graph and distinguish required,
optional, and final-primary objectives. `ObjectiveSystem` evaluates their
success/failure triggers headlessly, resolves active stages in definition order, then
activates newly unblocked stages in a second definition-order pass. Bridge currently
requires Player One to secure every map control point before its timed crossing hold
begins. Required objectives may not depend on optional ones; any required failure
takes precedence, and victory requires every required objective to succeed. Unreal
reads the resulting stage-aware core view instead of constructing
objective text from HUD state. `SimulationConfig` still selects the scenario and
embeds the skirmish map size, obstacles, starting factions, starting resources, and
starting forces.

The native parity runner can construct a scenario and serialize a JSON result. The
self-play harness records command and AI traces plus periodic hashes and duplicates
runs to detect nondeterminism. `SnapshotV1` is the portable authoritative checkpoint
format: it round-trips simulation configuration, authoritative state, fog and AI
memory, queued commands, audit traces, events, stable-ID cursors, and the event digest.
Entity lookup and spatial cells are derived and rebuilt after load. `ReplayV1` embeds
that checkpoint, records subsequent external submissions, and verifies regenerated
AI commands, events, checkpoints, and final state. There is not yet an Unreal
checkpoint browser, replay playback flow, or migration from a prior schema. The
current Unreal adapter wraps SnapshotV1 in `USaveGame`, atomically swaps only a
validated restore, records player submissions, and exports ReplayV1 only after
in-memory verification succeeds.

Mission-objective runtime status and activation-relative deadlines are deterministic
derived projections of the scenario definition and the persisted ordered
`MissionObjectiveChanged` history. SnapshotV1 rebuilds that projection and rejects a
payload when its events do not reproduce the live objective state. The transition
event is stamped with the pre-increment simulation tick, so its authoritative state
transition tick is `event.tick + 1`. Scenario definitions participate in the content
digest, so a save from an older scenario revision is rejected as incompatible content
rather than read under changed objective rules. A later bounded event history or
objective progress that changes without a transition will require explicit snapshot
fields and a new schema.

## Current deterministic step order

The observable Phase 0 order in `Simulation::step()` is:

1. Apply all due commands; validation happens inside application.
2. Decrement entity attack cooldowns and player power cooldowns.
3. Update Dread Tide.
4. Update research and apply completed bonuses.
5. Update production and spawn completed units.
6. Update control-point capture and income.
7. Recompute resolve.
8. Execute orders, including movement, gathering, construction, and instant-hit unit
   attacks; then run separation.
9. Acquire idle-unit targets.
10. Resolve defensive-building attacks.
11. Remove dead entities and adjust supply.
12. Refresh visibility.
13. Evaluate scenario objectives and mission outcome (or legacy victory for an
    unsupported scenario).
14. Increment the tick.
15. Refresh observation memory.
16. Ask enabled commanders to plan and queue ordinary commands.

This order is authoritative legacy behavior. It is intentionally documented before
being changed.

## Target architecture

The target simulation pipeline is:

1. Command validation.
2. Command application.
3. Scenario processing.
4. Economy.
5. Construction.
6. Production.
7. Research and doctrine.
8. Navigation.
9. Movement.
10. Formation and cohesion.
11. Combat targeting.
12. Projectile and ability resolution.
13. Casualty processing.
14. Resolve.
15. Territory and objectives.
16. Visibility and memory.
17. Vow processing.
18. Victory and defeat.
19. Event finalization.
20. State hashing/checkpoint sampling.

Each extracted system receives an explicit immutable input view and returns ordered
updates or events. `Simulation` applies those outputs in the declared phase. A system
cannot retain mutable static state or use an unordered container as an iteration
source.

Phase 1 does not reorder all legacy work. It establishes the phase contract and
extracts Resolve first because its inputs and outputs can be made explicit without
changing command timing. Later migrations must add equivalence fixtures before moving
a responsibility to a new phase.

## Entity identity and lookup

Canonical entity iteration remains the ordered `entities_` vector. A derived side
index maps `EntityId::value` to the current vector slot:

- IDs remain monotonic and are not reused.
- Spawn appends an entity and updates the side index.
- Destruction invalidates dead IDs, compacts canonical storage, and rebuilds the side
  index in canonical order.
- Lookup never exposes the side index as an iteration source.
- The side index is derived and is not hashed.
- Raw `Entity*` values are valid only until a simulation operation that can spawn,
  destroy, reset, or otherwise reallocate canonical storage.

## Spatial queries

The Phase 1 uniform grid is independent of Unreal physics. It is rebuilt from the
canonical entity span at a documented system boundary.

- Integer floor division assigns each position to a cell.
- Cells store stable entity IDs and source indices.
- A query visits a deterministic row-major cell rectangle, exact-filters distance,
  then sorts and de-duplicates by `EntityId`.
- Query order therefore does not depend on insertion history or a hash table.
- Resolve is the first migrated broad query. Target acquisition and separation remain
  legacy behavior until dedicated equivalence and load tests exist.
- The grid is derived state. Its configuration is hashed when it can affect gameplay;
  rebuilt cell contents are not independently hashed because canonical entities are.

## Resolve boundary

The extracted Resolve system consumes:

- current Dread Tide;
- immutable player faction state;
- immutable entities and control points;
- the spatial grid built from the same entity span.

It returns one ordered update per live unit and aggregate player resolve. `Simulation`
applies the values and emits threshold transition events. The current Dread/terror/ward
formula and balance range remain unchanged. Persistent memory, rallies, abandonment,
casualty history, battlefield recognition, and Vow effects are later inputs.

## Event architecture

`SimulationEvent` is a portable typed variant. Every event has:

- a monotonic `EventId`;
- the authoritative simulation tick;
- one event-specific payload containing stable IDs and deterministic numeric values.

The initial vocabulary includes entity spawn/destruction; unit damage, wound, kill,
and recovery; formation creation/break; resolve thresholds; supply connection; Vows;
transformations; testimony; objectives; projectiles; and ability start/interruption.
Phase 1 emits only events backed by current state transitions plus the minimal Vow
lifecycle.

Ordering rules:

1. Events are appended at the exact authoritative transition.
2. Calls inside canonical entity iteration emit in that iteration order.
3. Multi-entity commands sort and de-duplicate entity IDs before transitions.
4. Damage precedes wound or kill; kill precedes later entity destruction.
5. Event IDs break any remaining same-tick ambiguity.

Events never mutate simulation state after emission. Presentation reads an immutable
stream using an external cursor.

The event buffer itself is not recursively folded into every state hash. Instead,
emission updates a rolling deterministic event digest, and `next_event_id` plus that
digest are authoritative hashed state. Reading events cannot change the hash.

## Content-definition strategy

The existing enums remain compact runtime handles during migration. A typed built-in
content registry adds:

- stable numeric ID and version;
- development name;
- localization key;
- presentation key;
- deterministic numeric gameplay fields;
- faction and prerequisite references;
- AI capability metadata.

Validation runs natively and never depends on Unreal asset loading. It checks duplicate
IDs, missing references, invalid prerequisites, negative values, invalid capability
sets, missing factions, malformed presentation keys, deterministic numeric bounds, and
research cycles.

Phase 1 registers representative definitions, including the current factions, roster,
research, powers, an ability/projectile path, formations, an Ascendancy transformation,
the bridge Vow, AI doctrine/strategy metadata, and map objectives. It does not replace
every `Catalog.cpp` switch at once.

## Phase 1 performance evidence

`ashen_simulation_performance` is a threshold-free CSV probe, not a correctness test.
On the same Windows/MSVC Debug build, before and after the entity index plus Resolve
spatial-query migration, it reported:

| Entities | Step before (us) | Step after (us) | Change | Navigation before (us) | Navigation after (us) | Approx. bytes before | Approx. bytes after |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 100 | 7,602 | 6,560 | -13.7% | 7,674 | 6,654 | 166,016 | 220,264 |
| 250 | 35,380 | 26,081 | -26.3% | 34,816 | 25,932 | 216,416 | 281,264 |
| 500 | 127,236 | 89,227 | -29.9% | 123,462 | 93,143 | 330,176 | 416,824 |
| 1,000 | 455,451 | 285,170 | -37.4% | 466,132 | 284,928 | 432,416 | 545,264 |

The capacity-based memory estimate intentionally rises because it now includes the
event buffer and spatial cells. Dedicated point-query timings remained in the 0–4 us
range at this scale and are too small for a stable percentage claim. AI, visibility,
and state-hash timings remain in the CSV. Debug timings are machine-specific; only
same-machine before/after direction is meaningful. The remaining dominant scalability
risk is legacy pairwise unit separation.

## Save and replay implications

Phase 1 changed the state-hash schema by adding explicit entity faction, resolve state,
Vows, AI strategic state, event sequence/digest, spatial configuration, and new command
payloads. SnapshotV1 makes that compatibility boundary explicit.

The fixed, little-endian SnapshotV1 header contains:

- schema and minimum-reader versions;
- digests of the complete built-in content registry, gameplay/AI catalog, and
  deterministic pipeline contract, including ticks per second;
- checkpoint tick and state hash;
- bounded payload size and payload checksum.

The protected payload carries map/scenario identity, match seed,
player/faction/control configuration, difficulty profiles, next stable IDs, all
authoritative state, and the complete command, AI-decision, and event audit histories.
Loads reject incompatible versions, content, or pipeline definitions and malformed,
oversized, truncated, trailing, or checksum-invalid data. V1 has no implicit
best-effort migration.

A restored live AI match is tested to produce the same subsequent commands, events,
AI state, final hash, and SnapshotV1 bytes as uninterrupted play.

ReplayV1 is a separate bounded, checksummed little-endian container. Its header repeats
the schema, content, and deterministic-pipeline compatibility boundary and identifies
the initial/final ticks and state hashes. Its payload contains:

- the exact initial SnapshotV1 checkpoint;
- every subsequent external submission with explicit immediate/queued mode, issue and
  application ticks, sequence, accepted/rejected/pending outcome, and validation code;
- expected command audit records, including regenerated AI provenance;
- event evidence as stable event ID, tick, type, and full event hash, never as
  authoritative state-changing input;
- checkpoint state hash, command/event counts, event digest, and an external-input
  cursor that makes same-tick checkpoint placement unambiguous.

Verification restores the checkpoint, resubmits only external inputs at their recorded
boundaries, lets the normal fog-limited commander regenerate AI work, and compares each
checkpoint followed by the complete command and event audit suffix and final state.
`ashen_replay record|inspect|verify` provides the native inspection boundary. ReplayV1
rejects incompatible definitions and malformed, oversized, truncated, trailing, or
checksum-invalid data. Unreal uses the same validation boundary for its F5/F9 quick
checkpoint, starts a fresh recorder at the checkpoint boundary, samples replay
checkpoints during fixed stepping, and writes F6 exports under `Saved/Replays` only
after deterministic verification. V1 has no best-effort migration; future schema
migration, named checkpoint browsing, and replay playback remain explicit follow-up work.

## Unreal integration boundary

Correct in Phase 1:

- `Entity` carries faction explicitly from authoritative spawn.
- Unreal view values expose owner relationship separately from faction identity.
- Entity and control-point actors resolve palette/silhouette from faction, not slot.
- Typed core events are converted to Blueprint-readable event views.
- Fixed-step advancement and ordinary command submission remain unchanged.

Later:

- A presentation registry resolves
  `(FactionId, archetype, variant, upgrades, damage, transformation, cosmetic/campaign)`
  to assets.
- View models consume state snapshots and events.
- UMG/CommonUI replaces Canvas command and mission UI.
- Input becomes rebindable; text becomes localization-ready; critical state gains
  accessible redundant cues.

## Migration sequence

1. Freeze and test faction identity, state hashing, and existing replay equality.
2. Add the entity index without changing iteration.
3. Add content metadata and validation around current compiled data.
4. Add event IDs, payloads, digesting, and emissions from current transitions.
5. Add the deterministic spatial grid and extract Resolve.
6. Add threshold resolve state and minimal Vow state/commands.
7. Add hashed AI strategic state derived only from observations.
8. Version snapshot/replay formats.
9. Extract construction/production, combat/casualty, objectives, visibility/memory, and
   Vows in that order, with equivalence fixtures at every boundary.
10. Implement mission-specific systems only after the foundation is stable.
