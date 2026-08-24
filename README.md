# Mipsync

Mipsync is a Windows-first game editor and runtime for authoring projects that target the original PlayStation. It combines a visual editor, scene and asset pipelines, Mips# gameplay scripting, animation, UI, audio, physics, native Windows player builds, a PS1 export path, and the Mipsync Hub project launcher.

This repository contains the engine/editor and Hub source. Release-production infrastructure and generated distribution artifacts are intentionally kept outside this source tree.

## Repository scope

Included:

- The C++20 engine and editor under `src/`
- The Mips# compiler, bytecode VM, and runtime
- Scene, animation, UI, audio, renderer, physics, and asset systems
- Native Windows player and PS1 export code
- The React + Tauri Mipsync Hub under `hub-tauri/`
- Engine/Hub registry, launch, editor-version, and toolchain integration
- Original PS1 runtime source templates under `templates/ps1/`
- Required editor resources and explicitly identified third-party source

Intentionally not included:

- Update, publishing, packaging, deployment, or release automation
- The Hub self-replacing updater and its dedicated updater executable
- Release manifests, release notes, installers, archives, or built engine binaries
- Prebuilt PS1 executables or disc images
- Emulators, BIOS images, PSn00bSDK, Ninja, or any downloaded toolchain cache
- Website source, IDE extensions, example projects, or private test projects

## Requirements

- Windows 10 or Windows 11
- Git
- CMake 3.20 or newer
- A C++20 MinGW-w64 toolchain available on `PATH`
- Node.js 20 or newer and Rust for building the Hub
- The platform prerequisites required by Tauri 2

CMake downloads the pinned source dependencies during configuration. The first configure therefore requires network access.

## Build the engine

```powershell
git clone https://github.com/KasaBranca/Mipsync.git
cd Mipsync
cmake --preset default
cmake --build --preset default
```

The editor executable is produced under `build/src/`.

## Build the Hub

```powershell
cd hub-tauri
npm ci
npm run build
npm run tauri build
```

The first command builds the React frontend; the second Tauri command builds the desktop Hub. For development, use `npm run tauri dev`. Set `MIPSYNC_ENGINE` to a locally built editor executable when it is not beside the Hub binary.

## Run the editor

The editor can be launched directly with an explicit project directory:

```powershell
.\build\src\MipsyncEngine.exe --project "C:\path\to\project"
```

The directory must contain a `nostalty.project` file.

When launched without a project argument, the engine looks for `MipsyncHub.exe` beside the editor. The Hub can also launch an editor selected for a project.

Useful command-line modes:

```text
--project <directory>          Open a project in the editor
--validate-mips <file>        Compile-check a Mips# source file
--test-mips-runtime <file>    Run the Mips# runtime regression entry point
--export-ps1 <directory>      Export PS1 scene and runtime sources
--build-disc <directory>      Stage and build a PS1 disc folder
--player --data <directory>   Run an exported native player data directory
```

## PS1 builds

PS1 compilation requires a separately installed [PSn00bSDK](https://github.com/Lameguy64/PSn00bSDK). Set `PSN00BSDK` to the SDK root before building a PS1 target. Ninja and CMake must also be available on `PATH`.

```powershell
$env:PSN00BSDK = "C:\path\to\PSn00bSDK"
.\build\src\MipsyncEngine.exe --build-disc "C:\path\to\project"
```

No emulator, BIOS, Sony SDK material, or PSn00bSDK source/binary is stored in this repository. Use only BIOS and platform material that you are legally permitted to use.

## Third-party software

Bundled, configure-time, npm, and Cargo dependencies were audited for this source publication. Versions, license families, and notice locations are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md). Full license texts for configure-time engine dependencies are under `third_party/licenses/`; bundled single-file libraries retain their notices in the source files, and Inter retains its SIL OFL license under `resources/fonts/`. Hub dependency resolution is fixed by `package-lock.json` and `Cargo.lock`.

No project-wide license has been selected for the original Mipsync source in this repository. Third-party components remain governed by their respective licenses.

## Current status

Mipsync is under active development. Project formats, Mips# behavior, editor workflows, and PS1 output may change between revisions. Keep backups and validate exported builds on the intended emulator or hardware.
