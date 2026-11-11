# Native MIPS script export

Included in Mipsync 1.0.0, following the calendar-era 2026.10.18 release.

PS1 export lowers the shared Mips# compiler IR to generated C functions. The
PSn00bSDK GCC toolchain compiles those functions together with the engine runtime
into native MIPS code. Editor Play and Windows players retain their existing VM.

## What is in the PS1 image

`ps1_src/generated/scripts_data.c` contains a native function per script method,
direct C branches for control flow, and tables of method/function pointers.
Coroutine methods have a resume-state switch and native jump targets. They do
not fetch, decode or interpret opcodes. There are no embedded MBC blobs, startup
MBC decoding, or decoded number/name/string pools in the production target.

`runtime/native.c` and `native_ops.h` provide tagged values, fixed-point math,
component references, arrays and coroutine snapshots. Some shared ABI names
still contain `vm`/`mbc`; they do not imply an interpreter in the executable.
`runtime/vm.c` remains an independent compatibility-test oracle and is excluded
from the PS1 target's source list. The desktop compiler can still encode MBC for
those tests; production export uses generated C exclusively.

Generated code is compiled with the existing `-O3 -flto` settings. This removes
instruction-dispatch overhead and constant-pool decoding/storage. Actual speed
and code size depend on the script; no fixed FPS improvement is promised.
P-Sync's existing workload estimate is not an AOT benchmark. Measure exported games.

## PC/PS1 API coverage

The formerly PC-only host APIs now have PS1 implementations:

| API | PS1 behavior |
| --- | --- |
| Save.Set/GetInt, Float, Bool, String | Typed in-memory values; defaults/type checks follow PC behavior. |
| Save.Write(slot), Save.Read(slot) | Persistent memory-card files in slot 1 (`bu00:`). Return true/false. |
| Physics.Raycast(ox, oy, oz, dx, dy, dz, distance) | Nearest world-space render-mesh AABB, like the current desktop API. Normalizes direction; returns entity ID or 0. |
| Application.Quit() | Deferred to the end of the frame, stops drivers/audio and invokes BIOS warm boot. There is no desktop OS to exit to on PS1. |
| AudioSource.clip assignment | Accepts a project-relative path or exported AudioClip reference. Stops the old clip and selects the new one. |

Transform, Animator, AudioSource, Collider, Rigidbody and script references can be
obtained through GetComponent, including on other objects. Animator parameter
calls honor their explicit target. Input.cursorLocked preserves the requested
logical state; PS1 has no desktop mouse pointer to capture. Dynamic strings are accepted by host APIs,
including UI.SetText, input names and scene paths. String/number concatenation
uses the desktop's six-decimal numeric text format. Transient strings and arrays
are reclaimed by scanning field, local, stack and coroutine roots.

The supported lifecycle and language subset remain the same as Editor Play.
This does not add FixedUpdate, OnEnable/Disable, Stay callbacks, arbitrary custom
method calls, cross-script writes, or a general .NET runtime that PC lacked too.
The compact PS1 Animator profile and hardware rendering/audio constraints remain.

## Save example

```csharp
class Progress : MipsBehaviour {
    void Start() {
        if (Save.Read("slot1.json")) {
            Log.Info(Save.GetInt("notes", 0));
        }
    }
    void OnTriggerEnter() {
        Save.SetInt("notes", Save.GetInt("notes", 0) + 1);
        if (!Save.Write("slot1.json")) Log.Info("Could not save");
    }
}
```

A slot consumes one 8 KiB memory-card block, with a BIOS title/icon and two
CRC-checked generations. Writes replace the older generation so a partial update
preserves the previous valid save. Reads validate identity, CRC and record bounds
before replacing live values. Missing/full cards and invalid/corrupt data return
false. The implementation never formats the card, deletes other files or grows
an existing file. As with any physical card, removing it during I/O can corrupt
its filesystem; use the standard save precautions.

Names are scoped to the game/company configuration and the slot name. The
in-memory cache is transferred across scene executable loads through a checked
snapshot in BIOS kernel-allocated memory. If that transfer cannot be prepared,
scene loading is cancelled instead of discarding cached values.

The BIOS file mode and 128-byte sector alignment follow the
[BIOS file interface](https://psx-spx.consoledev.net/dev/ps1/kernelbios/file-functions/)
and [memory-card interface](https://psx-spx.consoledev.net/ps1/kernelbios/memory-card-functions/).
PC save files remain JSON; the on-card representation is a compact binary format.

## Runtime audio

When a script assigns AudioSource.clip, export catalogs WAV/MP3/VAG/MID/MIDI
assets below assets/ and scripts/, including paths selected dynamically at runtime.
Existing Inspector AudioClip references remain supported, including the initial
clip on disabled AudioSources that scripts enable later. Unknown runtime paths
log a failure and keep the old clip rather than silently selecting an invalid
sample. Read AudioSource.clip to obtain the project-relative path.

Clips are converted and packed into the disc or resident sound data at build
time; runtime assignment does not load files from the development PC. Existing
SPU budgets, clip count and streaming/polyphony limits still apply.

## Capacity and verification

Native export supports up to 32 bound modules and 32 script instances. The old
MBC constant/name counts, method-name widths and 4 KiB method-bytecode limit no
longer constrain the native output. Actual executable/RAM limits are enforced by
the target build. Runtime values retain bounded storage:

- 64 fields/locals/stack values, 4 active coroutines per instance.
- 8 live arrays per instance, 32 elements each; dynamic lengths are checked.
- 64 live generated strings, 95 UTF-8 bytes per string.
- Save: 24 entries, 47-byte keys and 95-byte string values; each slot name is
  limited to 95 bytes. Invalid capacity requests fail visibly.
- Q16.16 numeric range and the existing scene/audio/graphics budgets.

Tests compile the generated C with the real native runtime and execute the same
object-reference fixture as the interpreter oracle. They also cover arrays,
coroutine resumption, branches, operators, lifecycle, Save/clip/component APIs,
transient allocations and malformed branch rejection. Save storage tests inject
partial writes, corrupt data, missing cards and namespace mismatches.


Run the native integration pipeline from PowerShell (requires a local PSn00bSDK):

```powershell
./tests/Ps1NativePipeline.ps1 -SdkRoot <PSn00bSDK> -EmulatorPath <pcsx-redux.exe>
```

The emulator argument is optional for a compile-only check. The test creates an
isolated project and private emulator/card directory, compiles two real MIPS
scene executables, rejects interpreter/decoder symbols, then checks script
results in emulated 2 MiB PS1 memory. It exercises disabled AudioSource Inspector
references, runtime clip selection, typed saves, memory-card write/read, cached
save transfer across LoadExec, transforms, arrays and coroutine resumption.
It does not interact with existing emulator settings or memory cards.

Development validation on 2026-10-20: all 38 CTest cases passed, PSn00bSDK built
the starter and both integration scene images, and the emulated native pipeline
passed core/card/handoff/coroutine checks. Physical console testing and AOT
performance measurements are still outstanding.
