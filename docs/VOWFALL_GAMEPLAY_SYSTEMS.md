# Vowfall Gameplay Systems

## Faction identity

`PlayerId` answers who issues a command and who owns state. `FactionId` answers which
rules, content, presentation, doctrine, and narrative institutions apply. Control
source answers whether the commands came from input, AI, replay, or mission scripting.
These are independent axes.

An entity's future presentation key is resolved from:

`FactionId + EntityType + variant + upgrades + damage + transformation + campaign identity`

Phase 1 stores faction on every entity and validates faction presentation metadata.
Owner index remains valid for local/enemy relationship checks but cannot choose a
faction silhouette, name, palette, power, doctrine, or behavior.

## Cinder Compact

### Fantasy

Ordinary people survive because institutions keep counting them and because supply,
care, and retreat remain physical obligations.

### Road Ledger

The first Road Ledger slice gives Compact command keeps, assembly halls, signal
bastions, and Field Hospitals stable supply profiles. Keeps provide bounded capacity,
connected assembly halls consume capacity and relay the route, and bastions and
hospitals are terminal consumers.
Connectivity is recalculated through the deterministic spatial grid. Candidate paths
prefer shortest hop count, then lower consumer entity ID, then lower source entity ID.
Only a relay that received capacity may propagate the route.

Explicit road segments, carts, field kitchens, additional medical stations, evacuation exits,
and bridge-health edges remain later authored nodes and edges rather than aliases for
the current structures.

The implemented consumers are reinforcement production and construction. A
disconnected Compact producer cannot accept a Train command, stops advertising that
command to AI/UI observations, and pauses an existing queue until the route returns.
A new Compact construction site must fit the current route and source capacity before
ore is charged. Once accepted, the unfinished site reserves its normal demand but
cannot relay supply; a cut pauses progress. An orphaned site can still receive a new
builder without another charge and resumes only when the route returns. Compact units
within the physical range of a connected completed keep or assembly hall
receive the assisted `Retreat` capability. The closest qualifying transmitter is
resolved with lower entity ID breaking equal-distance ties, while a target-less order
keeps the established behavior of withdrawing to the nearest command post. A cut-off
unit can still receive an ordinary `Move`, but does not receive the Retreat resolve
recovery or defensive-stance transition. AI discovers this through the same per-unit
command capabilities as the player.

Compact casualty recovery now uses a buildable Field Hospital. During the base recovery
window, a casualty must be within a completed, connected, owned hospital's 300,000-unit
intake radius and that facility must have queue capacity. The closest hospital wins,
with lower entity ID breaking equal-distance ties, unless the command explicitly names
a facility. The command addresses the retained `UnitIdentityId`, never the removed
runtime entity handle, and player and AI discover the same owned-only, facility-specific
capability without enemy casualty leakage.

Admission reserves the unit's normal population cost and protects the casualty from its
original deadline. A hospital treats two casualties concurrently for 120 ticks and may
hold four more in a stable waiting queue. Supply disconnection pauses treatment but does
not reorder or discard the queue. Destruction emits `CasualtyCareInterrupted`, releases
the reservations, and resumes the original casualty deadline. Other factions retain
their immediate base recovery rule for now.

Later connected formations may also receive:

- reinforcement and ammunition;
- recovery and evacuation;
- resolve support;
- construction capacity;
- richer controlled retreat routes through explicit road and evacuation edges.

Disconnection is a battlefield fact exposed through `SupplyConnected` and
`SupplyDisconnected` events. It is not a hidden global modifier.

### Recoverable casualties

The planned casualty state machine is:

`Active -> Wounded -> Incapacitated -> Recoverable -> Recovered`

with terminal or unresolved paths to `Missing` and `Dead`. A soldier has stable
campaign identity, formation identity, experience, injuries, relevant memories, and
last known location. Recovery moves identity; it does not spawn an unrelated
replacement.

The X4 foundation assigns every unit a stable `UnitIdentityId` and retains its
identity-ordered `CasualtyRecord` after the combat entity is removed. Lethal damage
transitions a unit to `Incapacitated`. After a fixed 40-tick (two-second)
stabilization delay it becomes `Recoverable` for a base 400-tick (twenty-second)
window; expiry transitions it to `Dead`. Equal deadlines are processed by identity.
Eligibility is true only while the state is `Recoverable` and the authoritative tick
is strictly before the stored deadline. Compact eligibility additionally requires the
Road Ledger care access described above. The timer remains owned by `CasualtySystem`;
the network query is a derived `Simulation` rule and cannot mutate or freeze it.

The ordered event stream exposes nonterminal casualty state changes, while
`UnitKilled` is emitted only on terminal expiry. SnapshotV3/ReplayV3 preserve and
verify records, deadlines, transitions, and their event projection. Owned
observations carry live identity/state; sanitized enemy observations do not.

`Recovered` is now authoritative. Recovery creates a new monotonic runtime `EntityId`
with the same persistent identity, owner, faction, archetype, injuries, formation,
experience, and retained record. The body returns at 50% current researched maximum
health. Compact units emerge beside the treating Field Hospital; other factions
return at the retained casualty position, with deterministic terrain correction in
both cases. The event order is `EntitySpawned`, `CasualtyStateChanged`, then
`UnitRecovered`. A recovered unit can be wounded or incapacitated again without
forking its identity. There is no ore charge in this foundation rule.

`Missing` remains reserved vocabulary. Authored evacuation routes, formation and
experience mutation, richer failure outcomes, and the `No One Left Uncounted`
eligibility modifier remain later work.

### Production roles

The intended Compact role set is:

- Ledger Guard: line holder and brace anchor.
- Roadwarden: scouting, route security, pursuit denial, and convoy escort.
- Crossbow Company: ranged pressure with formation and supply dependency.
- Field Engineer: bridge, road, relay, fortification, and demolition work.
- Surgeon-Carter: recovery and evacuation support.
- Bell Crew: signal, resolve, and formation support.
- Ash Cannon: supplied siege and area denial.
- Marshal: named formation command and operational Vow interaction.

The existing Worker/Vanguard/Skirmisher/Command/Barracks/Turret catalog remains a
temporary compatibility roster.

### No One Left Uncounted

This replaces the existing generic global heal behavior. The final ability creates a
time-bounded rule under which eligible casualties remain recoverable longer and
evacuation/recovery strengthens. It must not erase injury history or heal arbitrary
health bars globally.

## Gloam Ascendancy

### Fantasy

People escape uncertainty by accepting irreversible purpose. Strength comes from
removing alternatives, not from receiving a conventional upgrade tier.

### Absolution

An Initiate can transform into:

- Protector;
- Witness;
- Hunter;
- Bearer;
- Judge;
- Shelter;
- Cantor;
- Warden.

A transformation definition names source and result archetypes, required ability and
commands, duration, interrupt rules, permanent costs, presentation key, resolve
changes, AI purpose metadata, and reversibility. Completion changes the legal command
set and persistent identity.

Phase 1 registers and validates a representative transformation definition and the
event vocabulary. It does not yet mutate a production unit.

### Completion

The final faction ability immediately completes a prepared or wounded person's
transformation at a permanent strategic and personal cost. It is never a generic
spawned-unit power.

## Elder Concord

The Concord is outside the first vertical slice. Its future architecture reserves:

- a limited number of treaty slots;
- treaty-dependent rosters and command capabilities;
- living infrastructure and terrain relationships;
- slow replacement;
- political and ecological obligations represented as authoritative state.

The current third-faction skirmish data remains available for regression and AI
symmetry tests. Phase 1 must not hard-code two-faction assumptions that block the
Concord later.

## Combat roles and readable stories

Role definitions carry capability metadata rather than relying on damage multipliers.
Future combat interfaces reserve explicit data for:

- facing and directional defense;
- formations and cohesion;
- flanking;
- charge and brace states;
- deterministic projectiles;
- cover and elevation;
- suppression;
- melee engagement and pursuit;
- controlled retreat;
- ability windup, telegraph, interruption, and recovery;
- casualty outcomes.

The Phase 1 event stream makes current instant-hit combat observable while leaving room
for later projectile and ability phases. Instant attacks are not mislabeled as
projectiles.

## Formation and cohesion

A production formation is a stable simulation object with:

- formation ID, persistent name, faction, owner, and leader;
- member IDs and ordered slots;
- shape and facing;
- current objective and retreat location;
- cohesion value and threshold state;
- supply/support dependencies;
- casualty and memory history.

The current `formation_targets` helper only assigns temporary destinations and
`resolve_unit_separation` only resolves overlap. Neither is a persistent formation
system. Phase 1 defines event and content foundations without claiming otherwise.

## Resolve

The target threshold states are:

- Steady;
- Strained;
- Wavering;
- Broken;
- Rallied.

Phase 1 classifies the current scalar Resolve into thresholds and emits ordered
transitions. `Rallied` is reserved for an explicit recovery transition; it is not
inferred by relabeling every high scalar value.

The future persistent model records evidence such as:

- leader death;
- abandonment or supply loss;
- successful evacuation;
- recovered casualties;
- voluntary or forced transformation;
- holding a charge;
- broken or amended Vows;
- returning to a known battlefield.

Evidence has a stable ID, source event, weight, decay/retention rule, and affected
formation. Resolve is not a global morality score.

## Supply

Supply is a deterministic graph, not a global aura. Node rules have stable content
IDs, capacity, demand, faction access, and physical positions. The current proximity
links are derived from transmitting nodes; explicit stable road and bridge edges are
still planned. The update phase:

1. applies construction and destruction before the end-of-tick rebuild;
2. orders eligible nodes and spatial-query results by stable entity ID;
3. expands only sources and already allocated relays;
4. allocates per-source capacity by hop count, consumer ID, source ID, and
   predecessor ID;
5. emits only connection threshold changes in stable entity-ID order.

Supply now affects Compact reinforcement and construction legality/progress plus
assisted-retreat legality. Retreat support is checked when the authoritative command is
applied; an accepted withdrawal remains a normal movement order if the graph changes
afterward. Heavy weapon operation and recovery remain later consumers. Supply does not
silently rewrite unit identity.

## Vows

A Vow definition contains stable content metadata, operational terms, affected-party
rules, evidence conditions, and consequence keys. Runtime state contains:

- Vow ID;
- maker;
- made tick;
- unresolved/kept/broken/amended resolution;
- resolution tick and revision;
- participating affected party where required.

Authoritative commands are `MakeVow`, `KeepVow`, `BreakVow`, and `AmendVow`. Phase 1's
minimal bridge Vow permits amendment only when a player other than the maker submits
the amendment, proving that the commander's private reinterpretation is insufficient.
The mission layer will later model specific civilian and institutional participants.

Vow state and commands are hashed. Replays regenerate Vow events from commands. Saves
store lifecycle state and the content definition version.

## Testimony and memory

Testimony changes what is known and which claims are supported. A testimony definition
has a stable source identity, subject, discovery conditions, localization keys, and
affected objective/Vow references. Discovery emits `TestimonyDiscovered`.

Testimony never writes a hidden moral score and never automatically selects the
correct decision. Conflicting testimony can coexist.

The Ledger of the Uncounted is a concentration of memories belonging to people who
were excluded, misnamed, transformed, erased, or denied legal recognition. The Quiet
completes existing commands literally; it does not invent desires, lie, understand
amendment, or become a conventional monster.

## Transformations

Transformation runtime state reserves:

- transformation ID and definition version;
- person/entity ID;
- start and completion ticks;
- source and target capabilities;
- interruption state;
- permanent costs;
- consent/testimony evidence;
- resulting presentation and resolve behavior.

Start and completion are authoritative events. An interrupted transformation retains
the history that it was attempted.

## Civilian institutions

Convoys, hospitals, kitchens, ferries, councils, and affected communities are
authoritative mission actors or objectives, not decorative triggers. They obey
visibility, movement, damage, supply, capacity, and command rules appropriate to their
role. The hospital and convoy expose capabilities through the same content validation
used by combat entities.

## AI strategic state

`CommanderAI` retains a deterministic strategic record containing:

- current intention;
- opening plan;
- desired economy and composition;
- timing window;
- preferred route;
- known opponent behavior;
- confidence;
- abort conditions;
- contingency;
- evidence that changed the plan.

Phase 1 updates this state only from `PlayerObservation` and includes it in the
commander state hash. It does not yet alter the mature utility layers. Future stable
squads add role, leader, formation, objective, cohesion, retreat location, and support
dependency.

AI never receives live hidden entities, hidden resources, opponent production queues,
or private campaign state. Mission scripting must use the same observation or explicit
public scenario facts.
