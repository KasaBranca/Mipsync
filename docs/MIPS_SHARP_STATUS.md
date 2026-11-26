# Mips# practical-core status

Mips# is a component scripting language for the editor and PS1 runtime. It
deliberately targets the gameplay subset of Unity C# instead of claiming full
.NET or C# compatibility.

## Supported now (language 0.7)

- Component classes, public/private inspector fields, auto-properties and enums
- `if` / `else`, `while`, `for`, `break`, `continue`, and `return`
- Ternary `condition ? a : b`
- Compound assignment (`+=`, `-=`, `*=`, `/=`, `%=`) and `++` / `--`
- User-defined methods in the same class; calls are inlined at compile time
  (no recursion, no function pointers)
- Block-scoped locals, arithmetic, comparisons, booleans and strings
- `new Vector3(...)` alias, enum declarations with an optional trailing `;`
- Runtime prefab spawn (`Scene.Prewarm`, `Scene.Spawn`, `Scene.Despawn`):
  PC instantiates `.nprefab` assets; PS1 bakes fixed copies
- Fixed runtime pools (`Pool.Register/Acquire/Release/Count`), `Entity.SetActive`
  / `IsActive` and `OnSpawn` / `OnDespawn` lifecycle
- Transform and Vector3 access, input, collision movement and raycasts
- Animator, AudioSource, scene loading, save data and application control
- `Mathf` including `Abs`, `Min`, `Max`, `Clamp`, `Clamp01`, `Lerp`,
  `MoveTowards`, `Sign`, `Atan2`, `Deg2Rad`, `Rad2Deg`, `PI`
- The same bytecode for editor play mode and PS1 export
- Headless script validation with `MipsyncEngine --validate-mips file.mips`

Private fields are runtime state: they are hidden from the Inspector and are
not serialized into scenes. Keep per-scene tuning values public.

## Deliberate boundaries

These are not silently accepted. They remain outside the practical core until
their runtime and PS1 implementations can match:

- General .NET libraries, reflection, exceptions, threads and dynamic loading
- Managed allocation features such as arbitrary classes, delegates and LINQ
- `switch`, `foreach`, `do-while`, custom property accessor bodies
- `Rigidbody`, `Quaternion`, `Random`, `Time.time`
- Unity-style `Instantiate`/`Destroy` overloads; use `Scene.Spawn` /
  `Scene.Despawn` / `Destroy(entity)`
- Cross-script method calls and events; poll public fields instead

Engine APIs should be added as deterministic host functions so editor and PS1
behavior stays identical.
