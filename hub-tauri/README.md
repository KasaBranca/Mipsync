# Mipsync Hub (Tauri)

React + Tauri 2 project launcher for Mipsync Engine. Reads and writes the same registry as the C++ editor:

- Windows: `%APPDATA%\MipsyncEngine\hub.json`
- Linux: `~/.config/nostalty/hub.json`

## Prerequisites

- [Node.js](https://nodejs.org/) 20+
- [Rust](https://rustup.rs/)
- Built `MipsyncEngine.exe` (for **Open project**)

## Development

```bash
cd hub-tauri
npm install
npm run tauri dev
```

Set `MIPSYNC_ENGINE` if the engine is not next to the hub binary:

```powershell
$env:MIPSYNC_ENGINE = "D:\Nostalty\build\src\MipsyncEngine.exe"
npm run tauri dev
```

## Release build

```bash
npm run tauri build
```

Output (Windows): `src-tauri/target/release/MipsyncHub.exe` (name follows `productName` in `tauri.conf.json`).

Copy **`MipsyncHub.exe`** beside **`MipsyncEngine.exe`**. When you run the engine without `--project`, it launches the hub and exits.

## Icons

```bash
npm run tauri icon ../resources/icons/app_icon.png
```

This regenerates `src-tauri/icons/` from the engine app icon.
