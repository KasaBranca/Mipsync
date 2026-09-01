## Mipsync 0.1.16

Binary-only release (Windows x64). PS1 hardware target, with a zero-setup PS1
toolchain bundled inside the engine zip — download, open a project, hit
**Build PS1 && Run in Emulator**, and the bundled emulator boots a Mipsync PS1
build with no extra installation.

### Highlights
- **Bundled PCSX-Redux + bundled OpenBIOS.bin**: now ship under ps1_runtime/
  next to the engine. v0.1.15 shipped PCSX-Redux but no BIOS image, so the
  emulator launched and immediately halted at *"No BIOS loaded, emulation
  halted"*. v0.1.16 fetches the OpenBIOS Azure DevOps artifact (PCSX-Redux
  build definition id=2) alongside the emulator drop, stages
  `ps1_runtime/bios/openbios.bin` (512 KB), and the PS1 runner now
  auto-passes `-bios <bundled openbios.bin>` to PCSX-Redux unless the user
  has set an explicit BIOS override in Build Settings or via the Hub.
  PCSX-Redux is GPL-2.0; OpenBIOS is MIT — both freely redistributable.
- **Pre-built PSX.EXE fallback**: even before PSn00bSDK is installed,
  `Build PS1` stages a Mipsync starter PSX.EXE that boots in the bundled
  emulator. Install PSn00bSDK from the Hub afterwards to compile your own
  PSX.EXE on subsequent builds.
- **PS1 runner CLI rewrite**: detects emulator family (PCSX-Redux vs
  DuckStation) and emits the right CLI (`-portable -run -iso/-loadexe` for
  PCSX-Redux, `-fullscreen -cdimage/-exec` for DuckStation). Working
  directory pinned next to the bundled exe so portable state stays out of the
  project.
- **OpenBIOS as default, retail BIOS as optional override** — Hub adds a PS1
  BIOS section to pick an external `openbios.bin`; per-editor BIOS override
  remains available in Build Settings.
- **Build result toasts**: `Build PS1` / `Build && Run` from the menu
  surface success/failure in a bottom-right toast.

### BIOS resolution order
1. Build Settings → BIOS override (per-editor)
2. `MIPSYNC_OPENBIOS_PATH` env (set by the Hub)
3. **Bundled** `ps1_runtime/bios/openbios.bin`

### Assets
- MipsyncEngine_Windows_x64.zip (now ~47 MB; includes `ps1_runtime/`
  with emulator + bundled `openbios.bin`, plus `templates/ps1/` with
  pre-built `PSX.EXE`)
- MipsyncHub_Windows_x64.zip
