# A6 Native Draw Replay - Phase 1 (2026-10-09)

## Summary and experimental boundary

A5-off quiet gameplay is roughly 20.1 FPS over the last 200 frames of a
300-frame capture at an actual CPU clock of 444 MHz / GPU 222 MHz; A5 XF-only
and TEV-only did not demonstrate sustained gains. The prior AVNR v2 pack
preconverts immutable GLG geometry but Aurora still processes every GX FIFO
command and builds the draw packet every frame.

**Phase 1 adds a genuine, narrow alternative to GX command decoding**, by
replaying the **last GXM DrawPacket** for *consecutive identical* native
draws in the **same frame**, only on the renderer-owning GX worker. This is
not a complete producer-side per-material renderer or generalized offline
GXM material compiler. It is intentionally a correctness gate and a hardware
measurement of real bypass applicability.

### Normal path

`Strikers glx_SendFrame_cb` -> emit GX BP/CP/XF setup ->
`GXCallDisplayList` -> Aurora FIFO parse -> DrawSink translate ->
validate resident geometry -> submit GXM DrawPacket -> `sceGxmDraw`.

### A6 accepted replay

`Strikers glx_SendFrame_cb` verifies identical packet/stream state and
no other GX command emitted since prior packet -> pins original immutable
GX DL in FIFO with a **NativeDrawReplay** segment -> ordered GX worker
validates one triangle draw, its immutable pin identity and existing
resident AVNR v2 geometry + frame/memory revisions -> duplicates the
previous GXM `DrawPacket` (including existing GPU program, buffers, uniform
and texture bindings) -> same GXM renderer -> `sceGxmDraw`.

The second packet **does not emit** the producer-side GX state and draw
commands and **does not invoke DrawSink::submit()**. Its producer and
consumer FIFO/translation overhead is actually avoided, rather than just
prepacking geometry once.

If any producer or consumer precondition fails, **the original GX DL bytes
are decoded at the exact original FIFO position**. No draw is silently
skipped. Ineligible packets bypass A6 entirely and emit all original GX
commands.

## Exact eligibility gates

**Producer / Strikers:** `gxm_native_draw_replay=1`; same
`glModelPacket` identity, full byte-identical packet and stream records,
same current frame and render view, GX write epoch unchanged since previous
packet; new draw callback flags exactly `0x800` (no view/material/user/
stream/matrix switch). Only compiled DLs, triangles, >=48 vertices,
`prog_3d_unlit`, 1-8 known indexed streams, identity matrix, no textures,
no user data, no alpha, co-planar, fog, viewport, translucent/constant color
or indirect state. Only two conservative views (`GLV_UnsortedPerspective`
and `GLV_FrontEnd`) currently admit replay. Every stream has an identified
source size; DL length between 3 B and 256 KiB.

**Transport:** the existing async FIFO pins a cached, byte-validated,
immutable shadow of the *original* GX display list, retaining its revision
identity. `NativeDrawReplay` uses the same segment slot and source pointer;
it does **not** store raw guest pointers in the command packet as GPU
handles. FIFO write helpers bump the producer write epoch while A6 is enabled.
The epoch also advances for **pinned ordinary display lists**, synchronous
GX processing, queued worker callbacks, and rewrites of recorded display lists.
Without these gates a GPU state change could occur between the original
packet and a replay without a corresponding byte written to the inline FIFO.
The segment retains the complete original DL bytes for exact fallback.
The GX worker validates one exact triangle draw plus NOP padding, current
CP vertex-format index and stride. Any compound BP/XF/CP list falls back.

**Consumer / Aurora:** only a GPU-resident geometry cache entry *actually*
admitted from AVNR v2 can seed a native replay. An ordinary geometry cache
entry, a streamed draw and a v1 precompiled geometry cannot. The last
accepted packet must refer to that entry's exact VB/IB handles. A6 checks
the stable-geometry key, entry object identity, same-frame last-use, source
revision epoch and all dependent vertex streams. No GPU handle survives
a frame reset, command reset, flush, shutdown or cache retirement.

**GXM ownership:** the queued native replay is processed on the same FIFO
consumer that owns the renderer. It copies the complete immediately previous
DrawPacket into the command stream (materializing shared uniforms/textures),
reusing only the **current frame's** fixed uniform pool. No worker calls
`sceGxmDraw` directly from the game thread, and no GPU finish, scene
reordering or view omission is introduced.

The replay also preserves the frontend draw-index increment, primitive and
vertex format coverage, and an equivalent `FrameTrace` record. If a diagnostic
draw limit is enabled, A6 rejects replay and uses the normal GX path. GPU
resource invalidations during copy/eviction and Aurora renderer shutdown
invalidate the cached replay candidate. The positive replay fast path is
**compiled for native GXM only**, never VitaGL.

Final OFF-path audit: the replay-tracking switch is atomic between producer
and consumer, and the sink does **not** capture or clear replay state for
every draw when A6 is disabled. The ordinary non-A6 GXM submission remains
on its existing path, with no A6 asset lookup or per-draw metadata copy.

### Resume QA (2026-10-09 morning)

- `vita_cpu_workers` timeout in the earlier full 23-case CTest was not
  reproducible in two consecutive **isolated** invocations (both passed).
  Treat the original timeout as an unresolved, possibly scheduler-dependent
  flake, not as evidence that A6 is fully validated on the Vita.
- Rebuilt Aurora host targets and ran targeted CTest for backend contract,
  native assets, prepared DL parser, frontend translation (including new
  pinned-DL / worker-callback / exact-slot GX fallback test) and command
  stream: **5/5 PASS**.
- Rebuilt **all** Aurora host targets and reran the complete serial CTest:
  **23/23 PASS**, including `vita_cpu_workers` (0.80 s). Previous 22/23 was
  not reproduced. Native replay counters CSV header and values both have
  **59 fields**, in the same order. Both Git worktrees pass `diff --check`.
- The Strikers **PS Vita target** has intentionally **not** been compiled:
  this host-only suite validates shared Aurora components and the synthetic
  producer/FIFO test, **not** native PS Vita rendering, Vita ABI or game link.
  Successful native GXM replay on real hardware has yet to be established.
- After the final OFF-path guard and atomic-switch changes, rebuilt all host
  targets and reran serial CTest: **23/23 PASS in 12.12 seconds**. This is the
  final tested host source state at the time of this report.
- No Vita executable, package, install or console INI mutation was performed.

## Configuration and diagnostics

Default values are **OFF** even for users with the sidecar:

```ini
gxm_native_assets=1
gxm_native_gpu_static=1
gxm_native_draw_replay=0

gxm_prepared_dl=0
gxm_xf_equal_matrix_writes=0
gxm_tev_decoded_write_gate=0
gxm_streamed_vertex_gpu=0
gxm_disable=0x8
fixed_dt=0
vita_frameskip=0
```

For the explicit A6 experiment change only `gxm_native_draw_replay=1`
and restart the game. For diagnostics enable `diagnostics=1` **only in a
separate run** with consumer capture enabled. The existing performance
capture CSV appends:

```
native_replay_attempts,native_replay_hits,native_replay_fallbacks
```

These are cumulative across completed frames; compute deltas. Also collect
`native_gpu_geometry_hits/attempts`, `prepared_hits`, `geometry_hits`,
`state_translate_us`, `gx_total_us`, draw counts and view coverage.
A zero `native_replay_hits` count means this phase does **nothing useful**
for that game scene even if enabled; do not conclude there is a native bypass
performance gain. In particular, the static AVNR v2 pack has **no indexed-PN
skinned characters** and this phase does not cover the hot CHARACTERS view.

## Test matrix and rollback

### First hardware run: A6 ON, no replay-counter capture yet (2026-10-09)

- User built and installed `strikers_vita.self` / `eboot.bin`, SHA-256
  `0de405e4fa3891e04b298d6e8b8198ca8fda7e7ddb3b57d08444f635337e45f3`.
  Old SELF backed up as `ux0:/app/SMSVITA01/eboot.bin.backup-900f0008e930`.
- Runtime log confirms `gxm_native_draw_replay=1`, native GPU static=1,
  native sidecar `sms-native-gxm-static.psarc` (280,013,735 bytes).
  Game-managed defaults appended another `gxm_native_draw_replay=0`
  **after** the custom override. The parser is *first-value wins*, so the
  effective A6 value remains **1**, not 0.
- Recovered on-device `a6_native_draw_replay_diag.csv` (SHA-256
  `2ecb2bf49c301babf7dd888fc71980d25cc2ba33529ef2665adbe1c396b9ddf5`)
  with **300/300 match=1 gameplay samples**, play-frame 60..359,
  **CPU actual 444 MHz, GPU 222 MHz**. With `diagnostics=1`, the full
  window ran at **15.978 FPS**, the last 200 samples at **18.802 FPS**;
  frame median 52.715 ms, p95 around 114.3 ms full-window.
  An earlier A5-0 (`diagnostics=0`) last-200 quiet reference was
  **20.121 FPS**, so the raw difference must **not** be interpreted as
  an A6 regression: diagnostics and inter-run timing are different.
- `a6_native_draw_replay_diag.log` (SHA-256
  `ce02164f3e1f49621806738e05a3829849ac340eadc6c53aec0cf3d8da173056`)
  classifies recent present/queue batches as `cpu_or_frontend_bound`
  (about 32 us average queue latency and 0% blocking). It also emits
  `scene_budget_exceeded scenes=6 budget=5`: monitor view fidelity;
  the log alone does not establish whether any scene was visibly dropped.
- **Critical measurement omission**: `frame_capture` is only the compact
  `sample,match,match_frame,frame_us,tasks_us,present_us,sleep_us` CSV;
  it does *not* include the A6 replay counters. `performance_capture`
  is a **different** optional consumer snapshot export, invoked only when
  `diagnostics=1` and `livePlay=true`. Therefore this hardware run cannot
  prove that a single draw used native A6 replay. No replay-hit claim is made.
- Next diagnostic requires this additional first-occurrence INI override:

  ```ini
  performance_capture=ux0:data/strikersVita/a6_native_replay_perf.csv
  performance_capture_frames=300
  performance_capture_skip=60
  ```

  After restart, allow >=360 live-play frames and inspect **deltas** of
  `native_replay_attempts`, `native_replay_hits`, `native_replay_fallbacks`
  and `native_gpu_geometry_hits/attempts`. The existing `diagnostics=1`
  and A6=1 must remain first effective values. The model-owned attempt to
  update this INI remotely in this conversation was blocked before execution;
  these three extra lines were **not installed on the Vita**.
- Downloaded the original A6 CSV/log/INI into
  `ab-artifacts/native-gxm-static-20261008/device-install-a6-20261009-0934/run-after-test/`.
  No A6 source rebuild, new Vita binary, PSARC change, or Git push this step.

1. Keep current GXM-only build and native GXM sidecar: first launch with A6
   OFF. Validate all draw views, geometry, characters, shadows, alpha/HUD,
   gameplay->goal->replay, menus and absence of crash.
2. Same SELF/PSARC and seed with A6 ON, all other A5 experiments OFF. Make
   sure the on-device INI explicitly provides a first-occurrence override:
   the PortConfig managed block is default-off. Check errors, flicker,
   shadows, texture/material integrity and all view/draw counts. If a
   single draw disappears, immediately revert `gxm_native_draw_replay=0`.
3. Collect **separate diagnostic** runs and classify producer eligibility
   vs consumer attempted/hit/fallback. Do not call a zero-hit test a success.
4. Repeat paired quiet OFF/ON active gameplay runs after 600 warmup frames,
   at least 1,200 gameplay frames each, with the same *actual* CPU/GPU clocks.
   Use independent runs/interleaved order for time-series variation. OFF
   remains the production/default setting until both correctness and useful
   recurring FPS improvement are shown.
5. Preserve `gxm_disable=0x8`, use no VitaGL, never enable
   `gxm_streamed_vertex_gpu` (known corrupted output), do not suppress any
   render view or shader family.

## The remaining major project

Phase 1 handles only identical consecutive draw packets on the live GX
pipeline. A **complete A6 native model path** must precompile per-model
*geometry and material* records with stable resource IDs; safely represent
dynamic transforms/animated skinning/normal palettes, texture changes and
per-view depth/cull/blend/alpha/TEV, then enqueue typed commands in FIFO order
*before any GX state switch*, including cross-frame use. For SHADOWED and
CHARACTERS, all uniforms and renderer state must be patched per draw and
stale caches invalidated. Reusing a static native vertex buffer alone is
insufficient.

This is a substantial separate development step; do not claim A6 phase 1
already implements it.

## Build

Only host tests are executed automatically. The **user** builds Vita GXM:

```sh
cd /Users/robin994/Documents/Code/strikersVita/smstrikers-port
STRIKERS_GX_THREAD=ON ./build-vita-native-gxm.sh
```

No Vita build, deployment, console INI edit or Git push in this task.
