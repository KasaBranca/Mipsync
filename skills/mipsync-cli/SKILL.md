---
name: mipsync-cli
description: Operate Mipsync projects and the Mipsync Editor through the Mipsync CLI. MUST use for scene, entity, component, mesh, material, Mips# scripting, validation, or Editor runtime tasks; do not use for changing Mipsync engine C++ source itself.
---

# Mipsync CLI

Use the typed `mipsync` command platform instead of editing an open project's
`.nscene` files directly. The CLI and Editor Console share the same command
registry, validation, result types, Undo history, and Editor main-thread
execution.

## Establish the target

1. If [references/runtime.md](references/runtime.md) exists, read it and use
   its exact CLI path first. It is generated from the Editor version that
   synchronized this Skill. Verify it with `& "<cli-path>" --version` on
   PowerShell.
2. Without a runtime binding, resolve the executable with `mipsync --version`.
   If it is not on `PATH`, look for `build/src/mipsync.exe` in an engine
   checkout or `mipsync.exe` beside an installed Mipsync Editor. Never choose a
   same-named executable from the project's `Builds` output.
3. Run `<cli> instances --json` before using Editor commands.
4. Once the project is known, pass `--project "<absolute-project-path>"`
   explicitly. If more than one Editor is open for that project, also pass
   `--instance <instanceId>`.

The CLI never starts the Editor. Local discovery, project inspection, and
Mips# compilation work without it; scene and runtime commands require a live
Editor session. If none is available, report that the user must open the
project rather than launching or selecting an unrelated Editor.

## Discover instead of guessing

Treat the running CLI registry as authoritative because commands and Mips#
bindings evolve with the engine:

```powershell
mipsync capabilities --json
mipsync help --json
mipsync search "create a textured cube" --json
mipsync describe entity.create --json
mipsync describe Transform.position --json
```

Use `search` when the goal is known but the command or API name is not. Use
`describe` before a mutation when parameter names, defaults, accepted values,
or side effects are uncertain. Prefer `--json` and consume structured fields;
do not scrape human-formatted output.

## Author safely

1. Inspect the current scene and target objects before changing them.
2. Prefer stable entity IDs returned by `entity.list --json` over names when
   names are duplicated.
3. Use the narrowest command that expresses the task. Prefer entity,
   component, mesh, and material commands over `scene.patch`.
4. Execute one logical mutation, inspect the result, and verify the affected
   entity or scene state.
5. If verification fails, use `editor.undo`, verify the rollback, and stop to
   diagnose rather than stacking speculative mutations.
6. Persist a completed, user-authorized authoring task with `scene.save`.
   Do not save read-only work, failed work, or a preview the user wanted kept
   temporary.

Successful authoring commands create Editor Undo snapshots. Authoring is
rejected during Play Mode. Inspect runtime state first; stop Play Mode only
when the requested edit requires it, since stopping restores the edit scene.

Deletion, component removal, and raw scene patching require `--confirm true`.
Supply confirmation only when the user's request authorizes that exact
destructive change. `scene.patch` is an escape hatch: inspect `scene.get-json`
first, send the smallest RFC 6902 patch possible, then verify immediately.

Do not invoke `ide.open` unless opening an external IDE is useful to the user's
request. Do not use CLI commands to broaden a read-only inspection request into
scene mutations.

## Work with Mips#

Use normal workspace file editing for `.mips` source, then validate through the
real compiler:

```powershell
mipsync --project "D:\Games\MyGame" language compile assets/scripts/Player.mips --json
```

Read structured diagnostics and the module's `ps1Compatible` result. Discover
engine bindings with `search` and `describe`; do not invent Unity or .NET APIs
that Mips# has not exposed.

## Handle failures

- A missing Editor means the project is not open or routing is wrong. Re-run
  `instances --json`; do not retry mutations against another project.
- Multiple matching Editors require an explicit instance ID.
- Parse or validation failures should be corrected from their structured
  diagnostics before retrying.
- If an operation may have succeeded before transport failure, inspect state
  before issuing it again to avoid duplicates.

For concrete inspection, authoring, patching, scripting, and playtest recipes,
read [references/workflows.md](references/workflows.md).
