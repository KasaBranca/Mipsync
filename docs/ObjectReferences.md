# Object references and world-space distance

These APIs are available in Mipsync **2026.10.18** and newer. They run
in Editor/PC and the PS1 runtime.

```csharp
class DoorProximity : MipsBehaviour {
    public Entity player;
    public float openDistance = 2;

    void Update() {
        if (player == null) return;
        var distance = Vector3.Distance(
            transform.worldPosition, player.transform.worldPosition);
        if (distance < openDistance) transform.rotation.y = 90;
        else transform.rotation.y = 0;
    }
}
```

Attach the script to a door. Drag the player from the Hierarchy into its
**player** Inspector field. `public Transform playerTransform;` also accepts a
Hierarchy object and provides direct `playerTransform.worldPosition` access.

Other ways to obtain an object:

```csharp
var door = Scene.Find("Door");                  // exact name; first match
var other = Scene.FindById(Physics.otherEntityId);
if (other != null) {
    var pose = other.GetComponent<Transform>();
    pose.rotation.y = 90;
    Log.Info(other.name, other.id);
}
```

On PC, `Scene.Find` includes inactive objects. PS1 lookup searches its exported
runtime scene. Missing IDs/names return `null`.
IDs are scene-local; resolve again after a scene change. Destroyed references
become null. Check before reading or writing a target. Cache a reference when
possible rather than searching by name every frame.

`target.transform.position/rotation/scale` support axis reads/writes and whole
Vector3 assignment. Use `worldPosition` for world-space distance, particularly
with parented objects. The PS1 renderer keeps flattened world transforms, so its
legacy `position` remains world-space; `worldPosition` is consistent across targets.

`target.GetComponent<Health>().hp` reads a numeric or boolean script field from the target.
`GetComponent<Health>(target)` is equivalent. Script fields are read-only through
another object's component reference; arbitrary user-defined method calls remain
unsupported. PS1 component lookup supports Transform, AudioSource, and bound
script instances. PC also exposes Animator, Collider and Rigidbody references.

Vector3 construction, Add/Sub/Scale, Length, Normalize and Distance work on both
targets. PS1 uses Q16.16 numbers and saturates distances outside its numeric range.
At most 128 PS1 entities can have mutable runtime transforms. Static targets are
promoted when first written, and physics caches are refreshed for them.

Lifecycle callbacks on both targets: Awake, Start, Update, LateUpdate, OnDestroy,
OnTriggerEnter/Exit, OnCollisionEnter/Exit. Callbacks take no arguments; use
`Physics.otherEntityId` for the other object. PS1 contacts use world collider
bounding boxes (solid contacts include collision-skin tolerance). Exit is not a
destruction notification; use OnDestroy for cleanup.

FixedUpdate, OnEnable/OnDisable and Stay callbacks are not automatically dispatched.
