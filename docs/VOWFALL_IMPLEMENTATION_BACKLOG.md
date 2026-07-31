# Vowfall Implementation Backlog

Risk levels are Low, Medium, High, or Critical. “Files” names the current or proposed
repository modules, not a promise that every listed module changes in one patch.

## Now

### N1 — Explicit faction identity

- **Goal:** Separate faction, player slot, and control source through core and Unreal.
- **Files:** `Types.hpp`, `Simulation.*`, `AshenTypes.h`, `AshenEntityActor.*`,
  `AshenControlPointActor.*`, `AshenSimulationSubsystem.*`, `AshenHUD.cpp`.
- **Dependencies:** Existing `FactionId`, `PlayerState`, and catalog.
- **Acceptance:** Swapped and mirror matches produce correct entity definitions and
  presentation faction keys; AI/human assignment does not change faction.
- **Tests:** Player/faction permutation, mirror configuration, control-source
  independence, duplicate hash run, Unreal bridge compile when available.
- **Risk:** Medium.

### N2 — Deterministic system contract and first extraction

- **Goal:** Publish the target system order and extract Resolve behind explicit
  immutable inputs and ordered outputs without rebalancing it.
- **Files:** new `ResolveSystem.*`, `Simulation.*`,
  `VOWFALL_TECHNICAL_ARCHITECTURE.md`.
- **Dependencies:** Spatial grid and current catalog.
- **Acceptance:** Current Resolve results remain equivalent for representative
  fixtures; threshold transitions are observable; no Unreal dependency.
- **Tests:** Exact Resolve fixture, threshold classification, duplicate run.
- **Risk:** High.

### N3 — Typed simulation events

- **Goal:** Provide a portable ordered event stream and stable event digest.
- **Files:** new `SimulationEvent.*`, `Simulation.*`, Unreal event view conversion.
- **Dependencies:** Stable entity/content IDs.
- **Acceptance:** Current spawn, damage, wound, kill, destruction, objective, ability,
  resolve, and minimal Vow transitions emit typed events in documented order.
- **Tests:** Event IDs/order, payloads, digest, replay equality, non-mutating reads.
- **Risk:** High.

### N4 — Validated content foundation

- **Goal:** Add stable metadata and native validation for all required content
  categories without replacing the whole compiled catalog.
- **Files:** new `Content.*`, `Catalog.*`, focused tests.
- **Dependencies:** Existing enums and static definitions.
- **Acceptance:** Built-in registry validates; corrupted copies detect duplicate IDs,
  missing references, invalid prerequisites, values, capabilities, faction links,
  visual keys, and cycles.
- **Tests:** One focused test per validation class.
- **Risk:** Medium.

### N5 — Indexed entity lookup

- **Goal:** Replace repeated linear entity-ID scans while preserving ordered iteration.
- **Files:** `Simulation.*`.
- **Dependencies:** Monotonic `EntityId`.
- **Acceptance:** Spawn, command lookup, deletion, invalid access, compaction, and
  replay work; IDs are not reused; side index is not an iteration source.
- **Tests:** Lookup/deletion/non-reuse/invalid ID and duplicate state hash.
- **Risk:** Medium.

### N6 — Deterministic spatial grid

- **Goal:** Add an exact, stable uniform-grid query and migrate Resolve broad scans.
- **Files:** new `SpatialGrid.*`, `ResolveSystem.*`, `Simulation.*`.
- **Dependencies:** Indexed entity lookup.
- **Acceptance:** Query results are sorted by ID, exact at cell/radius boundaries, and
  independent of insertion order; Resolve behavior remains covered.
- **Tests:** Cell assignment, exact radius, ordering, rebuild, migrated Resolve.
- **Risk:** High.

### N7 — Resolve threshold model

- **Goal:** Name Resolve states and expose transitions while keeping current scalar
  balance.
- **Files:** `Types.hpp`, `ResolveSystem.*`, `SimulationEvent.*`, `Simulation.*`.
- **Dependencies:** N2 and N3.
- **Acceptance:** Steady/Strained/Wavering/Broken classification is deterministic;
  `Rallied` is reserved for explicit recovery; transitions hash and replay.
- **Tests:** Boundary values and event transitions.
- **Risk:** Medium.

### N8 — Minimal authoritative bridge Vow

- **Goal:** Prove Vow data, commands, lifecycle, events, hash, and affected-party
  amendment authorization.
- **Files:** `Types.hpp`, `Content.*`, `SimulationEvent.*`, `Simulation.*`.
- **Dependencies:** N3 and N4.
- **Acceptance:** Native tests can make then keep, break, or amend the bridge Vow;
  maker-only private amendment is rejected.
- **Tests:** Each lifecycle resolution, invalid authority, replay/hash equality.
- **Risk:** Medium.

### N9 — Persistent AI strategic state

- **Goal:** Add intention/opening/economy/composition/timing/route/opponent/confidence/
  abort/contingency/evidence state without changing current utility decisions.
- **Files:** new `AIStrategyState.*` or `CommanderAI.*`, `AIDecision.hpp`,
  self-play hashing.
- **Dependencies:** Observation boundary and content AI metadata.
- **Acceptance:** State updates deterministically from sanitized observations, resets
  correctly, and is included in commander/state hashes.
- **Tests:** Duplicate observations, hidden-state invariance, faction/control-source
  permutations.
- **Risk:** Medium.

### N10 — Performance baseline

- **Goal:** Produce repeatable 100/250/500/1,000 entity measurements.
- **Files:** new `native/benchmark/SimulationPerformance.cpp`,
  `native/CMakeLists.txt`.
- **Dependencies:** Public simulation/spatial APIs.
- **Acceptance:** Tool reports step, spatial query, visibility, AI, hash, and
  approximate memory where practical; no machine-specific correctness threshold.
- **Tests:** Tool runs and produces every population row.
- **Risk:** Low.

## Next

### X1 — Versioned snapshot and replay format

- **Status:** In progress.
- **Goal:** Save and restore all authoritative state and verify replay events/hashes.
- **Complete:** Portable little-endian SnapshotV1, schema/content/pipeline
  compatibility checks, bounded/checksummed loads, exact checkpoint restore, derived
  index rebuild, and deterministic AI continuation tests.
- **Remaining:** Replay container/verifier, explicit future-version migrations,
  native inspection tooling, and the Unreal save-game adapter.
- **Files:** new `Snapshot.*`, `Replay.*`, `Simulation.*`, native tools, Unreal save
  adapter.
- **Dependencies:** All Now state schemas and stable content digest.
- **Acceptance:** Restore at a checkpoint produces the same next commands, events,
  AI state, and final hash as uninterrupted play; compatible migrations are explicit.
- **Tests:** Round trip, corrupted version/content/pipeline/payload, checkpoint
  continuation (complete); replay verification (remaining).
- **Risk:** Critical.

### X2 — Scenario/objective system

- **Goal:** Move mission state and objectives out of `SimulationConfig`/HUD text into
  validated authoritative scenario data.
- **Files:** new `Scenario.*`, `ObjectiveSystem.*`, `Campaign.*`, bridge mission data.
- **Dependencies:** Content, events, snapshot.
- **Acceptance:** Objective changes are headless, event-driven, saveable, and cannot
  soft-lock in native mission fixtures.
- **Tests:** Contested/captured/failed/succeeded objectives and restore.
- **Risk:** High.

### X3 — Road Ledger supply graph

- **Goal:** Implement physical Compact supply connection and allocation.
- **Files:** new `SupplySystem.*`, road/relay/cart/hospital definitions, mission data.
- **Dependencies:** Scenario, spatial queries, events, snapshot.
- **Acceptance:** Cutting and restoring a route deterministically changes legal
  reinforcement/recovery/construction/retreat capabilities.
- **Tests:** Graph tie-breaking, capacity, bridge destruction, reconnect, replay.
- **Risk:** Critical.

### X4 — Casualty and recovery identities

- **Goal:** Retain wounded/incapacitated/recoverable/missing/dead state and history.
- **Files:** new `CasualtySystem.*`, identity/history definitions, hospital/evacuation.
- **Dependencies:** Combat resolution, supply, snapshot, events.
- **Acceptance:** Recovery preserves identity, formation, experience, injuries, and
  memory; `No One Left Uncounted` modifies eligibility rather than generic health.
- **Tests:** Every state transition, route loss, hospital capacity, restore.
- **Risk:** Critical.

### X5 — Persistent formations and cohesion

- **Goal:** Replace temporary formation destinations with named formations and
  threshold cohesion.
- **Files:** new `FormationSystem.*`, commands, content, AI squad state.
- **Dependencies:** Spatial grid, casualties, combat interfaces.
- **Acceptance:** Formation identity, facing, ordered slots, break/reform/retreat, and
  AI use are deterministic.
- **Tests:** Stable slots, break/rally, casualty membership, replay.
- **Risk:** High.

### X6 — Projectile and ability phases

- **Goal:** Replace instant ranged hits and generic powers with deterministic windup,
  projectiles, telegraphs, interrupts, and recovery.
- **Files:** new `CombatTargetingSystem.*`, `ProjectileSystem.*`, `AbilitySystem.*`.
- **Dependencies:** System extraction, events, content, spatial grid.
- **Acceptance:** Projectile launch/impact and ability start/interrupt order are
  headless and reproducible.
- **Tests:** Tie-breaking, interception/interrupt, simultaneous outcomes, replay.
- **Risk:** Critical.

### X7 — Ascendancy Absolution

- **Goal:** Implement irreversible specialization and Completion.
- **Files:** new `TransformationSystem.*`, command capability projection, AI strategy.
- **Dependencies:** Abilities, snapshot, presentation registry, resolve memory.
- **Acceptance:** Transformation removes options, changes commands/visuals/resolve,
  preserves identity, and cannot be silently reversed.
- **Tests:** Start/interrupt/complete, permanent cost, save/replay, AI legality.
- **Risk:** Critical.

### X8 — Bridge mission substrate

- **Goal:** Implement bridge health/destruction, convoy, hospital, Dread escalation,
  testimony, and public Vow conditions as one headless scenario.
- **Files:** bridge scenario data, scenario/objective/institution systems.
- **Dependencies:** X1–X7 as applicable.
- **Acceptance:** A deterministic accelerated mission fixture reaches every major
  branch without Unreal.
- **Tests:** Keep/break/amend paths, convoy losses, bridge states, checkpoint recovery.
- **Risk:** Critical.

## Later

### L1 — UMG/CommonUI presentation

- **Goal:** Replace Canvas command/mission UI with data-driven localized view models.
- **Files:** Unreal UI module/assets, input and accessibility settings.
- **Dependencies:** Stable content and event/view contracts.
- **Acceptance:** Primary workflows work at supported resolutions with rebindable input,
  focus navigation, subtitles, scaling, and redundant critical cues.
- **Tests:** Unreal automation, resolution captures, accessibility checklist.
- **Risk:** High.

### L2 — Production faction presentation registry

- **Goal:** Resolve full visual identity tuple to production assets and effects.
- **Files:** Unreal data assets, entity presentation components, art content.
- **Dependencies:** Final roster and transformations.
- **Acceptance:** No owner-index or human/monster fallback determines faction visuals.
- **Tests:** Asset validation matrix and mirror captures.
- **Risk:** High.

### L3 — Elder Concord

- **Goal:** Implement treaties, living infrastructure, terrain relationships, and
  obligations after the slice.
- **Files:** future content and systems.
- **Dependencies:** Stable supply, objectives, Vows, formations, save/replay.
- **Acceptance:** Treaty slots alter roster and obligations without player-index or
  map-script assumptions.
- **Tests:** Treaty graph, roster legality, terrain changes, AI.
- **Risk:** Critical.

### L4 — Authoritative multiplayer

- **Goal:** Dedicated server, buffering, snapshots, reconnect, replays, desync tools.
- **Files:** future networking/server modules.
- **Dependencies:** Versioned snapshot/replay and stable deterministic pipeline.
- **Acceptance:** Remote matches remain synchronized under latency/jitter/loss tests.
- **Tests:** Network simulation matrix and hostile command validation.
- **Risk:** Critical.

## Explicitly frozen

### F1 — TypeScript/Three.js prototype

- **Goal:** Preserve design provenance only.
- **Files:** `src/`, `parity/` except required native scenario maintenance.
- **Dependencies:** None.
- **Acceptance:** New authoritative gameplay does not land in TypeScript.
- **Tests:** Native CI remains the production gate.
- **Risk:** Low.

### F2 — Broad environment-kit replacement

- **Goal:** Avoid destabilizing current playable terrain while gameplay foundations
  change.
- **Files:** `unreal/AshenDominion/Content/Art/Environment/VowfallKit`,
  environment build scripts.
- **Dependencies:** Later art/performance review.
- **Acceptance:** Only compile-preserving faction plumbing changes touch presentation.
- **Tests:** Existing environment audit when Unreal is available.
- **Risk:** Medium.

### F3 — Full HUD rewrite

- **Goal:** Keep Canvas UI until stable view models and content/event contracts exist.
- **Files:** `AshenHUD.*`, `AshenPlayerController.*`.
- **Dependencies:** L1.
- **Acceptance:** Phase 1 only corrects identity/event plumbing and compilation.
- **Tests:** Existing UI automation when available.
- **Risk:** Medium.

## Removed

### R1 — Mercy Engine

- **Goal:** Keep it out of Vowfall's conflict and mechanics.
- **Files:** Any future design or mission content.
- **Dependencies:** None.
- **Acceptance:** No magic machine resolves the political conflict.
- **Tests:** Narrative review.
- **Risk:** Low.

### R2 — Generic faction powers

- **Goal:** Retire global heal and spawned-unit powers when their real systems exist.
- **Files:** `Catalog.cpp`, `Simulation::apply_activate_power`, content/UI.
- **Dependencies:** Casualties and transformation systems.
- **Acceptance:** Compact uses `No One Left Uncounted`; Ascendancy uses `Completion`.
- **Tests:** System-specific ability fixtures.
- **Risk:** High.

### R3 — Owner-index faction presentation

- **Goal:** Permanently remove `OwnerIndex == 0` as a silhouette or palette decision.
- **Files:** Unreal entity, control-point, and HUD presentation.
- **Dependencies:** N1.
- **Acceptance:** Static audit and mirror-match capture find no owner-to-faction visual
  mapping.
- **Tests:** Presentation metadata and Unreal mirror capture.
- **Risk:** Medium.

### R4 — Runtime machine-learning commander

- **Goal:** Exclude opaque runtime ML from authoritative decisions.
- **Files:** AI runtime.
- **Dependencies:** None.
- **Acceptance:** AI remains deterministic, auditable, fog-limited, and command-legal.
- **Tests:** Existing observation, decision trace, and duplicate-run audits.
- **Risk:** Low.
