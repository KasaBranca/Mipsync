# Mipsync PS1 target (real hardware)

Mipsync on PC is **editor + emulator preview only**. Shipping builds target **PlayStation 1 hardware** (or accurate emulators for testing).

## Zero-setup workflow

Mipsync ships **PCSX-Redux + OpenBIOS** alongside the editor — out of the box you can
download Mipsync, create a project, hit **Build && Run in Emulator**, and see your PS1
build boot without touching any BIOS, emulator, or PATH configuration.

1. Edit scenes/assets in the editor (OpenGL preview — not bit-accurate PS1 GPU).
2. **Build → Build Settings (PS1)** — configure product name, scenes (emulator path is
   auto-detected from the bundled runtime; override only if you want a different one).
3. **Build PS1** — stages `Builds/PS1/<Product>/` and runs PSn00bSDK when installed.
4. **Build && Run in Emulator** — launches the bundled PCSX-Redux in portable mode
   (`-portable -iso file.cue -run`); OpenBIOS is statically built into PCSX-Redux, so
   no external BIOS file is required.
5. Burn `out/game.cue` / flash `PSX.EXE` for hardware (modchip / cheat cart / PSIO as applicable).

## Bundled PS1 runtime

The engine zip contains:

```
ps1_runtime/
  BUILD.txt                    # PCSX-Redux build id / fetch timestamp
  LICENSE-PCSX-Redux.txt       # GPL-2.0
  emulator/
    pcsx-redux.exe + .main + DLLs
    assets/ (fonts, i18n, gamecontrollerdb)
  tools/
    exe2elf.exe exe2iso.exe ps1-packer.exe psyq-obj-parser.exe ...
```

PCSX-Redux is GPL-2.0; redistribution is permitted. `tools/ps1-packer.exe` can produce
compressed self-decompressing PS-EXE binaries; `tools/exe2iso.exe` can wrap a PS-EXE
into a bootable ISO. The Mipsync release fetches the latest successful build from the
official `grumpycoders/pcsx-redux` Azure DevOps pipeline via
`scripts/fetch_ps1_runtime.ps1`.

To refresh the runtime locally during development:

```powershell
powershell -ExecutionPolicy Bypass -File scripts/fetch_ps1_runtime.ps1 -Force
```

The engine resolves the emulator in this order:

1. `prefs.emulatorPath` (set in Build Settings → PS1 Emulator)
2. `MIPSYNC_PS1_EMULATOR` environment variable
3. Bundled `ps1_runtime/emulator/pcsx-redux.exe`
4. Common DuckStation install locations (legacy fallback)

## Toolchain

Install [PSn00bSDK](https://github.com/Lameguy64/psn00bsdk) and set:

```powershell
$env:PSN00BSDK = "C:\path\to\psn00bsdk"
```

Then build from the editor or run `Builds/PS1/<Product>/build.ps1`.

## Layout after build

```
Builds/PS1/MyGame/
  build.ps1
  mipsync_export/     # editor project + boot.json + export manifest
  ps1_src/            # PSn00bSDK C starter (from templates/ps1/starter)
  out/
    PSX.EXE           # when SDK build succeeds
    game.cue          # emulator / CD mastering
    SYSTEM.CNF
```

## BIOS

Mipsync **defaults to OpenBIOS** — the MIT-licensed PlayStation BIOS replacement from PCSX-Redux.

- **Bundled path (default):** the engine ships PCSX-Redux, which has OpenBIOS statically
  linked into the executable. No external BIOS file is required.
- **Other emulators (DuckStation etc.):** in the **Hub → Installs → PS1 BIOS**, point
  Mipsync at an `openbios.bin` extracted from a PCSX-Redux build (or any other
  redistributable PS1 BIOS). The Hub exports this as `MIPSYNC_OPENBIOS_PATH` to the
  editor, which passes it to the emulator with `-bios`.
- **Retail BIOS (optional override):** for higher-fidelity testing, set a per-editor BIOS
  path in **Build Settings → PS1 → BIOS override**. The override always wins over
  OpenBIOS. **Sony BIOS images are not distributed** — use your own dump.
- Hardware homebrew typically boots via **PSn00bSDK** without any retail BIOS dependency.

## License file (for PS2 / POPStarter compatibility)

To boot the constructed PSX disc image on PlayStation 2 using **POPStarter (POPS)**, a valid PS1 license file is required to be injected into the first 16 sectors of the disc. Without this, POPS will reject the disc image and return to the PS2 browser screen.

The engine automatically searches for one of the following license files during build:
- `licensea.dat` (America)
- `licensee.dat` (Europe)
- `licensej.dat` (Japan)

You can place the license file in any of the following locations:
1. Your project root folder (e.g. `MyProject/licensea.dat`)
2. The engine root directory (where the editor executable resides)
3. The `ps1_runtime` directory under the engine root
4. The templates directory (`templates/ps1/licensea.dat`)

If a license file is found, Mipsync copies it into `ps1_src/`, and automatically patches `iso.xml` to include the `<license>` tag. `mkpsxiso` will then inject this license file into the generated `.bin`/`.cue` image.

## PS2 Disc Folder (MechaPwn)

MechaPwn fore-unlock is a softmod/region-unlock exploit for late model PS2 consoles (SCPH-50000 through SCPH-90000). Once unlocked, these consoles can run burned PS1 CD-Rs natively in PSX hardware mode.

To support this workflow, the editor includes a **Build PS2 Disc Folder** option (available in both the **Build** menu and **Build Settings** window). This generates a folder at `Builds/PS1/<Product>/disc/` containing the exact directory structure ready to burn:

```
Builds/PS1/MyGame/disc/
  SYSTEM.CNF           # PS1 boot descriptor
  PSX.EXE              # The game binary
  licensea.dat         # PS1 license file (if found during build)
  IMGBURN_README.txt   # Step-by-step burn instructions
```

### Burning with ImgBurn

To burn the generated folder correctly, use **ImgBurn** with the following configuration:

1. Select **Write files/folders to disc** (Build Mode).
2. Drag/add all files *inside* the `disc/` folder (not the `disc/` folder itself) to the Source list. The files must be at the root of the disc.
3. On the right-hand panel, go to the **Options** tab:
   - **Data Type**: `MODE2/XA` (Essential for PS1 hardware compatibility)
   - **File System**: `ISO9660` (Do not use UDF or Joliet)
4. Go to the **Labels** tab and specify a Volume Label (e.g. `MIPSYNC_GAME`).
5. Insert a high-quality blank CD-R (PS1 hardware cannot boot DVD-Rs for PS1 games).
6. Click the Build/Burn button.

## Roadmap (not done yet)

| Phase | Work |
|-------|------|
| Now | Stage export + PSn00b starter + emulator launch |
| Next | Scene → GPU packet / TIM texture pipeline |
| Next | Mips# → MIPS or drop Mips# on PS1 |
| Next | CD image builder (MODE2/2352) in Build menu |
| Later | In-editor emulator framebuffer (embedded runner) |

## Environment variables

| Variable | Purpose |
|----------|---------|
| `PSN00BSDK` | PSn00bSDK root for `build.ps1` |
| `MIPSYNC_PS1_EMULATOR` | DuckStation (or compatible) executable |

Editor prefs are also stored in `%APPDATA%/MipsyncEngine/editor.json`.
