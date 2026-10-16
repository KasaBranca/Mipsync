## Mipsync 2026.10.16

Destroy scene objects from Mips# on PC and PS1.

- Call `Destroy(gameObject)` to remove the current object and its descendants.
- Call `Destroy(Physics.otherEntityId)` in `OnTriggerEnter()` to remove the entering object.
- Destruction is deferred until callbacks finish. `OnDestroy()` runs once, live script references are cleared, and removed colliders stop participating in physics.
- Added PS1 trigger enter/exit dispatch and callback context, stable destruction of static and animated scene objects, and UI/audio cleanup. PS1 trigger contacts use world-space collider bounding boxes.
- Added editor completion, a trigger snippet, CLI language reference and [usage documentation](https://github.com/KasaBranca/Mipsync/blob/v2026.10.16/docs/ObjectDestruction.md).

```csharp
class DestroyOnTrigger : MipsBehaviour {
    void OnTriggerEnter() {
        Destroy(gameObject);
    }
}
```

Enable **Is Trigger** on the object's Collider. To destroy the entering object instead, use `Destroy(Physics.otherEntityId)`. There is no delayed Destroy overload yet.

### Downloads
- **MipsyncEngine_Windows_x64.zip** — Windows x64 Editor, matching CLI, language tools, agent skill, updated PS1 templates and bundled emulator runtime.
- **manifest.json** — Hub-compatible editor update feed, preserving previous releases. Hub 2026.10.12 continues to be used.
