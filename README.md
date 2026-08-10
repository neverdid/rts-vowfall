# Vowfall

Vowfall is an original dark-medieval-horror real-time strategy game about three civilizations with
incompatible answers to suffering. It combines asymmetric factions, economy and production,
positional combat, campaign perspectives, and competitive matches without using copyrighted settings,
characters, names, or assets from other games or manga.

## Project status

The repository now has two active production layers and one archived prototype:

- `unreal/AshenDominion/` is the playable Unreal Engine 5.8 C++ client. It currently provides an RTS
  camera, edge scrolling and zoom, click and drag-box selection, contextual move/attack/gather commands,
  attack-move, patrol, retreat, stances, queued orders, control groups, worker construction, research,
  faction powers, resolve, authoritative fog of war, capture relics, production and rally queues, a
  clickable command card, a deployment menu, tactical minimap, an unlocked campaign briefing, and a
  core-owned, fog-constrained, influence-aware commander that can control either player. It also has two
  distinct multi-part faction silhouettes and a procedural dark-medieval battlefield with castles,
  forests, roads, bridges, a contested island, and shader-driven river water. Compact reinforcement and
  construction obey the deterministic Road Ledger route graph. SnapshotV1 quick
  checkpoints can be saved and restored through Unreal, and verified ReplayV1 files can be exported
  from the running match.
- `unreal/AshenDominion/Source/AshenCore/` is the portable C++20 authoritative simulation. CMake and
  Unreal compile these exact same sources, so gameplay rules do not fork between clients. The canonical
  13-mission campaign catalog and selected story mission are authoritative C++ state as well. Its
  versioned SnapshotV1 API can checkpoint and deterministically restore the portable simulation, while
  ReplayV1 records external inputs and verifies regenerated commands, events, checkpoints, and final state.
- `src/` is the frozen TypeScript/Three.js prototype retained for design provenance. It is not an active
  client, gameplay authority, parity target, or CI requirement. New gameplay, AI, presentation, and
  testing work belongs in Unreal and `AshenCore`.

The Unreal directory and C++ module retain the internal name `AshenDominion` for now. Renaming Unreal
targets, generated files, and module symbols is a separate migration so the playable native foundation
is not destabilized by a cosmetic path change.

The Unreal competitive vertical slice and Story-mode foundation are playable, but neither is being presented as
a finished game. The menu exposes the full campaign spine and launches **The Bridge of Names** with its own
briefing, objective, public vow, and deterministic Story identity. Scripted reversals, dialogue, player-facing
checkpoint selection and replay playback, cinematics, production terrain and characters, matchmaking,
and authoritative online PvP remain later milestones.

## Unreal client

Requirements:

- Unreal Engine 5.8
- Visual Studio with Game development with C++, MSVC, Windows SDK, and Visual Studio Tools for Unreal

Open `unreal/AshenDominion/AshenDominion.uproject`. Let Unreal build the modules if prompted, then press
Play. A first launch can spend extra time compiling shaders. The match remains frozen on the main menu.
Choose **Story** to open the campaign and begin the prologue, or choose **Skirmish** for the competitive
slice. Opening workers automatically begin harvesting, and the first enemy assault waits two minutes so
the command and production flow can be learned before the pressure begins.

Current controls:

- Left mouse: select; drag to box-select units; Shift adds to the selection; double-click selects matching
  visible units
- Right mouse: move in formation, attack an enemy, gather a resource, or set a selected building's rally
  point based on the target
- Arrow keys or screen edges: pan the war camera
- Mouse wheel: smooth zoom
- A then left mouse: attack-move; P then left mouse: patrol; R then left mouse: set a rally point
- S: stop; H: hold position; Shift while issuing an order: append it to the unit's command queue
- B or T with one worker selected, then left mouse: place an Assembly Hall or Signal Bastion
- X: retreat to the command keep; Z, C, or V: aggressive, defensive, or stand-ground stance
- Ctrl+0-9: assign a control group; 0-9: recall it; press the same group twice to center the camera
- Q and E: train the primary or secondary unit from a selected producer
- Y: research the Black-Iron Age; U: research the selected structure's faction doctrine
- F: activate the faction power when its ore cost and cooldown are ready
- F5: save the quick checkpoint; F9: restore it; F6: export a verified `.vowreplay` to `Saved/Replays`
- Command-card buttons mirror the hotkeys and show unavailable tech or cooldown actions as disabled
- Enter or Space: open the campaign from the main menu, then begin the prologue
- Escape: cancel a pending command mode, or pause and return to the deployment screen

Build the editor target directly from PowerShell when `UE_ROOT` points to the Unreal installation:

```powershell
& "$env:UE_ROOT\Engine\Build\BatchFiles\Build.bat" `
  AshenDominionEditor Win64 Development `
  "-Project=$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  -WaitMutex -NoHotReload
```

Run the Unreal automation test:

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  -unattended -nop4 -nullrhi -nosplash `
  '-ExecCmds=Automation RunTests Ashen' `
  '-TestExit=Automation Test Queue Empty' -log
```

Regenerate the source-controlled world master materials after changing the art pipeline script:

```powershell
$script = (Resolve-Path `
  'unreal/AshenDominion/Build/WorldArt/build_world_materials.py').Path.Replace('\', '/')
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  "-ExecutePythonScript=$script" -unattended -nop4 -nosplash -NoSound
```

Audit the locally acquired Step 3B production environment kit. The default audit reports missing slots
without failing, while `-EnvironmentKitStrict` makes an incomplete artist workstation fail:

```powershell
$script = (Resolve-Path `
  'unreal/AshenDominion/Build/EnvironmentKit/audit_environment_kit.py').Path.Replace('\', '/')
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  "-ExecutePythonScript=$script" -unattended -nop4 -nosplash -NoSound
```

Licensed Fab source assets belong in `Content/External` and are intentionally excluded from public Git.
See [the production environment-kit contract](docs/environment-kit.md) before migrating content.

Regenerate the original source-controlled fallback meshes after changing their Geometry Script factories:

```powershell
$script = (Resolve-Path `
  'unreal/AshenDominion/Build/EnvironmentKit/build_source_environment_kit.py').Path.Replace('\', '/')
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  "-ExecutePythonScript=$script" -unattended -nop4 -nullrhi -nosplash -NoSound
```

Set `VOWFALL_ENVIRONMENT_ASSETS` to a semicolon-separated list of manifest-relative mesh paths before
running the command to rebuild only selected assets. Leave it unset to reproduce all 40 meshes.

Capture the whole battlefield for visual regression review:

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  -game -AshenCaptureWorld -ResX=1280 -ResY=720 -Windowed -RenderOffScreen -NoSound
```

The image is written to `unreal/AshenDominion/Saved/Screenshots/Automation/World.png`.

Capture Blackridge at the deterministic gameplay-camera review angle:

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  -game -AshenCaptureBlackridge -ResX=1920 -ResY=1080 -Windowed -RenderOffScreen -NoSound
```

The image is written to `unreal/AshenDominion/Saved/Screenshots/Automation/Blackridge.png`.

Capture Gravewood at its deterministic gameplay-camera review angle:

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  -game -AshenCaptureGravewood -ResX=1920 -ResY=1080 -Windowed -RenderOffScreen -NoSound
```

The image is written to `unreal/AshenDominion/Saved/Screenshots/Automation/Gravewood.png`.

Capture the campaign briefing at a supported UI resolution:

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  -game -AshenCaptureCampaign -ResX=1280 -ResY=720 -Windowed -RenderOffScreen -NoSound
```

The image is written to `unreal/AshenDominion/Saved/Screenshots/Automation/Campaign.png`.

Capture the playable prologue foundation with its Story objective and public vow:

```powershell
& "$env:UE_ROOT\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  "$PWD\unreal\AshenDominion\AshenDominion.uproject" `
  -game -AshenCaptureStoryBattle -ResX=1920 -ResY=1080 -Windowed -RenderOffScreen -NoSound
```

The image is written to `unreal/AshenDominion/Saved/Screenshots/Automation/StoryBattle.png`.

## Portable core

Requirements: a C++20 compiler and CMake 3.24 or newer.

```bash
cmake --preset dev
cmake --build --preset dev --config Debug
ctest --preset dev -C Debug
```

The generated `ashen_headless` executable advances a match without graphics and prints its final tick,
economy, result, and deterministic state hash. `ashen_self_play` runs the faction-and-spawn matrix plus
economy-recovery, blocked-opening, early-rush, flank, danger-avoidance, reinforcement, and retreat fixtures for
every faction. It duplicates each seeded run to detect nondeterminism and writes a stable machine-readable
report. The C++ commander has independent strategic, tactical, and micro utility layers; the report separately
hashes every candidate score, influence sample, selected reason, observation, command, and authoritative result:

```powershell
.\build\native\native\Debug\ashen_self_play.exe `
  --seeds 2 --output build\native\self-play-report.json
```

Hard correctness or behavior failures return a nonzero exit code. The report includes named fixture checks and
diagnostic end state; balance alerts remain visible without failing the run. CI also requires the Windows and
Linux reports to be byte-identical.

Run the threshold-free simulation performance probe after building to emit repeatable CSV rows for 100, 250,
500, and 1,000 entities:

```powershell
.\build\native\native\Debug\ashen_simulation_performance.exe
```

The probe reports simulation-step, navigation, AI, deterministic spatial-query, visibility, and state-hash
timings plus an approximate capacity-based memory footprint. It is a developer comparison tool, not a
machine-specific correctness test.

Record, inspect, or deterministically verify a portable ReplayV1 file with the native replay tool:

```powershell
.\build\native\native\Debug\ashen_replay.exe record work\match.vowreplay 2400 42
.\build\native\native\Debug\ashen_replay.exe inspect work\match.vowreplay
.\build\native\native\Debug\ashen_replay.exe verify work\match.vowreplay
```

The verifier restores the embedded SnapshotV1 checkpoint, resubmits only recorded external inputs, regenerates
fog-limited AI decisions and typed events, and rejects command, event, checkpoint, or final-state divergence.
Unreal stores the same SnapshotV1 bytes inside its quick-save adapter and refuses incompatible or corrupt saves.
Its F6 export records player submissions, regenerates AI and events during verification, and writes the ReplayV1
file only after that verification passes. Named checkpoint browsing and replay playback remain future UI work.

## Archived web prototype

The `src/` and TypeScript tooling remain in source control only as a historical reference. They are no
longer part of the supported build, test, or release path.

See [docs/cpp-migration.md](docs/cpp-migration.md) for ownership boundaries and the remaining migration
stages, [docs/ai-architecture.md](docs/ai-architecture.md) for the ordered non-cheating AI plan,
[docs/research-brief.md](docs/research-brief.md) for the genre and market findings, and
[docs/world-bible.md](docs/world-bible.md) for the original setting, characters, factions, visual
direction, and source-safe inspiration ledger, and [docs/campaign-bible.md](docs/campaign-bible.md) for
the canonical 13-mission campaign and Vow system. The ordered path from the current foundation to beta
is tracked in [docs/beta-roadmap.md](docs/beta-roadmap.md).
