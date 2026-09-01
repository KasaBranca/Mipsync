## Mipsync 2026.5.32

Binary-only release (Windows x64). PS1 hardware target 窶・the PC build is the
editor only; **Build PS1** compiles your startup scene and Mipsync scripts into
a real PSX.EXE via PSn00bSDK.

### Highlights (v2026.5.32)
- **Fix: Build PS1 now finds bundled Ninja.** Installed editors ship
  `tools/ninja.exe` next to `MipsyncEngine.exe`; `build_mipsync.ps1`
  searches there before PATH. Fixes `ninja.exe not found` when PSn00bSDK
  was installed but the compile step still failed (v2026.5.31 regression).
- Scene-driven PS1 runtime (Milestone B): startup scene export, per-entity
  script binding, GTE mesh draw, pad input / physics host calls.
- Hub `hub-settings.json` PSn00bSDK auto-detect when the editor is not
  Hub-launched.
- Build PS1 errors now show the last `build.log` line instead of garbled
  log fragments in the toast.

### Versioning
- Date-stamped releases: `YYYY.M.D` (e.g. `2026.5.32`). Legacy `v0.1.x`
  editors are hidden from the Hub installer list.

### Assets
- MipsyncEngine_Windows_x64.zip (engine + `ps1_runtime/` + `templates/ps1/`)
- MipsyncHub_Windows_x64.zip
