# Project renderer sources

Opening a project in the Editor automatically creates `renderer/pc/` and
`renderer/ps1/runtime/`, including for projects created by older versions.
Use normal source-file editing here; scene JSON does not need to change.

## PC and Editor preview

- `mesh.vert`, `skinned.vert`, and the shared `mesh.frag` draw static and
  animated geometry. Preserve the attribute locations and uniforms used by
  the supplied shaders when their engine inputs are needed.
- `screen.vert` and `screen.frag` control the final screen pass. The screen
  vertex stage is also used with `skybox.frag`.
- `outline-mask.*` and `outline-compose.*` control the selection outline.

The Editor checks source contents about once per second. Saving a valid set
replaces all shader programs together. Compiler/linker errors are logged and
the preceding valid set remains active; fix and save to retry. On initial
failure the built-in shaders remain active. Removing an override uses its
built-in fallback until the next open seeds that file again.

PC player builds package project GLSL inside encrypted `game.mpak`. They
load it at startup without an external shader directory or file watcher.
GLSL changes affect PC rendering only; PS1 has its own C/GTE/GPU pipeline.

## PS1 renderer

Edit `renderer/ps1/runtime/render.c` and `render.h` for primitive generation,
clipping, ordering-table submission and frame rendering. Lighting, texture,
GPU transfer, UI, fixed-point and projection helpers are supplied beside
them. The full runtime is available so dependent interfaces can be changed
together. Preserve the entry points called by `main.c` and generated scene
data, or adapt the corresponding runtime callers too.

Build/Export copies the standard runtime, then overlays these sources for
every scene, including the runtime used for PS1 Play. Additional `.c`, `.s`,
and `.S` helpers under this directory are compiled automatically. `vm.c` is
the legacy interpreter and is excluded from the native AOT build because
`native.c` provides its entry points.

Use `search`/`describe` to discover PS1 export/build commands. A PS1 SDK build
is required to run changes. If it fails, inspect `Builds/PS1/<Product>/build.log`.
Customized runtimes never silently fall back to a standard prebuilt binary.
Edits inside generated `Builds/.../ps1_src/` are overwritten; keep source
changes in `renderer/ps1/runtime/`.

## Engine upgrades

On every open the Editor installs missing defaults and updates files still
equal to the previous defaults. Customized sources are retained, including
files present before their first migration. The current standard versions
are kept in `.nostalty/renderer-defaults/` to compare or merge manually.
Keep `renderer/` and that baseline directory with the project when moving
it between machines if automatic updates of unchanged defaults are wanted.
Custom code may need adapting when engine runtime interfaces change.

The engine-managed CLI Skill is synchronized separately: its bundled files
are replaced and retired references are removed. Personal instructions
outside the managed block in `AGENTS.md` remain intact.
