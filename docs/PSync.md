# P-Sync

P-Sync is an editor Game View performance preview. It is on by default and is
saved with the editor's Game View preferences. It controls script Update and
LateUpdate, animation, physics and Game View framebuffer presentation together.
The editor itself continues running normally. Pausing, stepping and disabling
P-Sync discard the pending timer; buffered key presses, mouse movement and wheel
movement are consumed once by the next game update.

## What the prediction means

This is a workload model, **not a cycle-accurate emulator or a measured hardware
frame rate**. Hardware clocks and the frame-period calculation are derived from
documented timings. Software CPU costs are explicit instruction budgets and have
been calibrated for the current prop renderer and grading pass against an
executed PS1 build at native clocks in DuckStation. Cache misses, branches,
memory contention and export optimizations can change the result. Do not treat
the displayed decimal as a promise of equivalent performance on a real console.
Validate release performance using the exported PS1 executable.

The current export in `templates/ps1/starter/main.c` sets NTSC, 320 x 240,
non-interlaced. P-Sync uses that target rather than the 1024 x 768 PC framebuffer.

* CPU: 33,868,800 cycles/s.
* Video clock: 53,693,175 cycles/s, average 3,412.5 clocks/line, 263 lines/frame.
* Refresh: `53,693,175 / (3,412.5 * 263)` = approximately 59.826 Hz.
* GTE projection: RTPT = 23 CPU cycles for three vertices; RTPS = 15 for one.
* GPU/OTC DMA: one word per CPU cycle, with a 17/16 allowance for DRAM rows.

Sources: [PSX-SPX video timing](https://psx-spx.consoledev.net/ps1/gpu/timings/),
[GTE timing research](https://psx-spx.consoledev.net/gtepipelinetimings/),
[DMA transfer rates](https://psx-spx.consoledev.net/dmachannels/), and Sony's
[Psy-Q Run-Time Library Overview 4.7](https://psx.arthus.net/sdk/Psy-Q/DOCS/LibOver47.pdf),
chapter 8, especially printed pages 8-28 and 8-29.

## Workload and arithmetic

Only Game View rendering is sampled. Scene View, gizmos, selection outlines and
wireframe overlay passes do not add to the prediction. Static export preview
meshes and rigid bone preview meshes already used by Game View supply geometry.
Actual bone matrices produce the animated projected positions.

Authored color grading is applied on PS1 after the lit and fogged world image,
before overlay UI, using an exported RGB555 lookup table. Sixteen scanlines at a
time are read back, transformed and uploaded (10 KiB staging, 64 KiB LUT only in
graded scenes). The CPU transforms two pixels per word, reuses results for
repeated pairs and performs one lookup when both pixels have the same color.
All RGB555 colors and mask bits retain the previous result. Identity grading
(exposure 0, contrast/saturation/filter 1) omits the LUT and framebuffer pass;
P-Sync also excludes that work. P-Sync budgets 10 CPU cycles and 3.4 transfer
cycles per pixel, calibrated against the optimized pass below.
Readback/upload has GPU readiness waits in addition to linked-list DMA bandwidth.
The graded presentation path submits the current world's OT after building it
and waits for completion before grading: world CPU, world GPU and grading are
serialized. The ungraded asynchronous path retains CPU/GPU overlap.

Triangles are clipped in homogeneous space to the camera frustum and drawing
area. Their projected area at 320 x 240 determines raster coverage. Backfaces
are excluded for single-sided meshes; double-sided character meshes retain
both sides. There is deliberately no depth-buffer rejection of hidden geometry:
PS1 rendering still pays for overdraw. UI images and glyphs add coverage too.
Subpixel coverage uses polygon area, not an exact hardware rasterizer.

The GPU estimate includes the framebuffer clear, background, shaded writes,
texture reads, transparent destination reads, and packet DMA words. The texture
miss budget follows the access model in Sony's documentation: one read per
16-bit VRAM word (four 4-bit texels, two 8-bit texels or one 16-bit texel). Mesh
textures exported by the current TIM writer are 16-bit. This uses the
conservative cache-miss side of the documented range; a texture-cache hit can be
faster. ASCII overlay text already uses the PS1 bitmap font in the PC renderer;
its individual flat tiles are sampled, with one TILE packet per pixel block.
Non-ASCII/world-space preview glyphs use an approximation from the PC font's
quad coverage (4-bit texture budget). Their exported bitmap-tile count can differ
substantially. Texture enlargement, transparency mask rejection and exact GPU
setup/scanline costs are not simulated.

Projected vertices and lighting are retained in a bounded cache model (64
draw entries, 16384 vertices total, at most 4096 vertices per mesh). Mesh instances at different world transforms
have distinct entries. Camera changes invalidate projection, while world
transform, light and material changes invalidate lighting. Flat props shade
every submitted face; smooth meshes shade vertices. Both include faces outside
the drawing area before culling. Uncached primitives and skinned paths pay
each frame. Plane coverage uses the runtime's independent repeat/subdivision
axis cells (at most 32 per axis) and four fresh lighting samples per tile,
rather than the desktop's shared 10x10 primitive mesh.
This approximates the export renderer's bounded static
cache; material splits, duplicate vertices and quad merging can differ from the
exporter and affect packet and CPU costs.

Primitive floors retain full 64-texel texture detail for tiling up to eight
repeats per axis; denser materials use the compact eight-repeat atlas.
Their grid follows material repeat boundaries, offsets and negative tiling,
then refines long spans for projection precision. P-Sync uses the same shared
cell builder and positions as the PS1 runtime. Near-plane crossing floor
triangles retain their visible portion, including interpolated UVs and lighting,
and are clipped to the GPU's safe polygon span before submission.

The VM counts actually executed bytecode instructions and host calls, including
LateUpdate, physics event handlers and UI callbacks. It does not infer loop costs
from source size. `EstimatePSync` applies separate budgets to arithmetic,
division, component/member access, arrays and host calls. Physics host calls
scale with enabled colliders and mesh-collider triangles. Scene traversal,
packet building, GTE register work, vertex lighting, animated bones, dynamic
bodies and audio-source updates also contribute. These software budgets are
approximations, **not measured instruction traces of the compiled MIPS binary**.
PS1-incompatible scripts, skinned export modes, runtime packet/memory limits,
CD streaming, loading, save I/O, MIDI event density and exact contact patterns
cannot be predicted accurately by this model.

### Calibration, 2026-10-06

The ReviewDemo Game export was instrumented in a separate test build using
timer 1's HBlank source. Its automatic title-to-game transition exercised the
real executable loader. No profiling code or demo edits are shipped.
Before grading optimization, at NTSC/native CPU clocks, stable 30-frame samples measured approximately
49,550 HBlanks in render preparation, 700 in logic and 36,430 in presentation
(the latter includes VSync waiting). This is about 11 fields/frame, 5.5 FPS.
The grading pass alone measured 103–105 HBlanks world GPU, 795–807 lookup,
and approximately 110–120 combined readback/upload: about 65 ms.
One rotating barrel mesh measured ~1,080 HBlanks cold versus ~352 cached;
the brick mesh ~310 versus ~108. The former 100/110-cycle triangle and
30+42/light vertex budgets omitted most compiled software work.

Current CPU budgets: 1,200/entity, 650/submitted triangle, 1,000/visible
packet, 160/projected vertex plus GTE command latency. Lighting samples use
900 setup/normal/transform cycles plus 150/directional light,
1,500/point or spot light, another 350/spot cone, and 400/flat-face setup.
With no scene lights the lighting setup budget is 80 cycles. These are
aggregate calibrated budgets, not exact instruction counts for every branch.
VM/physics/animation budgets remain approximations. This calibration does
not impose a fixed FPS cap and does not claim hardware validation for all scenes.
`mipsync runtime inspect` exposes sampled geometry, face/vertex lighting,
light types, grading pixels and CPU/GPU/DMA estimates for reproducing errors.
The editor with the original grading pass measured 11 fields (5.44 FPS) with grading and
7 fields (8.55 FPS) without; the UI-only title remained one field (59.83 FPS).
Polling the spinning object's transform observed 21 script-driven pose changes
over 4.02 seconds, verifying that simulation follows the predicted cadence.

The optimized grading pass was compared with that original pass in the same
exported scene at native CPU clocks. Lookup time fell from 795–808 to
329–357 HBlanks; combined readback/upload fell to 67–81 HBlanks after doubling
the transfer strip height. Excluding world rendering, grading took about
26 ms instead of 58 ms, roughly 55% less time. The staging buffer uses 5 KiB
more RAM. Full-frame measurements dropped from approximately 11 fields to
9 fields (about 6.65 FPS); this is an emulator measurement, not a physical-console
benchmark, and the improvement varies with scene colors and texture detail.
World rendering remains a separate cost. The calibrated P-Sync regression
accepts 8–10 fields with grading and 6–8 without for this workload.

The desktop renderer precomputes exposure, contrast, saturation and filter as
one affine color transform per draw instead of evaluating those separate
operations in each fragment. Its clamp and position in the shading pipeline
are preserved. RGB555 and affine-transform regression tests cover every color;
the PS1 tests also cover mask bits, odd counts and half-word alignment.

`PresentFrame` builds the next buffer while the GPU draws the preceding one.
It waits at DrawSync and then presents at VSync. Thus:

```
workSeconds = gradingEnabled
    ? cpuSeconds + dmaSeconds + gpuSeconds + gradingSeconds
    : max(cpuSeconds + dmaSeconds, gpuSeconds)
vblanks = max(1, ceil(workSeconds * refreshHz))
frameSeconds = vblanks / refreshHz
predictedFPS = refreshHz / vblanks
```

One missed VBlank changes approximately 59.8 FPS to 29.9 FPS; further missed
periods yield 19.9, 15.0 FPS, etc. A completed sample sets the next game-frame
budget. A heavier sample increases the VBlank count immediately. A decrease
requires half a second of continuously lighter samples, and uses the slowest
requirement observed during that recovery window. This conservative cadence
policy prevents borderline three/four-field samples from repeatedly changing
both Update timing and the displayed estimate. It applies to execution and
display together; CPU/GPU/DMA diagnostics retain the latest raw workload costs.
It resets on play/stop and P-Sync toggles, and does not force all scenes to 15 FPS.
This half-second recovery policy is an editor stabilization choice, not a PS1
hardware rule or a claim of calibrated prediction accuracy.
The pacing accumulator uses a double-precision monotonic clock and
retains fractional periods, avoiding integer-millisecond rounding and drift.
There is no sleep in the editor thread. A PC slower than the predicted cadence
cannot display frames it did not render. During P-Sync, the only FPS shown is
`PS1 estimate`, which supplies the game update and framebuffer presentation
interval. Editor FPS, editor frame time and measured game FPS are omitted;
there is no separate measured-FPS counter. Excess PC delay is consumed once rather than
replaying input or blocking the editor with a backlog of updates.

Presentation time and simulation time are distinct. The exported PS1 loop uses
`runtime/frame_timing.h` to calculate `Time.deltaTime`: elapsed VBlanks clamped
to 1 through 4, divided by nominal 60 Hz and truncated to Q16.16. P-Sync uses
the same helper for Update, coroutines, animation, physics and LateUpdate.
The first update receives one field, matching PS1 startup; a manual step uses
the predicted frame's field count. Physics accepts the whole logic delta in
P-Sync instead of applying the desktop's separate 50 ms limit. Long PC stalls
consume the wall-clock backlog once but advance at most four logic fields,
matching the export's cap. Frames slower than four fields therefore slow game
time, as they do on PS1. Lower FPS by itself does not reduce movement speed
when scripts multiply velocity by `Time.deltaTime`. PC mouse deltas and PS1
analog-stick input, numeric precision and collision engines still differ.

`MipsyncPSyncTests` covers clipping, overdraw, texture access, backface rules,
CPU-bound and GPU-bound estimates, CPU/GPU overlap, refresh quantization,
120/144/240/1000 Hz editor pacing, shared C fixed-point timing, stalled-frame
logic caps, alternating borderline workloads, sustained cadence recovery,
timer reset and accumulated input consumption.
These tests verify the implementation and arithmetic; they do not establish
prediction accuracy against PS1 hardware.

`MipsyncPSyncInputTests` exercises the real input backend without creating a
window, including the handoff from script update to UI callbacks and focus loss.
The existing Mips runtime regression command also verifies executed opcode and
host-call counts, counter reset, disabled profiling and actual character movement
with the P-Sync physics delta.
