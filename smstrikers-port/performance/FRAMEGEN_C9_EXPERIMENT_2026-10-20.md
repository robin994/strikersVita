# Strikers Vita C9 — experimental temporal frame generation (2026-10-20)

## Implementation status

**SOURCE IMPLEMENTED, HOST-TESTED; NOT BUILT OR MEASURED ON RETAIL VITA.**

Development base: reconstructed hardware C9, **27.114365 real FPS** in the
previous 300-frame benchmark (logical play frames 600–899), matching the
previous C9 quiet range. This number is a **before** measurement, not an
interpolation result or an expected future frame rate.

The patch is isolated from the original local `main` checkout, its staged
243 C20 changes, the installed C9 SELF, C20 fallback and retail saves. It
does **not** reintroduce the C14/C20 fog shader, queue optimizations or
other changes associated with the 17 FPS regression.

## What the experiment actually does

In the VitaGL **GXM display-queue callback**, after a new scene's GPU work has
completed, the feature can create one intermediate 960×544 RGBA image by
rounding the average of the *previous* and *current* completed scanout images.
The intermediate image is displayed for one 60 Hz vblank, followed by the
real image for the next vblank. A NEON half-add kernel runs on Vita; the
same exact rounding is implemented in the host scalar fallback.

- **No shader compilation, render-pass duplication or glReadPixels.**
  The input is a completed GXM scanout surface, already accessible through
  the display callback.
- The previous image lives in **private CPU memory**, not a previously
  submitted GPU color buffer that the producer may already be rewriting.
  The generated midpoint has its own mapped, 4096-aligned CDRAM scanout buffer.
- Screen storage: roughly **2.09 MiB CPU RAM + 3 MiB CDRAM** beyond the
  ordinary C9 display surfaces. Allocation is attempted **only** with
  `vita_framegen=1` at renderer startup, and is rejected if less than the
  configured VRAM guard remains; missing memory fails closed.
- Two successive suitable gameplay frames, 25–50 ms apart, are required.
  Real frames faster than the interpolation window, long stalls, menus,
  loading/bitmap screens, black transitions and invalid/unsupported scanout
  modes never generate an intermediate image.
- A sparse 64-pixel RGB scene-cut guard skips almost-global visual cuts.
- If one blend exceeds **8 ms**, that attempt is skipped instead of adding a
  vblank; after three consecutive slow attempts, interpolation is disabled
  for the session. Buffer allocation errors are similarly fail-closed.
- All generated frames are display-only. The simulation, input polling,
  world state and audio remain driven by original game frames. An extra
  vblank can add roughly **16.7 ms of presentation latency**, plus CPU
  computation; doubled displayed-image count is **not** doubled gameplay FPS.

This is **temporal blending, not true optical-flow/motion-vector generation**.
Fast-moving players, the ball and HUD may ghost even when hard cuts are
filtered. 27 real frames could theoretically supply up to about 54 different
scanout images/s if every suitable pair is accepted, but **no such rate
has yet been observed on Vita**. The additional CPU work may lower real FPS.

## Activation

The experiment is OFF by default, and it does not rewrite the validated Vita
INI block. The existing config reader already maps `key=value` to
`STRIKERS_KEY`, so an opt-in user can append to
`ux0:/data/strikersVita/strikers.ini` after deploying a build containing this
branch:

```ini
vita_framegen = 1
vita_framegen_trace = 1
```

`vita_framegen_trace=1` emits bounded **aggregate counters every 240
rendered frames**, never logs each scanout frame:
`real`, `intermediate`, `cadence_skip`, `cut_skip`,
`budget_skip`, `max_blend_us`, `alloc_failed`.
For quiet performance comparisons leave trace OFF.

Turning the option back to `0` on the next launch returns to C9's normal
single-frame display callback without additional surface allocation.

## Source changes

- `extern/vitaGL/source/framegen_temporal.h` — platform-independent
  midpoint and scene-cut kernels, cadence predicate; ARM NEON fast path.
- `extern/vitaGL/source/gxm.c` — opt-in buffer ownership, callback-only
  scanout interpolation, budget, stats, reset and resolution fallback.
- `extern/vitaGL/source/shared.h` — per-queue real-gameplay eligibility bit.
- `extern/vitaGL/source/vitaGL.h` — public opt-in/stats declarations.
- `src/platform/vitagl_renderer.cpp` — read config, mark eligible *real*
  gameplay frames only, optional aggregate tracing.
- `src/platform/benchmark.c` + `include/port/benchmark.h` — cheap
  relaxed-atomic live-play state getter instead of expensive rolling
  percentile calculation inside the render thread.
- `tests/framegen_temporal_test.c`, `tests/vitagl_renderer_driver_test.cpp`,
  `tools/test_temporal_framegen.py` — host deterministic kernels,
  lifecycle and eligibility tests under ASan/UBSan.
- `strikers-vitagl.ini.example` — example opt-in settings.

No new GL shader programs, frame-capture readbacks, texture uploads or
simulation steps were added.

## Completed verification

- Baseline reconstructed C9 original source receipt: all **83/83** historical
  SHA pins verified before the new experiment was staged.
- Host temporal interpolation tests: 8 alignment offsets, all byte lengths
  0–1023, 8 full 960×544 RGBA buffers, hard-cut detection and cadence
  eligibility thresholds. **PASS** under Clang ASan/UBSan both native ARM NEON
  and forced scalar fallback.
- Existing VitaGL renderer driver suite, with extra default-OFF, gameplay
  eligibility, black-frame and discard-gate cases: **PASS** with ASan/UBSan.
- SDK `arm-vita-eabi-gcc/g++ -fsyntax-only` for native
  `gxm.c`, `vitagl_renderer.cpp`, and `benchmark.c`: **PASS**. This
  does not produce ELF, SELF, VPK or run on retail hardware.
- Main checkout and installed C9 build unchanged. The current shared CMake
  cache still targets the original isolated C9 source directory, **not**
  this unbuilt experimental source.

## Required next controlled Vita test (NOT run)

1. With a fresh explicit hardware-build authorization, preserve the exact
   currently installed C9 and C20 rollbacks and use the existing **single**
   Vita build directory. Do **not** alter the staged C20 main checkout or
   original retail saves.
2. Run three consecutive 300-play-frame quiet captures with framegen OFF at
   the old clocks **444/222/222/166 MHz**, `fixed_dt=0`, frames **600–899**;
   then three matched runs with framegen ON and telemetry OFF. Measure
   mean/p95/p99 real frame timings, 30/60 FPS budgets and audio underruns.
3. Repeat with telemetry ON for a **separate diagnostic run**, check whether
   `intermediate` counts approach the eligible real-frame count, actual
   blend microseconds, disabled/memory skips and queue backpressure.
4. Verify the generated physical scanout cadence (external 120/240fps camera
   or equivalent independent observation), image correctness at high-motion
   and HUD edges, **input-to-photon latency**, goals/replays/menu switches,
   prolonged play, suspend/resume and readback safety.
5. Abort/restore the original installed C9 if real FPS or p95 regresses
   materially, input latency is objectionable, ghosting is excessive, or
   any freeze/hang/save change occurs. Reaching 60 Hz display samples
   cannot be substituted for 60 real simulation FPS.

**No proof of higher visual fluidity or performance is asserted by the host
tests alone.**
