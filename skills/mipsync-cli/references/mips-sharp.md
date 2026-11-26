# Mips# authoring guide (Unity C#-style)

Mips# is the gameplay subset of Unity C# used by Mipsync. It targets the PS1
runtime first, so keep scripts small, deterministic and allocation-free at
runtime. This guide prefers Unity-shaped code while staying inside the PS1
limits (`fields 64`, `locals 64`, `arrays 32`, `coroutines 4`).

Use this reference together with `mipsync describe <symbol> --json` and
`mipsync language reference --json`. The compiler is authoritative.

## What is supported now (language 0.7)

- Classes, public or private fields, auto-properties and enums
  (`enum` and auto-properties may omit the trailing `;`).
  `public float Speed { get; private set; }` is accepted; the accessor
  visibility is documentation only, assignments inside this class are allowed.
- `if` / `else`, `while`, `for`, `break`, `continue`, `return`.
- Ternary `condition ? a : b`.
- Compound assignment `+=`, `-=`, `*=`, `/=`, `%=`.
- Prefix and postfix `++` / `--`.
- User-defined methods declared in the same class. Calls are inlined at
  compile time; arguments, `return` and local scoping work.
- `new Vector3(x, y, z)` as an alias of `Vector3(x, y, z)`.
- `Mathf`: `Sin`, `Cos`, `Sqrt`, `Abs`, `Min`, `Max`, `Clamp`, `Clamp01`,
  `Lerp`, `MoveTowards`, `Floor`, `Ceil`, `Round`, `Sign`, `Atan2`.
  Constants: `Mathf.PI`, `Mathf.Deg2Rad`, `Mathf.Rad2Deg`.
- `Vector3`: `Add`, `Sub`, `Scale`, `Length`, `Normalize`, `Distance`,
  `zero`, `one`, `up`, `forward`, `right`.
- Runtime prefab spawn (`Scene.Prewarm`, `Scene.Spawn`, `Scene.Despawn`) on PC
  and PS1; fixed pools (`Pool.Register/Acquire/Release/Count`),
  `Entity.SetActive/IsActive` and `OnSpawn` / `OnDespawn`.
- Transform, input, physics, animator, audio, UI, scene, save and log APIs
  listed by `mipsync describe`.
- Arrays and coroutines are available within the PS1 limits.

Not available: `switch`, `foreach`, `do-while`, properties with custom accessor
bodies, delegates/events, `Rigidbody`, `Quaternion`, `Random`, `Time.time`,
`Transform.forward`. Use the documented alternatives below.

### Runtime prefab spawn

Declare a `Prefab` asset field, assign `.nprefab` in the Inspector (or
`script.fields`), and spawn it by reference:

```mips
[Header("Enemies")] public Prefab enemyPrefab;
private Entity enemy = 0;

void Start()
{
    int capacity = Scene.Prewarm(enemyPrefab, 8); // PS1 baked capacity
    Log.Info("enemy capacity=", capacity);
}

void Update()
{
    if (timer > 1.0)
    {
        timer = 0.0;
        enemy = Scene.Spawn(enemyPrefab, x, y, z);
        if (enemy == 0) Log.Info("prefab pool exhausted");
    }
}
```

- `Prefab` fields are drag-and-drop asset slots, serialize as project-relative
  paths, and appear in `runtime.fields`.
- The string overload (`Scene.Spawn("assets/prefabs/Enemy.nprefab", ...)`) is
  kept for compatibility; PS1 requires a literal string there.
- Capacity defaults to 4 when `Scene.Prewarm` is omitted; use Prewarm for more.
- The PS1 exporter reads the capacity at build time. Pass an integer literal
  or an `int` field (its scene value, else its default, is used). Any other
  expression bakes the default 4 copies and `language.compile` reports a
  `MIPS_PS1_WARNING`.
  Pools never grow automatically. PS1 returns `null` when exhausted.
- `Scene.Prewarm` is a no-op on PC.
- PS1 copies start hidden; `Scene.Spawn` activates one and moves its root.
  Spawned prefab scripts receive `OnSpawn`.
- `Scene.Despawn(entity)` deactivates the entity (use `Destroy(entity)` to
  remove it permanently).
- Prefab meshes, materials and scripts are included in PC and PS1 builds.
- Author prefabs with `prefab.create --entity <id> assets/prefabs/X.nprefab`;
  inspect with `prefab.inspect`, list with `asset.list --type prefab`.

### Fixed pools (pre-placed instances)

Pre-place the pool members in the scene while they are active, then call
`Pool.Register` once in `Start`:

```mips
private Entity[] _active;
private int _count = 0;

void Start()
{
    _active = new Entity[16];
    int capacity = Pool.Register("Road"); // matches Road, Road.1, Road.2...
    Log.Info("Road pool=", capacity);
}

void Update()
{
    if (_count < 16)
    {
        Entity road = Pool.Acquire("Road");
        if (road != 0)
        {
            road.transform.position = new Vector3(0.0, 0.0, 0.0);
            _active[_count] = road;
            _count++;
        }
    }
}
```

- The prefix matches `name == prefix` or `name` starting with `prefix + "."`.
- `Pool.Register` hides matching entities. Members must be active in the scene
  when the PS1 build is exported; registration happens at runtime.
- `Pool.Acquire` returns the first inactive member, or `null` when exhausted.
  Pools never grow automatically.
- `Pool.Release(entity)` deactivates and returns it.
- `Entity.SetActive(bool)` and `Entity.IsActive()` work on any entity.
- `OnSpawn` runs on scripts attached to the acquired entity; `OnDespawn` runs
  on release. Use them to reset state instead of `Start`.
- `runtime.pool --json` shows capacity, active/inactive and exhaustion.

### Method calls are inlined

```mips
private float ClampSpeed(float value)
{
    return Mathf.Clamp(value, -maxSpeed, maxSpeed);
}

void Update()
{
    speed = ClampSpeed(speed + acceleration * dt);
}
```

Rules:

- Declare helpers in the same class and call them by name.
- Parameters and locals are scoped to the helper; recursion is rejected.
- Calling a method does not allocate a VM frame. Keep helpers small; very
  large helpers inline into every call site and grow bytecode.
- Coroutine methods (`IEnumerator`) must still be passed to
  `StartCoroutine(Routine())`.

### Private fields and serialization

```mips
[Header("Tuning")] [Min(0)] private float maxSpeed = 6.0;
[HideInInspector] private float spawnX = 0.0;
```

- `private` / `protected` / `internal` / `public` are accepted on fields and
  methods.
- Private fields are runtime state: they are hidden from the Inspector and
  **not saved into the scene**. Initialize them in the declaration or in
  `Start()`.
- Use `[SerializeField] private float tuned;` when a private field should still
  be Inspector-visible and serialized. `[ShowInInspector]` is an alias.
- Field storage is still keyed by name. Renaming a public serialized field
  loses its saved value; migrate the scene in the same change.

## Unity-style conventions

### Fields

- Put inspector attributes on one line:
  `[Header("Movement")] [Min(0)] public float speed = 3.0;`
- Order members: tuning fields -> runtime readouts -> private state ->
  `Start` -> `Update` -> helper methods.
- Use `[Tooltip]` only when the value is not obvious from the name/range.
- Runtime readouts: `[Header("Runtime")] [ReadOnly] public float value;`
- Internal state: `[HideInInspector] private float _name = 0.0;`

### Vectors and transforms

Prefer whole-vector assignment over sub-field writes:

```mips
float x = transform.position.x + steer * speed * dt;
x = Mathf.Clamp(x, _startX - halfWidth, _startX + halfWidth);
transform.position = Vector3(x, transform.position.y, transform.position.z);
```

- Use `Vector3(...)` or `new Vector3(...)`; both compile.
- `transform.position` and `transform.rotation` are local-space.
- `transform.worldPosition` is available for world-space comparisons.

### Control flow and math

```mips
// Prefer one expression over a temporary + if/else.
speed = Mathf.Clamp(speed + acceleration * dt, -reverseSpeed, maxSpeed);
turn = speed < 0.0 ? -turn : turn;
ticks++;
```

- Use `Mathf.Clamp01`, `Mathf.Sign`, `Mathf.Lerp`, `Mathf.MoveTowards` instead
  of hand-written branches.
- Use `Mathf.Deg2Rad` / `Mathf.Rad2Deg` instead of magic constants.
- Avoid long `if` chains; if a block grows past roughly 60 lines or 20 locals,
  split the work into another MipsScript component and poll its public fields.

### Functions and components

When one script becomes a state machine, split by responsibility and let
components read each other's public fields. Mips# has no events or cross-script
method calls.

```mips
// CarInput.mips
class CarInput : MipsBehaviour
{
    [HideInInspector] public float steer = 0.0;

    void Update()
    {
        steer = Input.GetAxis("LeftStickX");
        if (Input.GetButton("DPadLeft")) steer = -1.0;
        if (Input.GetButton("DPadRight")) steer = 1.0;
    }
}
```

```mips
// CarMove.mips
class CarMove : MipsBehaviour
{
    public Entity input;

    void Update()
    {
        float steer = input.GetComponent<CarInput>().steer;
        // Move using steer...
    }
}
```

Readers should run in `LateUpdate` so they observe the latest values.

## Canonical CarController

```mips
// @mipsync-builtin v15
class CarController : MipsBehaviour
{
    [Header("Lateral Movement")] [Min(0)] public float lateralSpeed = 3.0;
    [Header("Lateral Movement")] [Min(0)] public float laneHalfWidth = 0.45;
    [Header("Runtime")] [ReadOnly] public float steer = 0.0;
    [HideInInspector] private float _startX = 0.0;

    void Start()
    {
        _startX = transform.position.x;
    }

    void Update()
    {
        steer = Input.GetAxis("LeftStickX");
        if (Input.GetButton("DPadLeft")) steer = -1.0;
        if (Input.GetButton("DPadRight")) steer = 1.0;

        float x = transform.position.x + steer * lateralSpeed * Time.deltaTime;
        x = Mathf.Clamp(x, _startX - laneHalfWidth, _startX + laneHalfWidth);
        transform.position = new Vector3(x, transform.position.y, transform.position.z);
    }
}
```

## Validation and common failures

1. Edit `.mips` with normal file editing.
2. Compile before testing:
   `mipsync --project "<p>" language compile assets/scripts/Foo.mips --json`
3. Require both `success: true` and `ps1Compatible: true`.
4. Fix diagnostics before Play Mode; then verify runtime fields.

Diagnostics:

- Unknown identifier: not a local, field, enum or built-in global.
- Unsupported call: not a built-in API and not a method declared in this class
  (check spelling, argument count and that the helper is in the same class).
- Recursive user method: inline expansion rejects direct or mutual recursion.
- `describe` may lag the compiler. Prefer `language.compile` to confirm a
  symbol, then add a one-line "why" comment if the API is unusual.

CLI selectors:

- Projection paths are relative to the result object: use
  `--fields dirty`, not `--fields result.dirty`.
- `--fields`/`--select` work before or after the command for commands without a
  `fields` parameter: `mipsync --project "<p>" scene.inspect --fields dirty --json`.
- Commands with their own `fields` parameter (for example `script.fields`) keep
  `--fields` as a command argument. Use `--select` for projection there:
  `... script.fields --entity 4 --fields '{"speed":0.0}' --select dirty --json`.

## Review checklist

- [ ] Whole-vector transform writes, not `transform.position.x = ...`.
- [ ] Inspector attributes on one line; private fields used for internal state.
- [ ] No private field is expected to be tuned per scene.
- [ ] `Mathf` helpers used instead of hand-written branches.
- [ ] Helper methods are small, same-class and non-recursive.
- [ ] `Update` stays under ~60 lines / 20 locals, or is split into components.
- [ ] Input strings are spelled exactly (`DPadLeft`, `Cross`, ...).
- [ ] `language.compile --json` reports `success` and `ps1Compatible`.
