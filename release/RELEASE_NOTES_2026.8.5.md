## Mipsync 2026.8.5

Editor-only update for Windows x64. Mipsync Hub remains unchanged.

### Fixes

- Changed mesh texture assignment to a Material-only workflow.
- Migrates legacy direct texture bindings to generated materials when loading scenes.
- Fixed PS1 material UV tiling and offset export.
- Fixed subdivided Plane UV generation so a 1x1 material no longer repeats once per subdivision.
- Ensured PS1 builds resolve renderer textures consistently from their assigned materials.

### Assets

- `MipsyncEngine_Windows_x64.zip` — editor, PS1 runtime/templates, and editor tools.
- `manifest.json` — editor update feed; Hub remains on its existing version.
