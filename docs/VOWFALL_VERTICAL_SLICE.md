# Vowfall Vertical Slice: The Bridge of Names

## Purpose

The Bridge of Names is Vowfall's first production vertical slice: one 30–45 minute
campaign mission that proves the game's identity through authoritative RTS mechanics.
It is not a reskinned version of the current skirmish. The mission must make logistics,
recoverable people, public promises, irreversible transformation, and remembered
formations matter to battlefield decisions.

The portable C++20 module in
`unreal/AshenDominion/Source/AshenCore` remains the gameplay authority. Unreal owns
input, presentation, audio, UI, and asset resolution. The frozen TypeScript prototype
under `src/` is design provenance and is not a production target.

## Slice scope

The final slice contains:

- The Cinder Compact as the primary playable faction and the Gloam Ascendancy as the
  opponent, without tying either faction to a player slot or control source.
- Four production combat roles per faction, plus workers and mission-specific civilian
  actors.
- A refugee convoy, a hospital, a destructible bridge, recoverable casualties, and
  physical supply routes.
- Resolve thresholds and formation cohesion that produce readable retreats, breaks,
  recoveries, and last stands.
- One public Vow to keep the bridge open. It can be kept, broken, or amended through
  authoritative commands and mission conditions.
- One Ascendancy Absolution decision, one Dread escalation, and testimony that changes
  what the commander knows rather than changing a morality score.
- Named persistent formations whose casualties, injuries, experience, and relevant
  memories survive checkpoints.
- Save, checkpoint restore, deterministic command replay, and desync validation.
- Three fair campaign AI difficulties built from observation delay, decision quality,
  memory, and execution limits—not resources, vision, or hidden state.
- A production-ready data and UI boundary even where Phase 1 still uses placeholder
  visuals and Canvas HUD elements.

## Gameplay pillars

### Institutions are battlefield systems

The Compact's strength comes from roads, relays, carts, kitchens, signals, hospitals,
and evacuation routes. Losing a route is not a passive debuff; it changes which people
can be supplied, reinforced, recovered, or safely withdrawn.

### Power closes possibilities

Ascendancy units become stronger by accepting irreversible specialization. A
transformation changes capabilities, silhouette, resolve behavior, personal history,
and future choices. It is never represented as a generic stat upgrade.

### Orders create public obligations

The bridge Vow is a simulation object with a speaker, affected parties, terms,
evidence, lifecycle, and consequences. Amendment requires an affected party's
participation. A private UI reinterpretation cannot amend an authoritative Vow.

### Combat leaves legible history

Damage, casualties, formation breaks, recoveries, threshold changes, interrupted
abilities, objective changes, and mission decisions generate ordered typed events.
Presentation and narrative react to those events without becoming authoritative.

### Determinism is a product feature

Every mission failure, tactical story, checkpoint, and AI decision must be reproducible
without launching Unreal. Entity identity, content identity, system order, spatial
queries, event order, and tie-breaking are explicit.

## Explicit non-goals for Phase 1

- The complete 30–45 minute mission, dialogue, cinematics, final terrain, final art,
  voice, music, or accessibility pass.
- The complete Compact or Ascendancy roster and final balance.
- The Elder Concord implementation.
- Full casualty recovery, the complete road/cart/hospital/bridge supply network,
  cohesion simulation, projectiles, facing, cover, charges, bracing, suppression,
  pursuit, or transformation gameplay. Stable unit identity and retained
  wound/incapacitation/recoverability/death history and fixed recovery-window
  eligibility now exist, but evacuation, hospitals, missing outcomes, actual recovery,
  and re-embodiment do not. The first Compact structure-node graph and reinforcement,
  construction, and assisted-retreat consumers are implemented headlessly.
- A UMG/CommonUI HUD replacement.
- Named checkpoint selection or replay playback in this pass. Unreal now provides a
  SnapshotV1 quick checkpoint/restore path and verified ReplayV1 export, while a
  browser and playback controls remain later production UI.
- Multiplayer, matchmaking, networking expansion, seasonal systems, cosmetics,
  procedural campaign growth, machine-learning AI, or mod support.
- The Mercy Engine, a morality meter, prophecy, chosen-one structure, secret bloodline,
  giant final boss, or perfect ending.

## Required systems and repository dependencies

| System | Current repository foundation | Slice requirement |
| --- | --- | --- |
| Authority | `AshenCore::Simulation`, commands, state hash | Preserve and split behind explicit deterministic boundaries |
| Factions | `PlayerState::faction`, faction-aware `Catalog.cpp` | Carry faction through every entity and presentation lookup |
| Content | Compiled switches in `Catalog.cpp` and `Campaign.cpp` | Stable IDs, versions, reference validation, localization and presentation keys |
| Events | Typed ordered gameplay events with casualty identity/state payloads | Extend the same stream to later transitions |
| Entities | Ordered `std::vector<Entity>`, indexed runtime IDs, persistent unit identity ledger | Preserve identity through recovery/re-embodiment |
| Space | Visibility grid plus broad entity scans | Deterministic uniform spatial query service |
| Resolve | Per-tick scalar recomputation | Threshold state now; persistent memory later |
| Vows | `StoryMissionDefinition::public_vow` text | Authoritative lifecycle, commands, events, save/replay contract |
| AI | Fog-limited `PlayerObservation` and `CommanderAI` | Persistent strategic state derived only from observations |
| Unreal | Fixed-step subsystem, actor proxies, Canvas HUD | Faction/content/event plumbing; later view models and UMG |
| Replay | SnapshotV1 restore, Unreal quick-save adapter, ReplayV1 external-input recording, verification, and export | Named checkpoint browser and replay playback later |

## Phase 1 acceptance criteria

Phase 1 is accepted when:

1. Any supported faction can occupy either player slot and can be controlled by a
   human or `CommanderAI` without changing its gameplay or presentation identity.
2. Mirror matches resolve faction and archetype presentation data correctly.
3. The simulation exposes the documented target system order and at least one existing
   system runs behind a pure, explicit input/output boundary.
4. Events have stable IDs, deterministic order, portable payloads, a documented hash
   policy, and tested emissions from current authoritative transitions.
5. Built-in content has stable metadata and validates duplicate IDs, references,
   prerequisites, costs/durations, capabilities, faction links, presentation keys,
   deterministic values, and research cycles.
6. Entity lookup is indexed while canonical entity iteration remains stable and
   ordered.
7. Spatial queries are exact-filtered and return stable `EntityId` order. At least one
   broad gameplay query uses them without an undocumented behavior change.
8. Resolve has named threshold states and emits transition events while retaining the
   current balance curve.
9. A minimal bridge Vow can be made, kept, broken, or amended in a native test.
10. Commander strategic state is deterministic, hashed, and updated only from a
    `PlayerObservation`.
11. Repeatable 100, 250, 500, and 1,000 entity benchmark output reports step, spatial,
    visibility, AI, hash, and approximate memory measurements where available.
12. Native correctness tests and deterministic duplicate runs pass. Unreal validation
    is reported only when an Unreal build is actually run.

## Known risks

- Reordering the current `Simulation::step()` would change combat, resolve, capture,
  fog, and AI perception simultaneously. Phase 1 therefore extracts incrementally and
  records legacy ordering until each transition has equivalence tests.
- SnapshotV1 versions the state/hash compatibility boundary and rejects incompatible
  content or pipeline definitions. Unreal quick saves use that boundary directly;
  future schema changes still require explicit migrations for long-lived saves.
- The actor layer previously used owner index for faction silhouettes and minimap
  colors. Phase 1 routes explicit faction metadata instead; Unreal mirror-match
  runtime capture remains a production gate because the engine SDK was unavailable
  during the portable implementation pass.
- The current entity vector invalidates pointers on growth or compaction. Indexed
  lookup removes repeated scans but does not make retained raw pointers safe across a
  mutating simulation call.
- Event history and AI traces can grow for a long mission. Phase 1 measures the cost;
  bounded persisted event pages are a later save/replay concern.
- `resolve_unit_separation`, pathfinding, visibility reveal, and several AI scans remain
  likely 1,000-unit bottlenecks after the first spatial migration.

## Production gates

### Gate A: Foundation correctness

- Portable build succeeds with warnings treated according to existing CMake policy.
- Focused foundation tests, existing native tests, parity catalog, and deterministic
  duplicate checks pass.
- No Unreal type appears in an `AshenCore` public or private gameplay definition.

### Gate B: Deterministic mission substrate

- Save schema version, snapshot restore, command replay, event replay, and checkpoint
  hash verification exist.
- The bridge, convoy, hospital, supply route, Vow, transformation, Dread escalation,
  and objectives are authoritative data, not level-script-only state.

### Gate C: Playable systems

- Compact recovery/logistics and Ascendancy Absolution produce distinct legal command
  sets and AI strategies.
- Casualty, resolve, cohesion, supply, and retreat loops pass headless mission fixtures.
- Three campaign difficulties obey the same resource, visibility, and command rules.

### Gate D: Production presentation

- Unreal resolves faction, archetype, variant, upgrade, damage, transformation, and
  campaign identity through data-driven presentation keys.
- UMG/CommonUI view models consume simulation state and events; text is localization
  ready and critical actions support rebindable input and accessible feedback.

### Gate E: Vertical-slice release candidate

- A first-time player can complete the mission in 30–45 minutes without a debug action.
- Checkpoint recovery cannot soft-lock the mission or change the deterministic result.
- Minimum-spec CPU/GPU/memory budgets hold in the worst scripted battle.
- No known crash, data-loss defect, progression blocker, replay divergence, or AI
  information leak remains.
