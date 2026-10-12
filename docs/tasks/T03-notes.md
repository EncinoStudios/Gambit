# T03 notes (implementation report details)

## Deviations from the brief

1. **`Application` is created in two phases.** A constructor cannot return a `Result`, so the constructor only stores the
   specification; `Initialize()` (called by `Run()` when needed) sets the working directory and opens the window and returns
   the error. `Run()` returns the exit code (0, or 1 when `Initialize` failed). Tools and tests that step manually call
   `Initialize()` themselves.
2. **`ApplicationSpecification::FrameLimit` and `ApplyStandardArguments`** (not in the brief) implement `--headless`,
   `--frames N` and `--hz N` once for both executables. With a frame limit the frames are synthetic (exactly one tick each,
   no waiting, window or not), so `GambitRuntime --headless --frames 10` is deterministic and instant and always reports
   "ran 10 frames, 10 simulation ticks". Without a limit the loop follows the real clock and sleeps 1 ms when a frame
   produced no tick.
3. **`WindowSpecification::Visible`** (default true) was added so that tests and tools can create hidden windows.
4. **Sampling lives in `InputDevices::Sample()`** (returns an `InputFrame`) instead of `InputFrame::Sample()`, which keeps
   `InputFrame` plain data. Devices are read through the small interface `IDeviceState` (`Input/DeviceState.h`) that `Window`
   implements; tests drive `InputDevices` with a scripted fake.
5. **Events:** one `WindowFocusEvent(bool focused)` instead of a focus / lost-focus pair; there is no `GetName()` (the
   readable text is `ToString()`).
6. **`Layer::OnImGuiRender` exists but nothing calls it yet** (the brief: "ImGui hookup comes later"; the editor layer will, T20).
7. **CTest entries were renamed**: `GambitRuntime.Smoke` / `GambitEditor.Smoke` became `.Headless` (`--headless --frames 10`,
   checks the output), `GambitRuntime.BadArguments` (must fail) and, when `xvfb-run` is installed, `.Window`.
8. **CI installs `xvfb`** and `scripts/test.py` runs the tests under `xvfb-run -a` on Linux when there is no display, so the
   window suite really runs there. Without a display and without xvfb the window tests return early and pass (they print a
   "skipped" message). `test.py` also sets a throw-away `XDG_RUNTIME_DIR` when none exists, which silences libwayland's
   "XDG_RUNTIME_DIR is invalid" line on every GLFW start.

## Design choices that deserve a second look

- **Fixed timestep (D-011).** `FixedTimestepClock` accumulates in exact integer units (nanoseconds times Hz, one tick =
  10^9 units), so any sequence of integer-nanosecond frames yields exactly floor(total * hz / 10^9) ticks (tested over a
  million random frames). A frame needing more than `MaxSimStepsPerFrame` ticks runs the maximum, keeps the partial tick and
  drops the surplus; the Application logs one warning per burst of clamped frames. Fixed updates get `TimeStep(1 / Hz, n)`
  with the tick number n starting at 1 (n = completed ticks including this one); `OnUpdate` gets the real frame time and the
  number of completed ticks. Simulation never sees frame time: a test proves identical per-tick state hashes for a steady
  60 fps run, an irregular run and a 500 fps run with the same input log and seed.
- **Event routing.** Events go to `InputDevices` (scroll, typed text, key / button latches), then to the layers from the top
  down until one marks the event handled; an unhandled `WindowClose` closes the application (so a layer can veto it, and the
  GLFW window resets its should-close flag when the event was handled). Layers may push / pop layers during a callback: the
  application iterates over a snapshot and re-checks membership.
- **Input frames (D-012).** `KeyCode` is the engine's own compact numbering (None = 0, then 120 keys, ordered like the GLFW
  list), independent of GLFW because logs store it; it is append-only and a test pins the numbers. The key set is 128 bits,
  mouse buttons 8 bits, position / delta / scroll are int32 in 1/64 units (saturating), gamepads are four slots (the first
  four GLFW joysticks) with a 15-bit button mask and six int16 axes (sticks -32767..32767, triggers 0..32767; GLFW's -1..1
  triggers are converted at the platform edge), text is at most 16 code points per tick. The binary layout is little endian
  and fixed (102 bytes without text) behind a `GBIF` header with a version; `Deserialize` rejects bad headers, truncation,
  trailing bytes, unknown key / button bits, state of disconnected pads and invalid code points.
- **Polling versus events.** Keys and buttons are polled each tick; a key pressed and released between two ticks is kept for
  one tick through the press events (GLFW's sticky-keys mode was avoided because it makes `glfwGetKey` stateful). The first
  sample and the first one after a cursor-mode change have a zero mouse delta (the position jumps when the cursor is
  captured).
- **InputActions.json.** Documented in `Input/InputActions.h`: actions `Button` / `Axis1D` / `Axis2D`; bindings `Key`,
  `MouseButton`, `MouseDelta`, `Scroll`, `GamepadButton`, `GamepadAxis` (deadzone, scale, invert, button threshold),
  `GamepadStick` (radial deadzone), `Composite1D`, `Composite2D`. An action with several bindings takes the value of the
  strongest binding (largest magnitude, first on ties) instead of summing, so WASD plus arrow keys never doubles and mouse
  look cannot be clamped. A binding without `Gamepad` reads any connected gamepad (strongest wins); with `Gamepad: n` only
  slot n. Composite diagonals are divided by sqrt(2). Unknown properties are errors (a typo such as `Deadzon` would
  otherwise be ignored silently); errors carry the JSON path ("Actions[2].Bindings[0].Key: unknown key 'Foo'"). `ToJson()`
  writes the canonical form (D-025) and `Parse(ToJson())` is the identity. Duplicate property names inside one JSON object
  are not detected (nlohmann keeps the last one).
- **Evaluation is deterministic**: integers to float by division by constants, `+ - * /` and the exactly rounded `sqrt`; no
  libm trigonometry. A test hashes the values of all action types over 500 random frames and pins the digest across
  compilers.
- **Input logs (.ginput).** Header (`GBIL`, log version, frame-layout version, project name, scene path, seed, tick rate)
  then frames, ending with an XXH64 checksum of everything before it. `Deserialize` verifies magic, then the checksum (so
  damage is reported as damage), then the content; the tests flip every byte of a log and cut it at every length. Strings are
  limited to 4096 bytes and the rate to 1..1000 Hz (`InputLog::Validate`, enforced by `Save`).
- **Window / GLFW.** `GLFWContext` reference counts `glfwInit` / `glfwTerminate` and routes GLFW errors to the core logger
  (an unavailable display is an `Error`, never an `exit`). The window has no client API (`GLFW_NO_API`); `SetVSync` only
  stores the flag for the swap chain of T07. Fullscreen uses the monitor's current mode (borderless). An exception thrown by
  an event handler is logged and dropped instead of unwinding through GLFW's C code. Joystick events are delivered to every
  window; only gamepads with a mapping and in the first four slots generate them.
- **Entry point.** `Core/EntryPoint.h` defines `main` (include it from one file per executable); `CreateApplication` may
  return null after logging, which exits with 2. Exit codes: 0 success, 1 runtime failure (no window), 2 usage error.

## Tests

`Core:` Event 7, FixedTimestepClock 8, Application 24 (headless stepping, layer order, events, clamp logging, input source,
recording / replay, frame limit, determinism), umbrella header 1. `Input:` KeyCode / GLFW key map 9, InputFrame 10, InputActions
12, InputState 15, InputLog / ReplayInputSource 10, InputDevices 10. `Platform:` Window 16 (suite `window`; real hidden
windows, synthetic GLFW callbacks, GLFW reference counting, failure without a display, Application with a window). 122 new
test cases; the suite is 291 test cases and 8 CTest smoke tests (6 in Dist, which has no editor).

## Verified

- GCC 13.3 and Clang 18.1: Debug, Release and Dist build warning-free and pass `scripts/check.py`.
- AddressSanitizer + UBSan: all tests (window suite under xvfb) and `GambitRuntime` headless / windowed are clean.
- `GambitRuntime --headless --frames 10`, `--hz 120 --frames 120`, bad arguments (exit 2) and "no display" (exit 1) behave.
- Every header compiles on its own, and `Gambit.h` alone.

## Not verified (no access here)

- Windows / macOS (GLFW code paths for Win32 / Cocoa, symbolic-link and clipboard behavior), real gamepads and real input
  devices: key, mouse, scroll and joystick callbacks are exercised with synthetic calls of the installed GLFW callbacks;
  fullscreen, high-DPI scaling and OS-generated resize / focus events are not covered.
