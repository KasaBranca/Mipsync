## Mipsync 2026.9.1

Windows x64 release. Mipsync targets real PS1 hardware — the PC executable is
the Editor, while **Build PS1** compiles your startup scene and Mips# scripts
into a real PSX.EXE via PSn00bSDK.

### Highlights (v2026.9.1)
- Agent-ready `mipsync` CLI with structured command discovery, Editor IPC,
  safe scene authoring, undo support, and project-scoped Codex Skills.
- Version-matched CLI binding: projects automatically point agents at the CLI
  shipped beside their exact Editor build.
- Major Editor UX pass across multi-selection, the common Inspector, hierarchy
  and Scene View selection, inline asset creation/renaming, and build progress.
- ProModeler cylinder creation, face deletion, optional mesh subdivision
  previews, collider defaults, and pivot fixes.
- PS1 renderer improvements for depth ordering, fog, frustum/distance culling,
  UV tiling, textures, character rendering, and large planar meshes.
- Mips# runtime and animation improvements, including Has Exit Time parity and
  additional APIs used by project scripts.
- Updated PS1 runtime templates, bundled language tools, and `tools/ninja.exe`.

### Versioning
- Date-stamped releases: `YYYY.M.D` (e.g. `2026.9.1`). Legacy `v0.1.x`
  editors are hidden from the Hub installer list.

### Assets
- MipsyncEngine_Windows_x64.zip (Editor + CLI + Skills + PS1 runtime/templates)
- MipsyncHub_Windows_x64.zip
