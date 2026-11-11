# Mipsync CLI workflows

These are composable examples, not a substitute for `mipsync describe`.
Place global selectors before the command namespace.

## Inspect a live project

```powershell
mipsync instances --json
mipsync --project "D:\Games\MyGame" scene inspect --json
mipsync --project "D:\Games\MyGame" entity list --json
mipsync --project "D:\Games\MyGame" entity inspect 42 --json
```

Use the returned stable ID for later operations. Inspect `runtime.inspect` when
an authoring command reports that Play Mode is active.

## Open another scene

```powershell
mipsync --project "D:\Games\MyGame" describe scene.open --json
mipsync --project "D:\Games\MyGame" scene open scenes/Title.nscene --json
```

Opening requires Edit Mode and a `.nscene` path inside the selected project.
Project-relative and absolute paths are accepted. Failed loads retain the current
scene. Unsaved changes block switching; save first or explicitly request saving:

```powershell
mipsync --project "D:\Games\MyGame" scene open scenes/Game.nscene --save-current true --json
```

Inspect the newly opened scene before authoring and use `scene.save` to persist
the completed edit.

## Create and verify an object

```powershell
mipsync --project "D:\Games\MyGame" describe entity.create --json
mipsync --project "D:\Games\MyGame" entity create Crate --primitive cube --x 2 --y 1 --z -3 --json
mipsync --project "D:\Games\MyGame" entity inspect Crate --json
mipsync --project "D:\Games\MyGame" scene save --json
```

If `Crate` is not unique, take the new entity ID from the create result and use
that ID for inspection and subsequent mutations.

## Build a small hierarchy with a material

```powershell
mipsync --project "D:\Games\MyGame" entity create Environment --json
mipsync --project "D:\Games\MyGame" material create assets/materials/Brick.nmat 0.7 0.2 0.1 --json
mipsync --project "D:\Games\MyGame" entity create Wall --primitive cube --parent Environment --json
mipsync --project "D:\Games\MyGame" entity transform Wall --x 4 --y 1.5 --sx 4 --sy 3 --sz 0.25 --json
mipsync --project "D:\Games\MyGame" material apply Wall assets/materials/Brick.nmat --json
mipsync --project "D:\Games\MyGame" component add Wall Collider --json
mipsync --project "D:\Games\MyGame" entity inspect Wall --json
mipsync --project "D:\Games\MyGame" scene save --json
```

Describe component and mesh commands first when the accepted type or preset is
uncertain.

## Edit and validate Mips#

1. Discover bindings related to the intended behavior:

   ```powershell
   mipsync search "move a kinematic platform" --json
   mipsync describe Physics.UseKinematicController --json
   ```

2. Edit the `.mips` file with the workspace's normal file-editing mechanism.
3. Compile it before testing:

   ```powershell
   mipsync --project "D:\Games\MyGame" language compile assets/scripts/MovingPlatform.mips --json
   ```

4. Fix every compiler diagnostic and check PS1 compatibility before entering
   Play Mode.

## Playtest without losing edit state

```powershell
mipsync --project "D:\Games\MyGame" scene save --json
mipsync --project "D:\Games\MyGame" runtime play --json
mipsync --project "D:\Games\MyGame" runtime inspect --json
```

Stop only after the requested observation or test:

```powershell
mipsync --project "D:\Games\MyGame" runtime stop --json
```

Play Mode changes are not authoring changes. Make persistent corrections after
stopping, verify them, and save the edit scene again.

## Recover from a bad mutation

```powershell
mipsync --project "D:\Games\MyGame" editor undo --json
mipsync --project "D:\Games\MyGame" entity inspect 42 --json
```

Do not save until the rollback is verified. Use `editor.redo` only when the
user still wants the reverted mutation.

## Use raw scene patching as a last resort

First obtain the canonical document and identify the exact JSON Pointer:

```powershell
mipsync --project "D:\Games\MyGame" scene get-json --json
mipsync describe scene.patch --json
```

In PowerShell, quote a minimal patch as one JSON argument:

```powershell
mipsync --project "D:\Games\MyGame" scene patch '[{"op":"replace","path":"/entities/0/active","value":false}]' --confirm true --json
```

Then inspect the scene or entity immediately. If the path selected the wrong
object, undo instead of applying a compensating patch.

## Route multiple Editors

```powershell
mipsync instances --json
mipsync --project "D:\Games\MyGame" --instance "<instance-id>" scene inspect --json
```

Never choose an instance by process order. Match the absolute project path and
use the returned instance ID when routing remains ambiguous.

## Build through the Editor

Stop Play Mode and save intended scene changes before starting a build. These
commands use the same background build service as the Build Settings window:

```powershell
mipsync --project "D:\Games\MyGame" build start pc --json
mipsync --project "D:\Games\MyGame" build inspect --json
```

Targets are `pc`, `ps1`, and `disc`. A successful `build.start` means the job
started. Wait until `build.inspect` reports `running: false`, then require
`finished: true` and `success: true` and check the returned output paths.
CLI builds leave Explorer closed and do not show progress or error dialogs.
Dirty scenes return `save_required`; use `build start ps1 --save-current true`
to save first. Failed/cancelled `build.inspect` results have exit code 1 with
`state`, `exitCode`, `message` and captured `log`. `build cancel` stops the job.
PS1 requires CMake and an installed PSn00bSDK.

If a GUI save/build dialog is already open, use `editor dialog inspect --json`.
It lists the supported ID, pending action and responses. Use
`editor dialog respond scene_save cancel` to keep edits and cancel the original
action, or choose `save`/`discard` to resume it on the next editor frame.
Save failures return immediately and can be retried or cancelled. Pending save
choices make scene mutations return `dialog_required`; queries and explicit
scene.save remain available. Use `editor dialog respond build_progress close`
for a completed GUI build, or `cancel` for a running build. Never infer that
discard is safe merely because a command was blocked.

## Author UI and test gameplay without file patches

Discover `component.list`, `component.describe`, `language.reference` and
`language.limits` before writing scripts. Use `language.compile --target ps1`
and `language.validate` with all build modules and the scene binding count.
Their verifier is shared with the PS1 exporter.

Use `ui.rect`, `ui.text`, `ui.image`, `canvas.set`, `script.attach`,
`script.fields`, `script.reload`, `entity.order` and `entity.grid` for authoring.
Each command's `describe` output lists its typed parameters. UI property names
and defaults come from `component.describe` (for example RectTransform).
`batch.run --file tools/layout.json` executes scene commands in one Undo
transaction and rolls back the scene on failure.

`runtime.play --save-current true` starts asynchronously; poll `runtime.inspect`
until state is playing or failed. Pause before `runtime.input` (PS1 buttons and
axes only) and `runtime.step`; wait for queuedFrames to become zero, then use
`runtime.fields` for live variables and `runtime.capture` for a Game View PNG.
Stop restores the edit scene. Use `script.reload` for explicit recompilation;
Play also recompiles source instead of trusting old module timestamps.

## Inspect and author animation

Use `model.inspect --model assets/models/Character.fbx` for mesh part indices,
bone names/parents and playable clip IDs, FBX stack indices, durations and
channel targets. `animation.list --model ...` returns the clip subset.
`component.describe Animator` and `component.describe SkinnedMeshRenderer`
expose model/controller/state, meshPart and PS1 export settings. Use
`animator.set --entity ... --properties '{...}'`, `skinned-mesh.set` or
`component.set --component MeshRenderer` to author typed properties with Undo.
Both renderer types support `renderType: "Lit"` (default) and `"Unlit"`.
Unlit skips lighting on desktop and PS1; P-Sync excludes that lighting work.

Read `animator.controller.schema` before creating a controller, then use
`animator.controller.set --path assets/animations/Controller.ncontroller
--controller '{...}'`. The state/parameter/transition setters modify existing
controllers. `animator.controller.inspect` returns the document and diagnostics;
`animator.controller.validate` rejects unknown clips, stack mismatches, duplicate
states/parameters and invalid transition references before writing. Controller
asset changes participate in editor.undo/editor.redo. Engine source is not needed.

For independent model regions, `model.extract --model ... --path ...nmodel
--bone-roots '["shoulder.L"]'` selects triangles by their strongest total bone
influence, including descendant bones. `--mesh-parts '[0]'` filters whole mesh
parts. The descriptor preserves the source FBX, skeleton, clips, skin weights
and whole-model fitting; assign it to a SkinnedMeshRenderer and use meshPart -1.
Create separate entities/Animators for independent clips. Animation layers and
bone masks are currently unsupported; `capabilities.animation` states this.
PS1 supports idle/walk/aim and one action per Animator; numeric/bool conditions
on the exported action work through SetFloat/SetBool/SetInt.

`runtime.animator --entity ...` reads current/next state, visible phase,
parameters, transition progress and condition probes. runtime.inspect also
includes each Animator. `runtime.step --buttons '["L1"]' --frames 10` sets input
and queues frames atomically while paused; optional --axes accepts PS1 stick
axes. Wait for queuedFrames=0 before changing input or queuing another step.
`entity.inspect --include-components true` returns serialized component data.
`search "animation bones mask" --kind command --namespace model` filters results.

An IPC access denial is reported as MIPSYNC_IPC_ACCESS_DENIED with the Win32
error, separately from an absent editor. Use matching process permissions;
do not infer an engine crash from a sandbox connection denial.
# Compact scene inspection and edit-mode previews

Use `scene summary --json` and `entity list --with-transform --with-bounds --json`
before requesting full scene JSON. `entity inspect <id> --compact --json` includes
component properties, mesh counts and shape names without raw geometry. Opt into
buffers with `--include-mesh-data`; use `scene get-json --json --out <file>` for a
complete document. Global `--fields` (alias `--select`) projects comma-separated
paths from the result, including array columns such as `entities.id,entities.bounds`.
`--output-format compact`, `--max-depth`, `--quiet` and `--out` control output size.
`--out` is relative to the CLI working directory and suppresses stdout on success.

Render the edit scene without Play: `scene capture --file tools/preview.png
--position 8,8,8 --target 0,2,0 --json` or `scene capture --file tools/top.png
--view top --projection orthographic --json`. `entity frame <id> --capture
tools/entity.png --view iso --json` fits an entity hierarchy without changing
the viewport. PNG paths are project-relative; dimensions default to 960x720.

Create geometry with `promodeler create --name Roof --shape pyramid --size 8,2.5,6
--render-type Unlit --json`. Discover presets/parameters with `promodeler describe`;
`promodeler set <id> --shape wedge` regenerates geometry and replaces face edits.
`material list`, `material inspect <path>`, `asset list --type texture` expose assets.
`entity bounds`, `entity overlap`, `scene raycast` and `entity find --bounds-contains`
use conservative render AABBs, not exact mesh/collider collisions.

`batch run --file -` accepts stdin JSON arrays of `{command,arguments}`.
`--dry-run` executes on an isolated copy with provisional IDs, preserving the live
scene, dirty state, selection and Undo. `--summary` returns IDs/names; failures
include `failedIndex`, `command`, `completedCount`, `results`, `rolledBack`.
Use `commands --ids`, `help <namespace>` (short summaries) or `help <namespace>
--details` / `describe <id>` for full definitions. Boolean flags accept explicit
true/false or a bare flag.
