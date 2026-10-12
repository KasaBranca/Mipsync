# CLI authoring and runtime checks

The Tetris CLI experiment exposed gaps in discovery, UI authoring and PS1
preflight. Commands below use the editor scene and its Undo history. They do
not edit `.nscene` files directly. Supply `--project <directory>` when more
than one project/editor is open.

## Discovery and PS1 preflight

```powershell
mipsync component list --json
mipsync component describe RectTransform --json
mipsync describe Transform --json
mipsync language reference --json
mipsync language limits --json
mipsync language compile assets/scripts/Tetris.mips --target ps1 --json
mipsync language validate '["assets/scripts/Tetris.mips","assets/scripts/Display.mips"]' --bindings 4 --json
```

`language.compile` and `language.validate` call the same `ValidatePs1Target`
used by export. Results include constant-pool sizes, bytecode/locals per method
and all bounded VM capacities. A desktop-valid file can have
`ps1Compatible: false`; `--target ps1` and `language.validate` exit unsuccessfully
on incompatibility. Aggregate validation must include every script module in
the build and the largest scene's binding count. This is script validation;
textures/audio/disc layout still require a complete build.

Compiler errors identify file, line and column, including unsupported method
calls and assignments. User-defined method calls, direct writes to another
script's scalar fields and serialized array overrides remain unsupported;
`language.reference` describes these restrictions before authoring.

## UI and scripts

```powershell
mipsync component add Canvas Canvas
mipsync canvas set Canvas --properties '{"referenceResolution":[320,240],"sortOrder":0}'
mipsync entity create Score --parent Canvas
mipsync ui text Score --properties '{"text":"0","fontSize":18,"alignment":1,"color":[1,1,1,1]}'
mipsync ui rect Score --properties '{"anchoredPosition":[0,80],"sizeDelta":[120,24]}'
mipsync script attach Score assets/scripts/Display.mips --fields '{"score":0}'
mipsync script fields Score --fields '{"score":12}'
mipsync script reload
mipsync component get Score UIText --json
```

UI additions automatically add RectTransform. `component.describe` returns
actual serialized defaults and property types; UI setters validate dimensions,
RGBA/anchor ranges, enum values and asset paths before changing the scene.
Each command remains undoable. Play always recompiles attached source files,
so edits do not depend on timestamp granularity. Explicit reload also updates
all instances of the same script and preserves compatible field overrides.

Scripts can update text without overlapping digit entities:

```csharp
UI.SetTextNumber(scoreEntityId, score);
UI.SetText(scoreEntityId, "READY");
UI.SetColor(scoreEntityId, 1, 0.5, 0, 1);
```

These APIs run on PC and PS1. SetText accepts up to 95 UTF-8 bytes; PS1 renders
exported glyphs and its built-in ASCII glyphs. SetTextNumber truncates toward
zero and uses the common signed integer range -32768..32767. The PS1 dynamic
UI table holds 64 text/image elements. Check the API's boolean return value.

## Grids, drawing order and batches

```powershell
mipsync entity grid Cell --rows 20 --columns 10 --dx 12 --dy -12 --name Cell --json
mipsync entity order Background --index 0
mipsync batch run --file tools/layout.json --json
```

Grid results contain IDs in row-major order. Copies append in that same sibling
order; the template stays in place. IDs are explicit, so callers need not assume
contiguous allocation. Up to 4096 entities including descendants may be created.
Higher sibling indices draw later/in front within their UI parent; Canvas
sortOrder controls ordering between canvases.

A batch JSON file is an array of `{ "command": "ui.text", "arguments": ... }`
objects. Up to 4096 scene-authoring operations run in one IPC request and one
Undo transaction. A failing operation rolls the whole scene batch back and
reports `failedIndex`, `command`, `rolledBack`. Disk writes/builds/runtime
actions/nested batches are rejected to make rollback predictable.

## Runtime tests

```powershell
mipsync runtime play --save-current true --json
mipsync runtime inspect --json
mipsync runtime pause --paused true
mipsync runtime input --buttons '["Cross"]' --axes '{"RightStickX":0.25}'
mipsync runtime step --frames 1 --delta 0.0166666667 --json
mipsync runtime inspect --json
mipsync runtime fields Score --json
mipsync runtime capture tools/score.png --json
mipsync runtime stop
```

Play returns `status: pending` and `state: preparing` until preparation finishes.
Unsaved scenes return `save_required` without opening an editor dialog.
`runtime.inspect` remains available during preparation and includes failure
reason, instruction budget, last invocation's instruction usage and runtime
diagnostics. A source compile or Awake/Start failure aborts preparation and
restores the edit scene.

Step runs exactly the queued gameplay frames at the supplied delta while
keeping Play paused. Input injection is PS1 input only and applies to these
frames, with correct held/down/up edges; resume/Stop clears it. Poll
`queuedFrames: 0` before reading variables or capturing a frame. Runtime fields
are live values, unlike serialized inspector defaults. PNG capture includes
the UI in the latest rendered Game View, without editor chrome. It fails if
Game View has not rendered yet. Runtime values/scene changes revert on Stop;
PNG files are project file changes and persist.

## Save confirmations and build failures

CLI `scene.open`, `runtime.play` and `build.start` return `save_required`
immediately for an unsaved scene. Save first or pass `--save-current true`;
these commands do not open a save confirmation. `scene.save` reports write
errors directly and retains the dirty state on failure.

```powershell
mipsync build start ps1 --save-current true --json
mipsync build inspect --json
mipsync build cancel --json
mipsync editor dialog inspect --json
mipsync editor dialog respond scene_save cancel --json
mipsync editor dialog respond build_progress close --json
```

CLI builds never open progress/error dialogs or Explorer. Poll `build.inspect`
until `running` is false. The result includes `state` (idle/running/complete/
failed/cancelled), `exitCode`, `message`, captured `log` and output paths.
Failed or cancelled builds make `build.inspect` exit with code 1 while keeping
the structured result available. `build.cancel` stops the isolated process and
its children and reports successful cancellation. An overlapping `build.start`
returns `busy` without opening a dialog.
Scripts with compiler diagnostics are rejected before export; partial bytecode
cannot make an invalid PS1 build appear successful.

If a user opened a save/build dialog through the GUI, `editor.dialog.inspect`
lists supported dialog IDs, the original pending action and available responses.
`scene_save` accepts `save`, `discard`, `cancel`; save/discard resumes that original
action on the next editor frame, including opening a scene, entering Play,
closing or restarting. Cancel preserves the current scene and edits. Failed
saves return the error immediately and leave the confirmation available to
retry or cancel. Scene mutations return `dialog_required` while a save choice
is pending; read-only queries, explicit scene.save, runtime.stop and build.cancel
remain usable. `build_progress` accepts `cancel` while building or `close` after
completion. GUI build dialogs keep their existing behavior. These commands do
not respond to unrelated asset-creation or native file-picker dialogs.
