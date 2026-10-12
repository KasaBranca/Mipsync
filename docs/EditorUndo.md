# Editor Undo / Redo

Ctrl+Z undoes edits; Ctrl+Y or Ctrl+Shift+Z redoes them. Scene and asset edits
share one chronological history, limited by Preferences > General.

The editor captures a scene baseline before controls run. This includes
reference replacement, component values, component add/remove/reorder,
hierarchy order and parenting, transforms, UI, terrain and modeler geometry.
Continuous gestures share one step, including the release frame. Failed
commands and unchanged clicks do not clear Redo or create entries.

File snapshots are registered before asset mutations. Coverage includes:

- Material textures, colors, tiling and offsets, and Save Material.
- Animator states, transitions, parameters, Entry/Any State positions and layout.
- Animation keys, recording, FPS/name changes, and generated controllers.
- Asset/folder creation, import, deletion, rename and move; embedded FBX textures.
- Prefab creation from Hierarchy or drag/drop into Project.
- Project build settings and the scene build list.

Undo restores the bytes and existence of affected files, including binary data,
empty folders and newly created parent folders. It reloads scene/asset caches,
inspectors, timeline and build settings. Unrelated files are not recursively
deleted during restoration. A failed restoration keeps history and attempts
to roll back its partial file changes before reporting the error.

CLI and Editor Console mutations are committed individually. Navigation,
selection, animation preview playback/scrubbing, preferences, audio preview,
Play Mode simulation and generated build output are not document edits.
Opening another scene starts its own history. External IDE/file-manager edits
are handled by those applications rather than the editor's transaction.

Game View rendering respects each Canvas reference resolution instead of
overwriting it with the preview framebuffer size.

Regression tests cover scene baselines, no-op frames, grouped drags, reused
gesture IDs, Redo branching, material byte restoration, interleaved scene/file
changes, binary Unicode folder moves, deletion, parent creation and restoration
failure with unrelated files, plus preview exclusion.
