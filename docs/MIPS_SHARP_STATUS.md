# Mips# practical-core status

Mips# is a component scripting language for the editor and PS1 runtime. It
deliberately targets the gameplay subset of Unity C# instead of claiming full
.NET or C# compatibility.

## Supported now

- Component classes, public inspector fields, auto-properties and enums
- `if` / `else`, `while`, `for`, `break`, `continue`, and `return`
- Block-scoped locals, arithmetic, comparisons, booleans and strings
- Transform and Vector3 access, input, collision movement and raycasts
- Animator, AudioSource, scene loading, save data and application control
- The same bytecode for editor play mode and PS1 export
- Headless script validation with `MipsyncEngine --validate-mips file.mips`

## Deliberate boundaries

These are not silently accepted. They remain outside the practical core until
their runtime and PS1 implementations can match:

- General .NET libraries, reflection, exceptions, threads and dynamic loading
- Managed allocation features such as arbitrary classes, delegates and LINQ
- Coroutines, arrays/collections and custom property accessor bodies
- User-method calls with parameters and return values

Engine APIs should be added as deterministic host functions so editor and PS1
behavior stays identical.
