# Destroying objects from Mips#

Available in Mipsync **2026.10.16** and newer, on PC and PS1.

Attach this script to the object you want to disappear. Add a Collider and enable
**Is Trigger**. The entering object must also have a Collider and a moving physics
body or character controller; two static colliders do not generate PC contacts.

```csharp
class DestroyOnTrigger : MipsBehaviour {
    void OnTriggerEnter() {
        Destroy(gameObject);
    }
}
```

To destroy the **entering object**, replace the call with:

```csharp
Destroy(Physics.otherEntityId);
```

`OnTriggerEnter()` takes no parameters. `Physics.otherEntityId` is the other
object's numeric ID during a physics callback; it is not a Unity Collider.
`Destroy` accepts either `gameObject`, an Entity reference, or a numeric entity ID.
IDs belong to the current scene and should not be reused across scene loads.

Destruction is deferred until the current callback batch finishes. Statements
after `Destroy(...)` in that invocation still execute. Further normal callbacks
and coroutines on pending objects are skipped. The object and its descendants
are removed from the runtime scene, rendering and physics; `OnDestroy()` runs
once per attached script before removal. `OnDestroy()` may queue other objects
for destruction. Stopping Editor Play restores the original edit scene.

`Destroy(null)`, `Destroy(0)`, missing IDs and duplicate requests are harmless.
There is no `Destroy(object, delay)` overload yet. For a delay, use a coroutine
with `yield return new WaitForSeconds(...)`, followed by `Destroy(gameObject)`.

PS1 trigger contacts use the runtime's world-space collider bounding boxes.
`OnTriggerEnter`, `OnTriggerExit` and `OnDestroy` are supported. Solid-collision
callbacks (`OnCollisionEnter`/`OnCollisionExit`) remain unsupported on PS1 and
are rejected at export.
