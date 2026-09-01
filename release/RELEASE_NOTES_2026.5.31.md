## Mipsync 2026.5.31

Binary-only release (Windows x64). PS1 hardware target 窶・the PC build is the
editor only; **Build PS1** compiles your startup scene and Mipsync scripts into
a real PSX.EXE via PSn00bSDK.

### Highlights (v2026.5.31)
- **Scene-driven PS1 runtime (Milestone B).** Build PS1 exports the startup
  scene (`scene_data.c`) and scene-referenced scripts (`scripts_data.c`),
  binds scripts per entity, and runs `Awake` / `Start` / `Update` on
  device. Transform rotation/position, `Input` (pad mapped from WASD/mouse),
  and `Physics_Move` / `IsGrounded` work through the mini-VM.
- **GTE mesh rendering.** Exported cube/plane entities draw as coloured 3D
  primitives with a scene camera; no more static placeholder text screen when
  scripts are embedded.
- **Hub PSn00bSDK auto-detect.** Build PS1 reads `hub-settings.json` when
  `PSN00BSDK` is not in the environment (e.g. launching the editor directly),
  so scripts compile without requiring a Hub-launched process.
- **Honest build failures.** If your scene has scripts but PSn00bSDK cannot
  compile PSX.EXE, Build PS1 now errors instead of silently substituting the
  empty prebuilt starter.
- PS1 starter runtime fixed for **PSn00bSDK 0.24** (pad API, GTE/double-buffer,
  `inline_c.h` macros). Prebuilt `PSX.EXE` refreshed (~43 KB with VM).

### Versioning
- Date-stamped releases: `YYYY.M.D` (e.g. `2026.5.31`). Legacy `v0.1.x`
  editors are hidden from the Hub installer list.

### Assets
- MipsyncEngine_Windows_x64.zip (engine + `ps1_runtime/` + `templates/ps1/`)
- MipsyncHub_Windows_x64.zip
