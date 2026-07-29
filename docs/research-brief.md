# RTS Research Brief

## Core Lessons

StarCraft succeeded less because it invented the RTS and more because it deeply polished an already understandable 2D RTS format. The best lessons to borrow are asymmetric faction identity, fast match readability, a real campaign, strong multiplayer balance testing, and a refusal to ship core-feel problems. The parts to avoid are crunch as a production method, story confusion caused by too many betrayals, and copying the surface of StarCraft without a fresh reason to exist.

The modern RTS audience is split between competitive players, campaign/co-op players, large-scale sandbox players, and strategy-adjacent players who like planning but not high APM stress. Current Steam/community signals show the genre is not dead, but it is niche compared with MOBAs and broader strategy games. Age of Empires II and IV still reward clarity and competitive depth; Company of Heroes 3 shows that rough launches can recover if patches improve feel, pathing, and spectacle; Stormgate shows the danger of launching with weak campaign value, shaky identity, and trust-eroding scope promises; Beyond All Reason shows demand for scale and strong control; Against the Storm and They Are Billions show that strategy players respond to pressure systems, replayable PvE, and readable escalating danger.

## Player Feedback Themes

Implement early:

- Fast, predictable unit control and readable command feedback.
- Strong campaign missions, not just tutorials with narration.
- Local skirmish/PvP fundamentals before online ranked.
- Clear faction silhouette and unit counter roles.
- Good performance at modest unit counts before chasing epic scale.
- Data-driven content so balance changes are cheap.
- Optional depth rather than mandatory high APM.
- A distinctive pressure mechanic that creates tension without hiding information or adding unfair randomness.

Avoid early:

- Online ranked before deterministic simulation and reconnect-safe networking exist.
- Monetization, battle passes, paid commanders, or paid campaigns before trust is earned.
- Three fully asymmetric factions before one faction is fun and one matchup is stable.
- Huge army sizes before pathing, selection, and UI remain readable.
- Cinematics or lore volume as a substitute for mission variety.
- Early access messaging that overpromises finished campaign, co-op, and esports all at once.
- Direct copying of protected settings, characters, symbols, factions, or story events from inspirations.

## Market Notes

| Segment | Examples | What It Teaches |
| --- | --- | --- |
| Classic competitive RTS | StarCraft, Age of Empires II/IV | Responsiveness, balance, map control, readable counters. |
| Tactical RTS | Company of Heroes, Dawn of War | Cover, squads, terrain, reduced economy load. |
| Epic-scale RTS | Total Annihilation, Supreme Commander, Beyond All Reason | Scale is compelling only if pathing and UI survive it. |
| Strategy-adjacent PvE | Against the Storm, They Are Billions, Northgard | Campaign/replayable PvE can reach players who avoid ladder stress. |
| Horror pressure systems | They Are Billions, dark-fantasy RPG/strategy communities | Fear works when it changes decisions: expansion risk, sound/noise, morale, timing, and sacrifices. |
| MOBA/action-strategy | Dota 2, Mechabellum, auto battlers | Fewer units, clearer hero moments, and repeatable sessions reduce RTS anxiety. |

## Original Medieval Horror Direction

Working title: **Vowfall**. The title is provisional pending professional trademark and storefront
clearance.

The world should evoke tragic medieval horror through besieged roads, displaced communities, practical
late-medieval armies, black iron, living memory, transformation, and civilizations trying to remain
morally recognizable while winning. It must not copy *Berserk* or any other source. Avoid protected
names, characters, silhouettes, symbols, factions, events, cosmology, costumes, and iconography. The
retained lessons are emotional: endurance under overwhelming pressure, intimacy inside cosmic horror,
physical consequence, and victory that changes the victor.

The playable conflict belongs to three original factions:

1. **Cinder Compact:** human cities defend freedom and survival while relying on conscription and
   memory-destroying extraction.
2. **Gloam Ascendancy:** transformed communities end hunger, debt, and fear by narrowing the self into
   one perfected purpose.
3. **Elder Concord:** elves, dwarves, and giants defend ecological memory while old law hardens into
   authority over younger lives.

The full creative boundary, historical and philosophical basis, cultural detail, character arcs, and
campaign structure live in [world-bible.md](world-bible.md) and
[campaign-bible.md](campaign-bible.md).

## Prototype Direction

The first playable slice should prove the basics: select units, harvest black iron, build production,
train troops, attack, survive AI pressure in story mode, and support a local PvP sandbox with both sides
controllable. The signature mechanic is **Dread Tide and Resolve**: dread rises on a deterministic tide
and near enemy forces, while faction wards, allies, and reclaimed memory stones stabilize troops. Low
resolve gently reduces movement and damage, creating horror pressure while keeping outcomes readable.
Online PvP should come later after deterministic lockstep or client-server authority is chosen.

## 2026 Control and Replayability Pass

The most consistent signal in recent player discussion is not a request for more spectacle. It is a request for better intent handling: responsive movement, reliable pathing, visible control groups, flexible hotkeys, useful camera control, less unit clumping, and fights that do not evaporate before a player can read them. Competitive players still want mechanical expression in macro, but many distinguish meaningful execution from repetitive busywork. Campaign-focused players repeatedly ask for mission variety, difficulty replay value, skirmish AI, more maps, scenarios, and eventually replay/spectator support.

This pass therefore implements:

- Attack-move that acquires targets and resumes its original advance.
- Persistent control groups with double-tap camera focus.
- Production rally points and wider formations.
- A visible, race-dependent army cap that makes production halls part of macro planning.
- One faction doctrine per race instead of activated abilities on every unit.
- A dedicated AI skirmish mode alongside story and local PvP.
- A real Three.js battlefield with depth, shadows, silhouettes, fog of war, and an interactive minimap.
- A full-screen world-first menu, while keeping match controls compact and readable.

Deliberately deferred:

- Large per-unit ability panels; recent Tempest Rising discussion frequently describes excessive activated abilities as fiddly under pressure.
- Online ladder and monetization before deterministic networking, replays, observer tools, and enough map variety exist.
- Very dense armies or stronger area damage before spacing, pathing, and time-to-react are proven at scale.

## Sources

- StarCraft history and design: https://www.filfre.net/2024/07/starcraft-a-history-in-two-acts/
- StarCraft II free-to-play model: https://news.blizzard.com/en-us/article/21173629/starcraft-ii-going-free-to-play-explained
- Current RTS Steam activity: https://steamdb.info/charts/?tagid=1676
- Age of Empires II current SteamDB data: https://steamdb.info/app/813780/charts/
- Age of Empires IV current SteamDB data: https://steamdb.info/app/1466860/charts/
- Company of Heroes 3 current SteamDB data: https://steamdb.info/app/1677280/charts/
- Stormgate current SteamDB data: https://steamdb.info/app/2012510/charts/
- Stormgate official mode positioning: https://playstormgate.com/
- Beyond All Reason positioning: https://www.beyondallreason.info/
- Player feedback sample on Stormgate launch issues: https://www.reddit.com/r/Stormgate/comments/1fbihtz/why_are_reviews_still_getting_worse/
- They Are Billions pressure-loop feedback: https://www.reddit.com/r/patientgamers/comments/w1jmrs/they_are_billions_is_a_huge_breath_of_fresh_air/
- Against the Storm UI/loop feedback: https://www.reddit.com/r/patientgamers/comments/1rwgxy0/against_the_storm_a_really_good_game_with_really/
- Horror tension design: https://www.gamedeveloper.com/design/the-balancing-act-of-tension-in-horror-game-design
- Stormgate priorities and customizable-hotkey feedback: https://playstormgate.com/news/looking-ahead-stormgate-s-path-forward
- Stormgate on responsiveness, terrain, camera, and weekly playtesting: https://playstormgate.com/news/stormgate-developer-update-april-12-2024
- StarCraft control-group learning feedback: https://us.forums.blizzard.com/en/sc2/t/how-do-i-micro-everything/7827
- StarCraft 3 discussion on macro, army spacing, and grounded story: https://www.reddit.com/r/starcraft/comments/1o2b14b/if_we_ever_somehow_get_starcraft_3_what_would_you/
- StarCraft 3 discussion on clumping, splash damage, terrain, and pathfinding: https://www.reddit.com/r/starcraft/comments/13i38sr/what_would_you_like_to_see_from_starcraft_3/
- Tempest Rising feedback on ability overload and long-term content: https://www.reddit.com/r/RealTimeStrategy/comments/1m9q0u8/any_thoughts_on_tempest_rising/
- Tempest Rising campaign and skirmish replayability feedback: https://www.reddit.com/r/RealTimeStrategy/comments/1k8py2r/ist_tempest_rising_worth_only_for_the_campaign/
