# PS1 controller input

Game scripts use the same controller API on PS1, in Game View, and in PC players:

```csharp
float horizontal = Input.GetAxis("LeftStickX");
float forward = Input.GetAxis("LeftStickY");
float yaw = Input.GetAxis("RightStickX");
float pitch = Input.GetAxis("RightStickY");
if (Input.GetButtonDown("Cross")) { /* jump / confirm */ }
```

Axes range from -1 to 1. Right and up are positive. The D-pad also drives the
left stick axes. `Horizontal` and `Vertical` alias those axes. The right stick
is available on analog controllers; a digital pad has no right stick input.
The shipped FPS controller also turns with L1/R1 and looks up/down with
Triangle/Circle, so a digital PS1 pad can operate it.

`GetButton`, `GetButtonDown`, and `GetButtonUp` accept Cross, Circle, Square,
Triangle, Start, Select, L1, R1, L2, R2, L3, R3, DPadUp, DPadDown, DPadLeft,
and DPadRight. They do not accept PC key names. Existing semantic aliases
(Jump, Run, Aim, StrafeLeft, StrafeRight, QuickTurn) remain compatible.
The legacy keyboard API remains available for existing scripts.

Preferences > PS1 Controller configures the keyboard and mouse independently
of game code. Defaults: WASD for the left stick, arrows for D-pad, mouse
movement for right stick, Space for Cross, C for Circle, Shift for Square, F for
Triangle, Enter for Start, Tab for Select, Q/E for L1/R1, right/left mouse for
L2/R2, Control/middle mouse for L3/R3. Controls can be unbound with None.
Settings persist between sessions; category reset restores defaults.

The four Right Stick direction bindings default to Mouse Up, Mouse Down,
Mouse Left, and Mouse Right. Each can be reassigned individually to a key,
mouse button, mouse direction, or None; the displayed bindings drive input.
Existing I/K/J/L defaults migrate to mouse directions while custom keys are kept.
Mouse movement is converted to stick deflection using pixels per second,
then limited as a two-axis vector so strong diagonal motion retains its direction.
FPS look also limits the combined stick/button vector to avoid faster diagonal turns.
Sensitivity, direction bindings, axis inversion and physical stick deadzone
are configurable. Multiply look
axes by angular speed and `Time.deltaTime`; do not use a mouse delta in game
code. Mapped input is active while Game View owns focus or captures the mouse.
Escape releases mouse capture. OS focus loss clears pending input and avoids
a mouse jump on refocus. Input in a PC player does not require an editor panel.
The editor captures the mouse only after clicking Game View; Play and FPS Start
requests alone do not hide it. Escape releases capture until the next click.
`mipsync runtime inspect --json` reports input enablement, cursor capture, and
both stick axes for diagnosing Play Mode input.

Press and release edges are buffered between P-Sync simulation ticks and handed
to scripts and UI once. A brief tap is retained even if no game tick occurred
while the button was held. PS1 uses the corresponding VBlank edge buffer.
An actual gamepad can be used alongside the virtual keyboard/mouse controller.

The shipped FirstPersonController, RadioController and SilentHillController
use controller input. FPS look is measured in degrees per
second; partial stick deflection retains partial movement speed.

New FPS controllers consist of a body with the movement script, capsule and
kinematic rigidbody, plus a child `Camera` at eye height (1.7 units). The body
pivot is at the feet. Horizontal look turns the body; vertical look changes
the child camera without tilting the collider. The script's `View Camera`
reference is filled automatically. `Camera.RotatePitch(camera, deltaDegrees,
minPitch, maxPitch)` returns the clamped pitch and supports the editor and PS1.
Existing controllers with a camera on the body remain supported with `View
Camera` set to None. Their collider offsets and tuned settings are preserved.

Validation covers binding persistence, disabled input, short taps, UI handoff,
axis signs/clamping/deadzone, mouse inversion and cadence, PS1 protocol names,
real Mips# compilation/PS1 compatibility, and MIPS compilation of the runtime.
