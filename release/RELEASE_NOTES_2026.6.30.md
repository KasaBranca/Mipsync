## Mipsync 2026.6.30

Binary-only release (Windows x64). PS1 hardware target 窶・the PC build is the
editor only; **Build PS1** compiles your startup scene and Mipsync scripts into
a real PSX.EXE via PSn00bSDK.

### Highlights (v2026.6.30)
- Major Animator Controller editor usability and graph presentation overhaul.
- Faster idle editing: scene dirty checks no longer serialize the full scene every frame.
- Inspector asset fields now show compact filenames, previews, and in-field clear controls.
- Hierarchy and Inspector selection now persist across editor restarts.
- Camera-relative movement now preserves only keys held through a camera cut;
  newly pressed keys immediately use the new camera direction.
- Updated PS1 runtime templates and bundled `tools/ninja.exe` for PS1 builds.

### Versioning
- Date-stamped releases: `YYYY.M.D` (e.g. `2026.6.30`). Legacy `v0.1.x`
  editors are hidden from the Hub installer list.

### Assets
- MipsyncEngine_Windows_x64.zip (engine + `ps1_runtime/` + `templates/ps1/`)
- MipsyncHub_Windows_x64.zip
