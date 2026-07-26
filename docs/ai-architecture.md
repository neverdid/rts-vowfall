# Vowfall AI Architecture

## Direction

Vowfall's production game is the Unreal client backed by the deterministic C++20 `AshenCore`.
The archived TypeScript prototype is not an AI runtime, parity target, or implementation destination.

AI is three cooperating systems, never one update function:

1. **Commander AI** chooses economy, technology, production, scouting, objectives, and attack timing.
2. **Combat AI** executes tactical positioning and unit micro after the commander commits forces.
3. **Ambient AI** drives wildlife, horrors, civilians, and scenario actors for mood and world response.

The first two systems may share observations and influence data, but they have separate schedules,
budgets, tests, and tuning surfaces. Ambient behavior cannot issue player commands or inspect hidden
player state.

## Non-negotiable rules

- AI runs in `AshenCore`, not in an Unreal-only player controller or subsystem.
- AI receives the same authoritative observation a human player can receive.
- Hidden entities cannot be targeted, pursued, counted, counter-built against, or used in utility scores.
- Known map geometry and previously explored terrain are valid knowledge; live hidden state is not.
- Difficulty comes from bounded reaction time, planning depth, risk tolerance, and execution quality.
- No difficulty grants free resources, impossible actions, hidden vision, or zero-latency reactions.
- Decisions are deterministic for a match seed and command stream.
- Designer-authored utility scoring, behavior trees/state machines, and influence maps come before ML.

## Ordered delivery

### Step 0: Authoritative fog of war

Status: implemented in `AshenCore`.

- Per-player cells have `Hidden`, `Explored`, or `Visible` state.
- Current visibility is rebuilt from living, completed friendly observers after each fixed simulation tick.
- Explored memory persists when current vision leaves an area.
- Direct attacks reject unseen targets with a generic invalid-target response.
- Existing attack orders stop when all contact is lost.
- Attack-move, patrol, hold position, idle aggression, and defenses acquire visible targets only.
- Deterministic hashes include visibility configuration and cell state.
- Unreal hides unseen enemies, discovers resources through fog, remembers last-observed relic ownership,
  and renders hidden versus explored terrain on the tactical map.

Exit gate:

- An enemy outside vision cannot be targeted or reacted to, even when its entity ID is known.

### Step 1: Core-owned AI controller

Status: implemented in `AshenCore`.

- `PlayerObservation` is a value snapshot containing owned state, sanitized visible enemies, explored map
  cells, remembered sightings, discovered resources, last-observed objective state, and legal capabilities.
- Visible enemy snapshots omit production queues, orders, routes, rally points, cooldowns, and other
  information a player cannot observe.
- `CommanderAI` includes no `Simulation` dependency. Its only input is a const observation and its only
  output is an ordinary `Command` value.
- `Simulation` owns both-player controller scheduling. It takes both observations before queuing commands
  for the next fixed tick, preventing immediate mutation and host-specific first reactions.
- The Unreal subsystem only enables the Player Two commander; it contains no opponent decision logic.
- The native headless runner enables both commanders and finishes a complete deterministic match without
  Unreal or rendering.

Exit gate:

- Passed: two bots finish a headless match, duplicate matches produce identical hashes, and perturbing a
  hidden enemy order changes raw simulation state without changing the observer hash or commander decision.

### Step 2: Self-play and measurement

Status: implemented in the native C++ benchmark layer.

- Six ordered full-match scenarios cover every non-mirror faction pairing with every faction on both spawns.
- Twenty-four targeted fixtures per seed exercise economy-deficit recovery, blocked-opening recovery,
  early-rush survival, flank choice, static-danger avoidance, reinforcement staging, and retreat destinations
  for every faction, plus faction-specific acceptable-loss decisions. Each fixture exposes named checks and
  final diagnostic state in the report.
- Match seeds are part of simulation state and the sanitized observation. They select deterministic commander
  variants without exposing hidden state or granting resources.
- Every applied command records source, issue and application ticks, the exact observation hash, complete
  payload, acceptance, and validation error. External and commander commands share the same trace contract.
- Headless reports record faction and spawn win rates, opening and technology timings, fifth-worker and
  first-reinforcement timings, legal idle producer-ticks, first contact and ore at contact, peak and final army
  value, command rate and rejection reasons, retreat survival, duration, checkpoints, and final hashes.
- The fixed CI suite runs each match twice. Nondeterminism, timeout, missing contact, broken openings, missing
  observation provenance, empty traces, and excessive rejected commands are correctness failures.
- Faction or spawn win-rate skew creates a non-blocking balance alert. Tuning evidence remains visible without
  weakening deterministic and behavioral gates.
- Windows and Linux upload the stable JSON report, and CI requires the files to be byte-identical.

Exit gate:

- Passed: the same seed and build produce the same telemetry, command trace, checkpoints, outcome, and state
  hash; the default two-seed suite completes 12 full matches and 48 targeted fixtures with all 228 behavior
  checks passing inside a bounded 16,000-tick match horizon. MSVC and GCC produce byte-identical schema-v4
  reports.

### Step 3: Strategic, tactical, and micro layers

Status: implemented in `AshenCore`.

- The strategic layer scores worker allocation, interrupted construction, opening and supply structures,
  production capacity, technology, faction doctrine, and visible-enemy-aware army composition. Worker
  allocation may accompany one independently selected macro spend without issuing an unaffordable batch.
- The tactical layer scores scouting, remembered-command search, objective capture, reinforcement, visible
  engagements, command assaults, faction-power timing, and retreat from unfavorable health, resolve, or
  observed force ratios.
- The micro layer scores critical-unit retreat, ranged kiting while weapons cool, target focus, melee
  screening for ranged units, and recovery of idle formation stragglers. Retreat hysteresis shelters wounded
  survivors once, while a separate combat-ready force view keeps them out of offensive command loops.
- All scores are deterministic integers derived only from `PlayerObservation`. Candidate ordering and ties
  have stable keys, and match-seed variation can change authored preferences without reading hidden state.
- Strategic decisions run every 80 ticks (4 seconds), tactical decisions every 120 ticks (6 seconds) with a
  30-tick phase, and micro decisions every 12 ticks (0.6 seconds). The opening receives one explicit
  strategic evaluation at tick 1.
- `Simulation` assigns every selected choice a stable decision ID, queues its ordinary player command, and
  later resolves the record as accepted or rejected through the same authoritative validation path.
- Every record retains the observation tick and hash, complete candidate list and utility components,
  selected candidate, winning reason, command payload and sequence, application tick, status, and error.
- Native self-play hashes the complete decision ledger independently from the command trace. CI audits score
  sums, winner selection, cadence, provenance, unique IDs, result linkage, replay equality, and the presence
  of all three layers in full matches.
- Focused native fixtures cover opening allocation, tactical retreat, focus fire, and cooldown-aware kiting.
  Unreal's integration fixture verifies the same ledger and command-result links through the production module.

Exit gate:

- Passed: every applied choice reports its candidate scores, winning reason, observation tick, and
  authoritative command result; a queued end-of-snapshot choice remains explicitly marked `Queued` until the
  next fixed tick applies it.

### Step 4: Influence maps

Status: implemented in `AshenCore`.

- `AIInfluenceMap` builds fixed-point cells for friendly power, observed enemy power, static danger,
  objective value, travel cost, enemy terror pressure, friendly terror, friendly ward support, observed
  resolve vulnerability, and uncertainty. Its dimensions, cells, reachability, and hash are deterministic.
- `PlayerObservation` exposes immutable navigation cell size and obstacle geometry because authored static
  terrain is public map knowledge. The AI still receives no `Simulation` reference or live hidden entity.
- Visible enemies project power, terror, and turret danger. Mobile sightings retain only their last observed
  snapshot, spread and decay for 2,400 ticks, and then expire; visibly clearing the last-known location removes
  the stale contact immediately. Structures remain remembered until their known location is visibly empty.
- Hidden, explored, and visible terrain carry distinct uncertainty. Public objective ownership changes value,
  friendly wards reduce local terror, and a deterministic grid search records travel cost from the command
  structure without replacing unit navigation.
- Engagement, objective, search, scouting, reinforcement, and retreat destinations score influence samples.
  Existing authoritative A* still creates every route after the AI selects a legal waypoint.
- Arrival commits to the real objective instead of orbiting a neighboring cell. After four minutes, an army
  with no combat-ready survivors enters a deterministic attrition commitment: only that exhausted force stops
  retreat loops and begins a final sweep through ordinary combat and command validation. Healthy armies retain
  normal threat assessment, so match age alone never forces a reckless visible engagement. After six minutes
  without visible combat opposition, survivors consolidate and advance directly toward the last-known or
  inferred enemy command. Seeing an enemy army immediately restores ordinary retreat and unfavorable-engagement
  checks; structures alone cannot restart a permanent wounded-unit retreat loop.
- Every tactical candidate records the influence-map hash and exact sampled cell. The self-play decision hash
  and audit include every channel, and Unreal consumes the same exported C++ type.

Exit gate:

- Passed: faction-balanced fixed scenarios verify flank choice, danger avoidance, reinforcement, and applied
  retreat destinations; adversarial tests prove different never-seen live positions cannot change an
  observation or influence-map hash; Unreal's `Ashen.Core.InfluenceTactics` automation test passes.

### Step 5: Faction, resolve, and dread behavior

Status: implemented in `AshenCore`.

- `AIDoctrineProfile` is the single deterministic contract for economy, fortification, technology,
  composition, objectives, scouting, aggression, preservation, cohesion, dread exploitation, terror
  resistance, ward affinity, faction-power use, acceptable losses, retreat thresholds, formation recovery,
  and scouting commitment.
- The Cinder Compact uses an industrial shield-line doctrine: it sustains a larger worker target, fortifies
  earlier, preserves wounded or wavering soldiers, reforms tighter lines, and values its army-wide heal and
  resolve restoration.
- The Gloam Ascendancy uses a predatory hunting-pack doctrine: it accepts materially worse force ratios,
  keeps fighting at lower health and resolve, scouts with a smaller reserve, spreads farther before
  reforming, manifests reinforcements under pressure, and focuses observed enemies whose resolve is already
  breaking.
- The Elder Concord uses a ward-web doctrine: it values warded support, objectives, reconnaissance,
  fortification, and cohesion most strongly, retreats before a poor exchange, and uses its faction power to
  stabilize resolve and damaged structures.
- Steady, Audacious, and Watchful temperaments are selected from the public match seed. Their adjustments are
  deliberately narrow; tests across 128 seeds prove that no temperament can invert the faction's primary
  identity.
- Resolve and dread now affect combat-ready classification, engagement ratios, retreat and rally utility,
  faction-power timing, influence destinations, and focus-fire targets. Friendly terror, friendly wards,
  enemy terror, and last-observed resolve are derived only from `PlayerObservation`.
- Every planned and applied decision retains faction, temperament, and a hash of the complete doctrine
  profile. Native telemetry hashes those fields alongside all utility and influence evidence.
- Total-attrition recovery keeps one affordable worker available for map control, banks further income for
  combat production, and stages an understrength force on objectives until it can assault a known
  fortification at its faction's minimum commitment size.
- The default benchmark adds same-state acceptable-loss fixtures for all factions. Focused tests build a
  three-bit signature from scouting commitment, disadvantaged engagement, and formation recovery without
  reading faction labels; Ascendancy also has an explicit wavering-target exploitation fixture.

Exit gate:

- Passed: blind behavior signatures are unique for all three factions, the two-seed benchmark completes 12
  matches and 48 deterministic fixture runs with all 228 checks passing, and Unreal executes the same
  doctrine fingerprint and acceptable-loss choices through `Ashen.Core.FactionDoctrines`. An extended
  eight-seed audit completes 48 matches and 192 fixtures with all 912 checks passing, no hard failures, and
  no balance alerts.

### Step 6: Honest difficulty

Status: implemented in `AshenCore`.
- Difficulty profiles define observation delay, decision cadence, command precision, planning horizon,
  mistake rate, remembered-information decay, and utility search breadth.
- Story, Standard, Veteran, and Competitive are immutable, hashed profiles consumed by the shared C++
  commander. Unreal exposes the opponent setting but owns no difficulty logic; changes apply when the next
  match starts.
- Reaction latency is applied only to hostile sightings and last-observed objective state. Current owned
  units, economy, resources, explored terrain, public map geometry, and legal non-hostile capabilities remain
  live. No profile changes ore, income, supply, unit statistics, vision, command validation, or simulation
  speed.
- New hostile facts enter a deterministic observation buffer only after the full profile delay. Static
  structures remain remembered, while stale mobile contacts and observed objective state decay at the
  profile's documented memory horizon.
- Strategic, tactical, and micro schedules are profile-aware. Planning breadth limits the deterministic
  candidate window, planning horizon limits each tactical advance, and bounded deterministic mistakes may
  choose only a positive runner-up above the profile's quality floor.
- Point orders receive a deterministic radial precision offset and extra command latency. Build placement,
  entity-target attacks, gathering, production, research, and powers are never perturbed.
- Competitive evaluates every utility candidate, never applies an authored mistake or precision offset, and
  adds no command delay beyond the simulation's ordinary next-fixed-step queue. Its four-tick hostile
  perception delay is 200 ms at the 20 Hz authoritative rate.

| Profile | Hostile reaction | Strategic / tactical / micro | Extra command delay | Point precision | Horizon | Mistake rate / floor | Mobile memory | Search breadth |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Story | 1.0 s | 7.0 / 9.0 / 1.5 s | 0.20 s | 18 world units | 3 cells | 18% / 55% | 30 s | 2 |
| Standard | 0.6 s | 5.0 / 7.5 / 1.0 s | 0.10 s | 10 world units | 4 cells | 9% / 70% | 60 s | 4 |
| Veteran | 0.3 s | 4.0 / 6.0 / 0.7 s | 0.05 s | 4 world units | 5 cells | 3% / 85% | 90 s | 6 |
| Competitive | 0.2 s | 4.0 / 6.0 / 0.6 s | none | exact | 6 cells | none | 120 s | all |

- Every decision records the difficulty and profile hash, delayed knowledge tick, available and evaluated
  candidates, selected quality, mistake flag, precision offset, and command latency. Self-play report schema
  v4 aggregates quality, search breadth, mistakes, and observation delay per player.
- Native tests compare otherwise identical Story and Competitive simulations for 240 ticks and require
  byte-identical player observations. Dedicated fixtures verify reaction windows, attack-capability delay,
  current own-state preservation, memory decay, radial precision, deterministic replay, and a measurable
  128-seed decision-quality difference. Unreal repeats the contract in `Ashen.Core.HonestDifficulty`.

Exit gate:

- Passed: difficulty audits prove equal resources, units, and vision while showing deterministic,
  measurable decision-quality differences without hidden-state or economy bonuses.

### Step 7: Optional narrow ML

ML is permitted only after deterministic self-play data exists. Suitable uses include offline tuning,
build-order evaluation, opponent-style classification, or training-data analysis. Runtime command legality,
visibility, combat execution, and the deterministic simulation never depend on an opaque model.

## Required test families

- **Visibility contracts:** hidden target rejection, reveal, loss of contact, explored memory, observer symmetry.
- **Command legality:** AI and human commands pass through the same validation path.
- **Determinism:** duplicate simulations, queued commands, self-play traces, and final hashes match.
- **Tactical fixtures:** engage, disengage, flank, defend, reinforce, focus fire, kite, and blocked-path recovery.
- **Strategic fixtures:** worker saturation, supply prevention, tech timing, counter-composition, and expansion risk.
- **Information audits:** perturbing hidden enemy state cannot change an AI decision until that state is observed.
- **Faction fixtures:** identical observations produce intentionally different, documented faction choices.
- **Difficulty fixtures:** identical raw simulations preserve resources and vision while delayed hostile
  knowledge and bounded decision quality follow the selected profile.

## Debugging contract

Every AI decision record must contain the simulation tick, observer player, observation hash, delayed
knowledge tick, difficulty fingerprint, layer, candidate actions, evaluated breadth, utility components,
selected quality, mistake and precision metadata, selected action, command latency and sequence, application
status, and rejected command reason. Tactical candidates also retain the influence-map hash and relevant
sampled cell. Shipping builds may discard these records, but tests and development builds must be able to
reproduce them.
