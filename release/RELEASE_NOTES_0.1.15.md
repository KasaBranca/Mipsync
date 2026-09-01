## Mipsync 0.1.15

Binary-only release (Windows x64). PS1 hardware target, with a zero-setup PS1
toolchain bundled inside the engine zip 窶・download, open a project, hit
**Build PS1 && Run in Emulator**, and the bundled emulator boots a Mipsync PS1
build with no extra installation.

### Highlights
- **Bundled PCSX-Redux + OpenBIOS** ships under ps1_runtime/ next to the engine.
  PCSX-Redux has OpenBIOS statically built in, so no external BIOS file is required
  for the default flow. (GPL-2.0, redistribution permitted.)
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
