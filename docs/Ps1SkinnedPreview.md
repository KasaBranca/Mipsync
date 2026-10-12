# Skinned character and FPS-arm previews

2026-10-10: Train / Thriller Part 2 and boxing arms.

The editor used `BuildPs1RigidBonePreview()` for every animated skinned
renderer in PS1 preview mode. The exporter actually uses baked vertex
animation first. These are different pipelines. Thriller Part 2 contains
1,591 triangles; the old editor preview partitioned them between bones and
reduced them to 480 triangles. Weighted joints became disconnected fragments.
The original GPU-skinned mesh renders the complete body in its bind pose and
in the `mixamo.com` animation.

Characters now retain their source skinning in the editor. Viewmodel previews
use the same `ReduceSkinnedTopologyForPs1()` function, initial controller clip,
mesh-part selection and budgets as the PS1 vertex-animation exporter. Every
cluster averages vertices **after skinning**, as the exporter does; averaging
bind-pose weights or assigning one bone to an entire face would change the
animated result. The topology stays fixed between poses.

| Arm | Original triangles | Preview vertices | Preview triangles |
| --- | ---: | ---: | ---: |
| Left | 588 | 138 | 315 |
| Right | 588 | 133 | 301 |

Preview meshes are cached per component and rebuilt when the source model,
mesh part, controller or budgets change. Pose hashes skip redundant uploads.
The renderer updates vertex buffers while retaining the index buffer; zero
skin weights and identity part transforms prevent skinning the baked pose a
second time. Imported source FBX files are preserved.

Validation:

- `tmp/CheckSkinnedPreview.cpp`: real Thriller before/after renders and animated
  pose; both real arms tested at three sample times. Each preview position
  matches the corresponding average of fully skinned source vertices within
  0.0001 model units, and indices stay identical between samples.
- `tmp/thriller-probe.log` and `tmp/thriller-preview-images/` contain results.
- `MipsyncUnlitRenderer` verifies vertex-buffer updates change actual rendered
  pixels and restore the original pose without changing topology.
- Native animation-phase and close-range viewmodel-projection tests pass.
- Train's PS1 build confirms the same 138/315 and 133/301 arm budgets.
  The original PS1 smoke check inspected only the start of the animation.
  It did not verify later poses; the follow-up below supersedes that check.
- The disc passes EDC/ECC and standard Japanese system-area checks:
  `tmp/thriller-disc-verification.json`.

Verified build:
`C:/Users/Owner/MipsyncProjects/Train/Builds/PS1/Train/out-112484-2591551156/game.cue`.

Both the engine checkout and installed `v2026.9.1/MipsyncEngine.exe` contain the
fix. The executable hashes match; the already running process retains its old
code until restart.

The running editor must be restarted to load the changed native renderer.

## PS1 character deformation follow-up

The complete 18.833-second Thriller clip exposed a separate export issue.
The spatial reducer clustered vertices using a single animated pose and no
skin-influence distinction. Hands near the head, or neighbouring legs, could
be welded into one cluster. As the joints separated, the averaged cluster
pulled unrelated surfaces together. The reduced native preview reproduced
this loss of limb separation; the full source mesh retained it.

An initial attempt separated spatial cells by dominant bone. Its numerical
tests only proved that exported points matched the reduced clusters; they did
not prove fidelity to the original character. It still averaged unrelated UV
islands and erased the head, limb thickness and joint shape. That reduction
has been replaced for character vertex animation.

Characters now use meshoptimizer v1.0 attribute-aware edge collapse. Exact
welding includes original UVs and eight sampled deformation offsets. The
simplifier returns original endpoints, so exported positions, UVs and skinning
are never averaged. Motion attributes protect joints throughout the selected
clip. Permissive seam welding and sloppy budget fallbacks are disabled. Short
Viewmodel clips retain their shared editor/export arm reducer and budgets.
Characters honour the authored animation limits (defaults: 30 requested
samples/sec, maximum 96 samples), rather than the arms' 15-sample ceiling.

Animated vertex samples also bypass static-mesh compaction and per-pose
simplification, keeping shared indices, UVs and trajectory references valid.
This is an additional invariant fix: numeric comparison of the original
Thriller export confirmed a consistent permutation across all 15 poses;
per-pose reordering was not the cause of this particular model's corruption.

Private regression fixtures are `tmp/PrepareThrillerAnimationProbe.ps1`,
`tmp/RunThrillerAnimationProbe.ps1` and `tmp/CheckSkinnedPreview.cpp`. They
capture every exported pose and halfway interpolation, including loop closure,
and compare reduced vertices to the source skinning throughout the clip.

Surface reduction results: 433 original vertices / 319 triangles, 96 baked
poses at 5 samples/sec. All 41,568 exported vertex samples exactly match the
retained original model vertices after SVECTOR quantisation, and all 433 UVs
match original texel coordinates. Arms remain 138/315 and 133/301.

Lighting also used the first frame's face normals for the entire sequence.
Character poses now retain their own precomputed face normals while sharing
indices and UVs. The runtime reads the current pose's normals without computing
face cross products each frame. Viewmodels retain their existing lighting path.

The new comparison fixture `tmp/ThrillerSurfaceProbe.cpp` renders the real
textured source and reduced mesh at twelve poses from front, back and side,
at 2.2 world units. This catches appearance loss hidden by point-consistency
tests. `MipsyncSkinnedSurfaceReduction` tests preserve separate nearby and
coincident surfaces, UV islands, deformation attributes and winding.

Evidence: `tmp/thriller-surface-animation-verification.json`,
`tmp/thriller-surface-engine/`, `tmp/thriller-surface-final-sheet.png` and
`tmp/thriller-surface-disc-verification.json`. Private emulator fixtures capture
all 96 poses and 96 halfway interpolation states, including loop closure.

All 30,624 baked face normals also match the current pose's geometry.
The close-range native comparison includes textured front/back/side views;
private PS1 Unlit captures isolate geometry/UV fidelity from the authored
directional light. No project material or scene-light values were changed.

Verified ROM:
`C:/Users/Owner/MipsyncProjects/Train/Builds/PS1/Train/out-111384-2596035546/game.cue`.
