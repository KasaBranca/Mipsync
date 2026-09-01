## Mipsync 2026.5.30

Binary-only release (Windows x64). PS1 hardware target, with a zero-setup PS1
toolchain bundled inside the engine zip 窶・download, open a project, hit
**Build PS1 && Run in Emulator**, and the bundled emulator boots a Mipsync PS1
build with no extra installation.

### Versioning
- Engine + Hub releases use **`YYYY.M.D` date stamps** (today: `2026.5.30`).
  Cargo's strict semver parser rejects leading zeros so the format is
  `YYYY.M.D` rather than `YYYY.MM.DD` (e.g. `2026.5.28` not
  `2026.05.28`). Hub's version sorter does triplet-of-u32 compare so date
  ordering works correctly.
- Pre-`YYYY.M.D` editors (legacy `v0.1.x`) are hidden from the Hub's
  installer list. The GitHub releases page still keeps them archived for
  forensics.

### Highlights
- **Phase 3 Milestone A 窶・Mipsync scripts now run on the PS1.** The starter
  PSX.EXE is no longer a static placeholder: it ships a 32 KB mini-VM that
  decodes a new `.mbc` (Mipsync byte code) format, drives `Awake` /
  `Update` on every embedded script, and routes host calls (`Log.Info`,
  `Time.deltaTime`, `Mathf.Sin/Cos/Sqrt/Abs/Clamp`) through a fixed-point
  (Q16.16) numerics layer. `Build PS1` walks the project for `*.mips`
  files, compiles them through the engine's existing Mipsync compiler, and
  stamps the resulting bytecode straight into the PSn00bSDK build as
  `generated/scripts_data.c` (no CD file I/O, works under `-loadexe`
  too). Build result toast now reports `N Mipsync scripts embedded`.
- **Stale `out/` artefacts no longer mask new builds.** Ps1Build wipes
  `out/PSX.EXE`, `out/game.cue`, `out/game.bin`, and `out/SYSTEM.CNF`
  at the start of every build. Carried over from v2026.5.29 窶・keeps the
  no-SDK + has-SDK paths honest across engine upgrades.
- Starter `main.c` uses proper PSn00bSDK double-buffering (carried over);
  no more black-screen-after-OpenBIOS regression.
- **Bundled PCSX-Redux + bundled OpenBIOS.bin** under `ps1_runtime/` (no
  retail BIOS dump required). PS1 runner auto-passes `-bios <bundled>` to
  PCSX-Redux unless the user has set an explicit override.
- **Pre-built PSX.EXE fallback**: even before PSn00bSDK is installed, Build PS1
  stages a Mipsync starter PS-EXE that boots in the bundled emulator. Install
  PSn00bSDK from the Hub afterwards to compile your own PSX.EXE on subsequent
  builds.
- **PS1 runner CLI rewrite**: detects emulator family (PCSX-Redux vs DuckStation)
  and emits the right CLI (-portable -run -iso/-loadexe for PCSX-Redux,
  -fullscreen -cdimage/-exec for DuckStation). Working directory pinned next to
  the bundled exe so portable state stays out of the project.
- **OpenBIOS as default, retail BIOS as optional override** 窶・Hub adds a PS1 BIOS
  section to pick an external `openbios.bin`; per-editor BIOS override remains
  available in Build Settings.
- **Build result toasts**: `Build PS1` / `Build && Run` from the menu now
  surface success/failure in a bottom-right toast with "Open Build Settings" and
  "Dismiss" actions (previously the result was silently discarded).
- PS1 starter migrated to **PSn00bSDK 0.24 CMake + Ninja** pipeline; uild_mipsync.ps1
  rewritten to drive CMake/Ninja directly.
- `scripts/fetch_ps1_runtime.ps1` correctly flattens the Azure DevOps drop
  layout (`assets/fonts/` 竊・`fonts/`) so PCSX-Redux's `findResource()` actually
  locates fonts 窶・without this the bundled emulator launched into a blank window
  with a font `IsLoaded()` assert.

### Assets
- MipsyncEngine_Windows_x64.zip (now includes `ps1_runtime/` 竕・80 MB and
  `templates/ps1/starter/prebuilt/PSX.EXE`)
- MipsyncHub_Windows_x64.zip
