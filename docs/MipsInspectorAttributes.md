# Mips# Inspector attributes

Attributes go before a public field or auto-property, as in a Unity-style component script:

```mips
class PlayerController : MipsBehaviour
{
    [Header("Movement")]
    [Tooltip("Speed in world units per second.")]
    [Range(0, 10)]
    public float speed = 3;

    [Space(12), Header("Camera")]
    [InspectorName("Follow Camera")]
    public Camera camera;

    [Header("Runtime"), Order(100), ReadOnly]
    public float verticalVelocity = 0;

    [HideInInspector]
    public int inputState = 0;
}
```

| Attribute | Inspector behavior |
| --- | --- |
| `Header("Title")` | Section heading before the field. |
| `Space` or `Space(12)` | Vertical spacing, default 8 pixels; follows editor font scaling. |
| `Tooltip("Help")` | Help on the label or control, including disabled fields. |
| `Range(min, max)` | Slider for an `int` or `float`; clamps Inspector edits. Integer bounds round inward. |
| `Min(value)` | Minimum for Inspector edits to an `int` or `float`. |
| `ReadOnly` | Visible but disabled in the Inspector; script assignments still work. |
| `HideInInspector` | Hidden while remaining public, saved, and accessible to scripts. |
| `InspectorName("Label")` | Display label; retains the original field name and saved values. |
| `Order(integer)` | Ascending display order, default 0; equal values retain declaration order. |

Arguments must be string or signed numeric literals. Attributes can share a bracket or use separate brackets; the `Attribute` suffix is also accepted. Duplicate attributes, unknown names, incorrect argument types, invalid bounds, and attributes on methods produce compiler diagnostics. `Space` accepts 0–1000 pixels. `Range` and `Min` require a representable numeric interval; `Min` can narrow a `Range`.

A Vector3 field is one Inspector row: its heading, spacing and ordering apply to the entire vector. Reference, boolean and array fields support the display attributes; `Range` and `Min` only support scalar numeric fields.

These attributes describe the Inspector. They do not enforce gameplay invariants, clamp script assignments, or alter default/saved values during loading. Field indices and executable bytecode retain declaration order. PS1 exports contain no attribute metadata or extra instructions. Desktop compiled modules retain the metadata and remain able to load the previous six-element field format.

The Mips# VS Code extension supplies attribute highlighting, completion and hover help. Compiler diagnostics use the same compiler as the editor.
