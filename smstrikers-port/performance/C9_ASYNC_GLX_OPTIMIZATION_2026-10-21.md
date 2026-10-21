# C9 isolated CPU async scheduling and generation-safe GLX fast path

Status: **source implemented; host ASan/UBSan + VitaSDK syntax verified; NO Vita
build, NO hardware validation, NO measured FPS gain**.

Branch: `experiment/c9-async-glx-safe-20261021`, starting from **exact original
C9** commit `512e9cc5a2fc044ba6ab5447d5cc4eb520631caa` — intentionally
**not** inheriting the failed experimental packed-state or extra CPU mesh
culling branches, nor previous packet profiling builds.

## Evidence and protected baseline

Latest quiet C9 hardware pairs were 27.84 and 28.86 FPS (~28.35 FPS average).
At 28.86 FPS a frame takes ~34.65 ms; 30 FPS requires 33.33 ms. Only a
repeatable quiet 4-run ABBA test may justify promoting any change.

The last 300-frame profile reported ~747.55 texture lookups per frame,
64.3% via marker AVL, ~3,379 marker probes/frame, and native texture ID
map 100% hits (0 uploads in the selected frames). Thus this change targets
**source handle resolution** rather than GPU texture decode/upload.

## What is already asynchronous in C9

- Frontend/game thread on Vita **CPU2**.
- Deferred VitaGL/GXM renderer and GPU owner on **CPU1**, existing bounded
  8 x 2 MiB immutable command ring. GPU and renderer APIs must stay there.
- One existing native game job worker on **CPU0**, shared with native audio
  support and other work. Adding arbitrary workers risks competing with audio.
- Native audio prefetch and disc reads have existing worker paths.
- The original game already uses `parallel_for` for fixed-update pose
  preparation, character grab/pose/blend and world frustum culling with
  barriers. The original game frustum culling stays unchanged.

Moving `glx_GetTex`, GLX registry updates, texture admission,
`gpu.create_texture`, upload, graphics state changes, or dynamic
resource release to the worker would violate ownership/lifetime rules.
They **remain synchronous and ordered on the frontend/GPU owner**.
The async disc completion is processed by
`nlServiceFileSystem`/`AsyncManager::Service` on game/task callbacks;
the background disc worker only performs I/O. No second thread writes
the GLX lookup registry through the new code.

## Implementation 1: asynchronous snapshots (default OFF)

Opt in with `vita_async_snapshot = 1` (maps to
`STRIKERS_VITA_ASYNC_SNAPSHOT=1`). Existing worker reused:

1. In `RenderSnapshot::Grab`, prewarm character-owned poses serially as
   before, dispatch the ten *already initialized* character snapshot
   copies on CPU0; CPU2 grabs up to 150 independent powerups plus 20
   fragments. Join before any NPC/ball/camera/snapshot consumers.
2. In `RenderSnapshot::Blend`, keep first-use character allocation serial.
   Dispatch ten warmed character blends on CPU0 and interpolate independent
   170 object values on CPU2. Join before chain chomp/bowser/ball/net
   interpolation reads the resulting snapshot.
3. `port::jobs::AsyncWork` is noncopyable and RAII-joined. A job may start
   only when the worker is idle, with its callback/context immutable until
   `finish_async`; the worker signals a pthread predicate barrier.
   `parallel_for` invoked while the async job is active runs synchronously
   on the calling thread to avoid replacing the worker's context. The
   shutdown path drains active work before stopping the worker.
4. No new threads, affinity, priority or GPU ownership changes. No delay
   loops and no barrier in the GPU submission thread.
5. On absent or `0` flag the existing C9 `parallel_for` scheduling path
   is preserved. If the worker cannot accept an async task, execute the
   existing synchronous/parallel code instead.

The expected win is overlap of independent CPU work, **not** guaranteed
fewer instructions. CPU0 also handles audio; measurement of P95, audio
stability and scheduler contention is essential before enabling it.

## Implementation 2: bounded GLX marker lookup cache (default OFF)

Opt in with `vita_glx_cache = 1` (environment
`STRIKERS_VITA_GLX_CACHE=1`). Key aspects:

- Two-way, 128-set, 256-entry fixed-size CPU-only cache from stable texture
  handle -> `PlatTexture*`. No unbounded allocation, hash map, or worker
  thread. Runs only on the game/GLX frontend.
- Exact direct `PTEX` pointer ownership/tag check always executes *first*.
  Animated handle `GetTexture(-1)` executes on **every** call and the
  resulting per-frame handle is never cached. Grid requests and fatal
  missing lookups bypass this cache entirely. Missing lookups are never
  negatively cached.
- Only successful ordinary marker tree lookups may be remembered. The
  original loop order (highest active marker level to zero) and
  `FindGet` result remain unchanged when the cache misses.
- Epoch invalidation on every state mutation that can change the result:
  `glx_AddTex`, `glplatTextureAdd`, `glplatTextureReplace`,
  `glx_AdvanceTexMarkerLevel`, `glx_BackupTexMarkerLevel`,
  `glxInitTex`, changed grid mode, and texture loader callback changes.
  Invalidating *before* marker removal prevents stale pointer access after
  arena rollback and prevents the ABA case when memory is reused.
- C9 original lookups are preserved bit-for-bit when flag is OFF.

## Offline verification completed

- `glx_marker_lookup_cache_test.cpp` with Clang C++20 ASan+UBSan: PASS
  over shadowed newest markers, 16 marker levels, repeated registrations,
  animation frame switching, direct-pointer bypass, grid and fatal
  lookups, marker rollback, replacement and reused pointers. 32,000
  repeated lookup comparisons exercised a seven-level marker workload.
- `native_jobs_async_driver_test.cpp` with real pthread workers and
  ASan+UBSan: PASS for true CPU overlap, nested work serial fallback,
  failure propagation, RAII join, shutdown drain, repeated cycles and
  original CPU0 priority/affinity.
- Unmodified `native_jobs_driver_test.cpp`: PASS.
- Existing high-stress `native_jobs_pending_driver_test.cpp`: PASS, 167
  exactly-once jobs, spurious wake handling and cancellation before
  worker observes stack-local contexts.
- `tools/test_c9_async_glx_integration.py`: PASS source contract checks
  for default-OFF flags, barrier ordering and all eight cache invalidation
  sites.
- VitaSDK `arm-vita-eabi-g++ -fsyntax-only`: **3/3 PASS**
  (`native_jobs.cpp`, `RenderSnapshot.cpp`, `glxTexture.cpp`).
  Receipt: `.tmp/strikers-c9-async-glx-tests-20261021/vita-front-end-syntax-v2.json`.
- The original C9 SELF/VPK SHA and the main HEAD/staged index must be
  checked again before promoting/building a candidate.

**Important**: sanitizer and syntax passes are not a hardware correctness
guarantee, and simple host registry models are not exhaustive tests of
Vita runtime allocation or live texture animation.

## Manual build, explicitly initiated by the user

Run the guarded script *only when ready for an isolated Vita hardware
candidate*:

```sh
sh /Users/robin994/Documents/Code/strikersVita/.tmp/strikers-c9-async-glx-safe-20261021/smstrikers-port/tools/build_c9_async_glx_manual.sh
```

Expected SELF:
`.tmp/strikers-c9-async-glx-safe-20261021/smstrikers-port/build-vita-c9-async-glx/strikers_vita.self`.

This script refuses a wrong source branch, dirty source, altered main
HEAD/index or changed original C9 hashes. It validates packaged eboot and
identical non-code VPK resources. It NEVER installs, pushes or merges.

## Strict hardware A/B matrix (no profiler)

Keep 444/222/222/166 MHz, deterministic seed and
`fixed_dt=0`, 600 frame warmup + 300 real gameplay frames, disable
VitaGL timing/packet profiler, uniform cache, affine UV, vertex fog,
extra CPU culling, GPU framegen, frameskip and overlay.

Test **separately**:

1. Both `vita_async_snapshot=0`, `vita_glx_cache=0` against original C9
   before interpreting any gains from a new link/build.
2. Async ONLY (`vita_async_snapshot=1`, `vita_glx_cache=0`).
3. GLX cache ONLY (`vita_async_snapshot=0`, `vita_glx_cache=1`).
4. Combined (both `1`) **only** if independent experiments show repeatable
   non-regressions.

For each candidate, run original C9 -> candidate -> candidate -> C9, each
300 selected gameplay frames after warmup, same hardware state/clocks.
Require both A/B order comparisons improve mean FPS, non-worse P95/P99,
same scene/draw workload, no flickering, correct animated textures, no
additional CPU/GPU sync, audio/goal transitions/save stability. Abort/
rollback on >5% FPS regression, >10% P95 regression or crash.
For robustness, use more samples if change is within natural ~1 FPS C9
run-to-run variability; **do not promote a marginal gain**.

Original C9 is still the deployed baseline, NOT this experiment.
Target: 30 **stable real gameplay FPS** first, then 60 FPS, without
accepting performance/correctness regressions.
