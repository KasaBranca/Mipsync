## Mipsync 1.0.0f1

The first formal Mipsync release, bringing native PS1 scripts, editable game
renderers and expanded scene-authoring tools to the current Windows editor.

### Native PS1 scripts

- Mips# scripts are compiled through generated C into native MIPS code for PS1.
  Editor Play and Windows players retain their existing VM.
- PS1 memory-card saves, raycasts, application quit and runtime audio clip
  selection join the supported platform APIs.
- See [native PS1 documentation](https://github.com/KasaBranca/Mipsync/blob/v1.0.0f1/docs/NativePS1.md)
  for supported APIs and capacity limits.

### A renderer for each game

- Edit project-local PC shaders and PS1 runtime sources to customize your game.
- PC shader edits reload in the Editor. PS1 changes take effect on rebuild.
- Engine upgrades preserve customized renderer files and update unedited defaults.
- Project CLI skills stay synchronized with the Editor version.

### Expanded authoring CLI

- Capture scenes from chosen cameras without entering Play mode.
- Create and edit more geometry shapes, including cones and pyramids.
- Inspect bounds and components without dumping raw geometry; select output
  fields, save reports to files, and preview batches with a dry run.
- Batch scene edits retain Undo support and roll back on failure.

### README

The banner now automatically uses dark lettering in light themes and white
lettering in dark themes, preserving the original logo colors and contours.

### Community contribution

Huge thanks to **@lfelipe** for the project's first pull request,
[PR #1](https://github.com/KasaBranca/Mipsync/pull/1), developed while integrating
Mipsync with Grandleon. It improves PS1 terrain caching, paged-scene visibility,
indexed textures, GPU clipping and visibility-cache costs. This release also
fixes forward shared-palette references and registers the new projection test.

### Upgrading from calendar-era releases

Releases now use **MAJOR.MINOR.PATCH[abf]BUILD**, beginning with **1.0.0f1**.
`a` denotes alpha, `b` beta and `f` final; the trailing number identifies the build.

**Calendar-era Hub users need a one-time manual Hub update.** Download
`MipsyncHub_Windows_x64.zip`, close the old Hub, and extract the new Hub to a
separate directory or replace the old Hub files. Run `MipsyncHub.exe`.
The old Hub cannot discover the new numbering scheme automatically.
Existing projects, installed editors and settings are retained. Calendar-era
editors remain available in the new Hub; projects keep their selected version.

### Validation

- Windows Release Editor/CLI builds and all 44 CTest cases pass.
- Hub version migration and ordering tests, frontend build and Rust tests pass.
- Mips# language tooling and real TextMate grammar tests pass.
- Real Editor CLI workflow and project renderer migration tests pass.
- Native PS1 integration passes in PCSX-Redux, including memory-card save/read
  and data handoff across scene loads. Custom PS1 renderer sources compile for
  two scenes, and invalid sources correctly fail the build.

### Downloads

- **MipsyncEngine_Windows_x64.zip** — Editor, matching CLI, agent skill,
  language tools, PS1 templates, bundled starter and emulator runtime.
- **MipsyncHub_Windows_x64.zip** — Hub 1.0.0f1 and updater.
- **manifest.json** — update feed, retaining previous editor releases.
