# Animation CLI and renderer types

MeshRenderer and SkinnedMeshRenderer have a Render Type control: Lit by default,
or Unlit. The setting survives scene save/load, duplication, component copy and
Undo. Legacy scenes default to Lit. Unlit preserves texture, material and vertex
colors while skipping vertex lighting, light matrices and normal corrections;
skinned desktop draws also skip bone normal inverse matrices. PS1 export records
the mode per drawable part. P-Sync keeps geometry/raster costs and excludes
lighting cost, including correct cache invalidation when the mode changes.

The public animation CLI provides model/clip metadata, typed component setters,
controller schema and validation, state/parameter/transition authoring, runtime
state inspection and non-destructive .nmodel region extraction. See
`skills/mipsync-cli/references/workflows.md` and `mipsync help animator.controller`.
Controller and descriptor file mutations use temporary files and atomic replace,
participate in the existing asset Undo history, and obey project path boundaries.
Extraction assigns each triangle to its strongest summed bone influence; it is
region extraction, not an animation layer/mask system. Original skeletons, clips
and fitting are retained. .nmodel descriptors are included in PC player packages
and supported by PS1 baking.

PS1 button masks were already complete. A separate animation defect made actions
bound to L1/R1 appear unresponsive: custom numeric parameters such as Punch were
ignored, and float-controlled action states were not exported. The exporter now
captures the action parameter hash/comparison/threshold and the runtime starts
the one-shot on the condition's rising edge. Clearing the parameter leaves the
one-shot running until its exit time and transition complete. Trigger APIs retain
their previous behavior. The compact PS1 profile remains idle/walk/aim plus one
action per Animator; use separate Animators for independently animated regions.

Project-owned FirstPersonController scripts without the @mipsync-builtin marker
are preserved at startup, so customized L1/R1 assignments are not overwritten.
Older marked builtins still receive bundled updates.

Regression coverage includes all 16 protocol buttons on digital/DualShock/analog
stick responses, held/down/up and a complete short tap between gameplay frames;
custom action thresholds, reset/hold/repeat and idle return for vertex and rigid
animation; command discovery/schema/search; and P-Sync Unlit work accounting.
Native CLI verification uses a copy of Train to exercise asset Undo, invalid clip
and stack rejection, left/right extraction, all boxing input cases, runtime
inspection, renderer Undo and a complete PS1 disc build. Results are retained in
`tmp/animation-qa/`.
