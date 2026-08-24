# Project panel icons (custom artwork)

Place **PNG** files in this folder. They are copied next to the editor executable on build
(`resources/icons/project/` beside `MipsyncEngine.exe`).

## Format

| Property | Recommendation |
|----------|----------------|
| File format | **PNG** (RGBA, transparent background) |
| Size | **64×64** or **128×128** px (square; scaled to fit each grid cell) |
| Color space | sRGB |
| Naming | Exact filenames below (lowercase, underscores) |

SVG is not loaded at runtime — export to PNG from your design tool.

## Filenames ↔ asset type

| File | Used for |
|------|----------|
| `folder.png` | Folders |
| `script.png` | `.mips` scripts |
| `scene.png` | `.nscene` scenes |
| `prefab.png` | `.nprefab` prefabs |
| `animator_controller.png` | `.ncontroller` animator controllers |
| `animation_clip.png` | Animation clips (children under FBX models) |
| `texture.png` | **Fallback only** when the image file cannot be previewed |
| `material.png` | **Fallback only** when material thumbnail generation failed |
| `model.png` | **Fallback only** when 3D mesh thumbnail is unavailable (e.g. skinned FBX) |
| `other.png` | Unknown / other file types |

## Not replaced by these icons

These types keep **live previews** when possible (unchanged):

- **3D models** (`.fbx`, `.obj`) — rendered mesh thumbnail (static meshes only)
- **Materials** (`.nmat`) — sphere + albedo preview
- **Textures** (`.png`, `.jpg`, …) — the image itself

Custom icons above are used for kinds that never had a preview, or as fallback when preview generation fails.

## Optional files

Any missing PNG falls back to the built-in vector icon (folder/script) or a short text label (SCN, AC, …).

## Example layout

```
resources/icons/project/
  README.md
  folder.png
  script.png
  scene.png
  prefab.png
  animator_controller.png
  animation_clip.png
  texture.png          # optional fallback
  material.png         # optional fallback
  model.png            # optional fallback
  other.png
```
