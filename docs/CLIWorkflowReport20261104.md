# Scene authoring CLI improvements (2026-11-04)

Implements the findings from `TheAmazingDigitalCircus/docs/mipsync-cli-workflow-report.md`.
Commands target the running editor; specify `--project <directory>` when needed.

## Inspect without dumping geometry

```powershell
mipsync entity list --with-transform --with-bounds --json
mipsync entity inspect 38 --compact --json
mipsync entity inspect 38 --include-components --json
mipsync entity inspect 38 --include-mesh-data --json
mipsync scene summary --json
mipsync entity list --with-bounds --fields entities.id,entities.name,entities.bounds --output-format compact
mipsync scene get-json --json --out tools/scene.json
```

`include-components` and `compact` omit raw ProModeler vertices, indices and face-ID
buffers, returning counts and `shapeName`. `include-mesh-data` opts into full
serialized component data; `compact` takes precedence. Shape `custom` means
edited geometry: the original primitive cannot reliably be recovered.
Scene serialization itself remains complete for export, Undo and raw inspection.

Global `--fields` / `--select` accept comma-separated paths relative to the result
(`entities.id`, for example). Paths project across arrays and preserve row order.
Status and diagnostics remain in the protocol envelope. `--max-depth 0..64`
replaces deeper containers with omission/count metadata. `--output-format compact`
selects single-line JSON. `--out` writes the selected human/JSON output to a path
relative to the CLI working directory and suppresses stdout on success; the parent
directory must exist. `--quiet` suppresses successful output, preserving errors and
exit codes. Boolean command flags accept either a bare flag or explicit true/false.

## Preview directly in edit mode

```powershell
mipsync scene capture --file tools/preview.png --position 8,8,8 --target 0,2,0 --json
mipsync scene capture --file tools/top.png --view top --projection orthographic --json
mipsync entity frame 112 --capture tools/roof.png --view iso --json
mipsync entity frame 112
```

Captures use Scene View rendering without editor chrome, Play or scene saves.
PNG output must be inside the project. Presets: scene (current camera), top,
bottom, front, back, left, right, iso. Position requires target; a target by itself
uses a fitted camera. `--width` / `--height` accept 16..4096 (default 960x720).
Orthographic projection accepts `--ortho-size`; otherwise bounds determine size.
Framing captures fit the entity and descendants and preserve viewport/selection.
Framing without `--capture` focuses/selects the entity in Scene View.

`runtime.capture` retains Game View capture with no camera arguments. Supplying
`--position` / `--target` renders a separate camera without changing the game
camera, runtime state or existing Game View framebuffer.

## Editable shapes and materials

```powershell
mipsync promodeler describe --json
mipsync promodeler create --name Roof --shape pyramid --size 8,2.5,6 --render-type Unlit --json
mipsync promodeler create --name Post --shape cylinder --sides 8 --size 0.3,4.9,0.3 --json
mipsync promodeler set Roof --shape wedge --json
mipsync material list --json
mipsync material inspect assets/materials/RoofRed.nmat --json
mipsync asset list --type texture --json
```

Shapes: box, plane, wedge (ramp alias), stairs, cylinder, cone, pyramid. Sizes
are positive local XYZ dimensions; sides 3..64, steps 1..32. Creation supports
parent/material/render-type. Regeneration replaces edited faces and overrides;
setting material/render-type alone preserves geometry. Existing Custom geometry
requires an explicit shape when regenerating. Commands use normal editor Undo.
Cone/pyramid presets are also available in the ProModeler GUI dropdown.
`component.describe ProModeler` includes shape enum names. `entity.create` and
`mesh.set` accept `--render-type Lit|Unlit`.

## Batches, discovery and spatial queries

```powershell
# stdin is consumed by the CLI, never by the editor IPC server
Get-Content -Raw tools/roof.json | mipsync batch run --file - --summary --json
mipsync batch run --file tools/roof.json --dry-run --summary --json
mipsync commands --ids --json
mipsync help promodeler
mipsync help entity --details
mipsync entity bounds 112 --world --json
mipsync entity overlap 112 22 --json
mipsync scene raycast --origin 0,6,0 --direction 0,-1,0 --json
mipsync entity find --component ProModeler --bounds-contains 0,0,0 --json
```

Batch format:

```json
[{"command":"entity.create","arguments":{"name":"Start Roof","primitive":"empty"}}]
```

`dry-run` executes on an isolated copy, then restores the original live scene
without changing dirty state, selection or Undo. IDs are provisional. `summary`
returns command/ID/name fields. Failure includes failedIndex, command,
completedCount, rolledBack, dryRun and completed results; results describe attempted
operations and their IDs no longer exist after rollback. Filesystem/runtime/build
operations and nested batches remain prohibited. New ProModeler operations are
allowed. `--quiet` avoids printing successful batch results.

Help defaults to short ID/summary entries. `describe` retains full metadata and
examples; `help <namespace> --details` returns full parameter definitions.
Search indexes parameter names/descriptions/examples and returns `matchedOn`.

Spatial queries use **render geometry AABBs**, not exact triangles, colliders or
animated skinned poses. Bounds include parent transforms, rotation and negative
scale; local bounds use `entity bounds --world false`. Missing geometry returns
null. Ray directions are normalized and hits are sorted by world distance;
overlap is conservative and touching counts as overlap.

## Play/Stop dirty state

ProModeler loading now preserves serialized UVs/normals instead of recalculating
authored data. Face material assignments serialize in stable face-ID order.
These prevent spurious fingerprint changes after Play restoration. Actual
unsaved edits remain dirty; `scene.inspect` / `scene.summary` report
`dirtyReason: scene_differs_from_saved_snapshot` (or null when clean).
