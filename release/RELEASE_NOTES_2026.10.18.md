## Mipsync 2026.10.18

Build interactive doors, pickups and first-person scenes with object references on PC and PS1.

- Assign `Entity` and `Transform` references directly in the Inspector, or find objects with `Scene.Find(name)` and `Scene.FindById(id)`.
- Read and update another object's transform, measure world-space distance with `Vector3.Distance`, and look up components with `target.GetComponent<T>()`.
- Read numeric and boolean fields from another object's script component. Destroyed object references become null.
- PS1 now dispatches `OnCollisionEnter()` and `OnCollisionExit()`, alongside trigger callbacks. Callbacks use `Physics.otherEntityId`; PS1 contacts use world-space collider bounding boxes with collision-skin tolerance for solid contacts.
- Updated editor completion, CLI language reference and bundled PS1 templates.

```csharp
class DoorProximity : MipsBehaviour {
    public Entity player;
    void Update() {
        if (player == null) return;
        var distance = Vector3.Distance(
            transform.worldPosition, player.transform.worldPosition);
        if (distance < 2) transform.rotation.y = 90;
        else transform.rotation.y = 0;
    }
}
```

Assign the player in the Inspector. [Object references, distance and lifecycle documentation](https://github.com/KasaBranca/Mipsync/blob/v2026.10.18/docs/ObjectReferences.md) includes platform support and current limits. [Destroy and trigger examples](https://github.com/KasaBranca/Mipsync/blob/v2026.10.18/docs/ObjectDestruction.md).

### Downloads

- **MipsyncEngine_Windows_x64.zip** — Windows x64 Editor, matching CLI, language tools, agent skill, PS1 templates and bundled emulator runtime.
- **manifest.json** — Hub-compatible editor update feed, preserving previous releases. Hub 2026.10.12 continues to be used.

### Validation

36 automated tests pass, including the production PS1 VM object API tests. Editor language tooling tests and PS1 SDK export/build checks also pass.
