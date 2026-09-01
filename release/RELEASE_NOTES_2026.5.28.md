## Mipsync 2026.5.28

Binary-only release (Windows x64). PS1 hardware target, with a zero-setup PS1
toolchain bundled inside the engine zip 窶・download, open a project, hit
**Build PS1 && Run in Emulator**, and the bundled emulator boots a Mipsync PS1
build with no extra installation.

### Versioning change
- Engine + Hub releases now use **`YYYY.M.D` date stamps** instead of the
  legacy `0.1.x` scaffolding (today: `2026.5.28`). Cargo's strict semver
  parser rejects leading zeros so `2026.05.28` is normalised to `2026.5.28`;
  Hub's version sorter already does triplet-of-u32 compare so date ordering
  works correctly.
- All pre-`YYYY.M.D` editors (legacy `v0.1.x`) are **hidden from the Hub's
  installer list** going forward. The GitHub releases page still keeps them
  archived, but the Hub treats `2026.5.28` as the new starting line.

### Highlights
- **Starter PSX.EXE no longer boots to a black screen.** The prebuilt
  `templates/ps1/starter/prebuilt/PSX.EXE` was rebuilt against a fixed
  `main.c` that uses correct PSn00bSDK double-buffering. The prior starter
  set `draw` to VRAM `y=240` while `disp` stayed at `y=0` and never
  swapped, so every clear + glyph went off-screen and the user saw nothing
  after OpenBIOS handed control to the PS-EXE.
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
