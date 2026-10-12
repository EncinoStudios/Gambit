# T02 + T03 review

Verdict: **APPROVE-WITH-FIXES**. Both briefs are fully covered; nothing is stubbed. The fixes below are applied in the
working tree; the open points need a decision but do not block.

## Independent verification

- **DMath vs musl.** musl 1.2.6 `src/math` (kraj/musl, VERSION 1.2.6) was compiled with GCC `-O2 -ffp-contract=off` with
  renamed symbols and compared with `Gambit::DMath` on 3 million inputs per function (random bit patterns, [-10, 10],
  all exponents, multiples of pi/2; asin/acos inside [-1, 1]; pow with small integer and moderate real exponents): zero
  mismatches for all 12 functions. The checked-in table agrees with glibc within 1 ulp (exp, log, log2, pow, fmod
  bit-identical). No libm call besides `sqrt`/`floor`/`fabs` in DMath, Random, InputState or the loop.
- **PCG32.** The reference sequences and seeded states in the tests were recomputed with an independent
  implementation of `pcg32_srandom_r`/`pcg32_random_r`: identical. Bounded draws follow `pcg32_boundedrand_r`.
- **VFS confinement.** NormalizePath, absolute-path rejection, `weakly_canonical` plus component-wise prefix test hold
  against `..`, absolute/drive paths, links leading out, and (new test) dangling links: `mkdir` on a dangling link fails
  and a write to the link's own path replaces the link, so nothing is created outside the root. Snapshot-based reads
  are thread-safe.

## Fixed

1. **Synthetic frames could run two ticks** (`Application::RunFrames`, `Run` with `--frames`). The frame time was
   1/Hz rounded up to whole nanoseconds; the excess accumulated in the clock (998 units per frame at 999 Hz, so a
   double tick after ~10^6 frames, or immediately after a real frame that left 0.999999999 of a tick). Synthetic frames
   now run exactly one tick without touching the clock (`RunSyntheticFrame` / `ExecuteFrame`). Regression test added.
2. **Interpolation alpha could be 1.0f** (`FixedTimestepClock::GetAlpha`, documented as [0, 1)): a partial tick of
   0.999999999 rounds to 1.0f. Clamped to the largest float below 1; test added.
3. **Invalid typed code points were recorded** (`InputDevices`): 0, surrogates or values above U+10FFFF from the
   platform went into the frame, and `InputFrame::Read` then rejects the recorded log, so a recording could not be
   replayed. `InputDevices` and `InputFrame::AddText` now drop them (`IsValidTextCodepoint`, shared with `Read`); tests
   added. `InputFrame::Write` asserts `TextCount` is in range.
4. **Mouse look stopped after ~33.5 million px of locked-cursor travel** (`InputDevices`): GLFW's locked cursor position
   is unbounded, and deltas were differences of the int32-saturated positions. Deltas now come from 64-bit fixed point
   positions (same rounding, so unchanged below saturation); test added.
5. **`InputActions::ToJson` could throw** (nlohmann `type_error` 316) for action names that are not UTF-8, which
   `Create` accepted (the parser cannot produce them). `ValidateActions` now requires well-formed UTF-8; tests added,
   including a multi-byte name round trip.
6. Style and hygiene: anonymous-namespace statics renamed `g_*` -> `s_*` (GLFWContext, GLFWWindow; precedent
   `Assert.cpp`); InputLog uses `FixedTimestepClock::MaxSimulationHz` instead of a second `1000`; unused `<concepts>`
   removed from `Math/Math.h`; `ReplayInputSource` asserts a non-null log; `RangeInt` documents that `min >= max` draws
   nothing; an over-long AGENTS.md line reflowed (and the DMath table mentioned next to `GB_UPDATE_GOLDEN`).

## Open points (decisions)

1. **VFS writes to a mount point.** `VFS::Write("Saves", ...)` with a writable mount at `Saves` skips that mount and
   writes a file `Saves` into an older writable mount, where the newer mount shadows it; the same for virtual
   directories leading to a mount point. Rejecting such writes with `InvalidPath` seems right.
2. **Windows path hazards in DirectoryMount** (untested here): reserved device names (`CON`, `NUL`, `COM1`, ...) and
   trailing dots/spaces are not rejected by `NormalizePath`; case-insensitivity is not enforced (documented). Rejecting
   these names everywhere would keep content portable to packs and other platforms.
3. **Permission errors read as NotFound.** `FileSystem::ReadBinary`/`ListDirectory` use `status()`, which reports an
   unknown status on `EACCES`; that becomes `NotFound`, so the VFS silently falls through to an older mount instead of
   reporting an I/O error.
4. **InputFrame validation of axes.** `Read` accepts axis value -32768 and negative triggers, which no device sample
   produces ("values no frame can hold"). Harmless (`GetAxis` clamps) but could be rejected for consistency.
5. **CI runs twice for PRs from `claude/**` branches** (push and pull_request use different concurrency groups). Keep
   (as requested) or key the group on `github.head_ref || github.ref`.

## Checks

`python3 scripts/check.py` green: GCC 13 Debug (format, build, 294 test cases, 8 smoke tests), Clang 18 Release with
`-D GB_USE_PCH=OFF`, GCC Dist. Zero warnings.
