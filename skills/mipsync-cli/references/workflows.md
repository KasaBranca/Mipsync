# Mipsync CLI workflows

These are composable examples, not a substitute for `mipsync describe`.
Place global selectors before the command namespace.

## Inspect a live project

```powershell
mipsync instances --json
mipsync --project "D:\Games\MyGame" scene inspect --json
mipsync --project "D:\Games\MyGame" entity list --json
mipsync --project "D:\Games\MyGame" entity inspect 42 --json
```

Use the returned stable ID for later operations. Inspect `runtime.inspect` when
an authoring command reports that Play Mode is active.

## Create and verify an object

```powershell
mipsync --project "D:\Games\MyGame" describe entity.create --json
mipsync --project "D:\Games\MyGame" entity create Crate --primitive cube --x 2 --y 1 --z -3 --json
mipsync --project "D:\Games\MyGame" entity inspect Crate --json
mipsync --project "D:\Games\MyGame" scene save --json
```

If `Crate` is not unique, take the new entity ID from the create result and use
that ID for inspection and subsequent mutations.

## Build a small hierarchy with a material

```powershell
mipsync --project "D:\Games\MyGame" entity create Environment --json
mipsync --project "D:\Games\MyGame" material create assets/materials/Brick.nmat 0.7 0.2 0.1 --json
mipsync --project "D:\Games\MyGame" entity create Wall --primitive cube --parent Environment --json
mipsync --project "D:\Games\MyGame" entity transform Wall --x 4 --y 1.5 --sx 4 --sy 3 --sz 0.25 --json
mipsync --project "D:\Games\MyGame" material apply Wall assets/materials/Brick.nmat --json
mipsync --project "D:\Games\MyGame" component add Wall Collider --json
mipsync --project "D:\Games\MyGame" entity inspect Wall --json
mipsync --project "D:\Games\MyGame" scene save --json
```

Describe component and mesh commands first when the accepted type or preset is
uncertain.

## Edit and validate Mips#

1. Discover bindings related to the intended behavior:

   ```powershell
   mipsync search "move a kinematic platform" --json
   mipsync describe Physics.UseKinematicController --json
   ```

2. Edit the `.mips` file with the workspace's normal file-editing mechanism.
3. Compile it before testing:

   ```powershell
   mipsync --project "D:\Games\MyGame" language compile assets/scripts/MovingPlatform.mips --json
   ```

4. Fix every compiler diagnostic and check PS1 compatibility before entering
   Play Mode.

## Playtest without losing edit state

```powershell
mipsync --project "D:\Games\MyGame" scene save --json
mipsync --project "D:\Games\MyGame" runtime play --json
mipsync --project "D:\Games\MyGame" runtime inspect --json
```

Stop only after the requested observation or test:

```powershell
mipsync --project "D:\Games\MyGame" runtime stop --json
```

Play Mode changes are not authoring changes. Make persistent corrections after
stopping, verify them, and save the edit scene again.

## Recover from a bad mutation

```powershell
mipsync --project "D:\Games\MyGame" editor undo --json
mipsync --project "D:\Games\MyGame" entity inspect 42 --json
```

Do not save until the rollback is verified. Use `editor.redo` only when the
user still wants the reverted mutation.

## Use raw scene patching as a last resort

First obtain the canonical document and identify the exact JSON Pointer:

```powershell
mipsync --project "D:\Games\MyGame" scene get-json --json
mipsync describe scene.patch --json
```

In PowerShell, quote a minimal patch as one JSON argument:

```powershell
mipsync --project "D:\Games\MyGame" scene patch '[{"op":"replace","path":"/entities/0/active","value":false}]' --confirm true --json
```

Then inspect the scene or entity immediately. If the path selected the wrong
object, undo instead of applying a compensating patch.

## Route multiple Editors

```powershell
mipsync instances --json
mipsync --project "D:\Games\MyGame" --instance "<instance-id>" scene inspect --json
```

Never choose an instance by process order. Match the absolute project path and
use the returned instance ID when routing remains ambiguous.
