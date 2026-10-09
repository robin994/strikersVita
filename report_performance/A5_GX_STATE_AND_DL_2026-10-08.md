# A5 - GX state write suppression and prepared display-list replay
Date: 2026-10-08

## Evidence and scope

The first Native GXM AVNR v2 static-mesh implementation was installed on PS Vita
and rendered normally, but the user reported **the same roughly 25 FPS**.
That is expected for a cold cache-admission optimization: previously recorded
steady gameplay had ~229 resident geometry cache hits per frame and zero new
geometry misses. It does not remove recurring work in the GX frontend.

The latest useful diagnostic attribution (`token-profile-native`, distinct
from the current installed SELF) measured state translation median **7.235 ms**
per completed frame (nested stage breakdown: pipeline 2.729 ms, vertex about
1.325 ms); 282 draws/frame median. These samples are *not* contemporaneous
with the new AVNR v2 hardware run. The current GPU bottleneck is still not
independently measured.

Code audit found the main native frontend already has:

- Per-generation `translatedPipeline_`, `cpuRecipe_`, `gpuRecipe_`,
  texture/vertex domain revisions and geometry resident buffers.
- Cached pipeline resolution and constrained local batching.
- An optional **single-draw, immutable, pinned display-list** replay route:
  `submit_prepared_display_list` on the ordered GX consumer; the asynchronous
  producer never calls DrawSink directly. The prepared cache stores only
  header/stride/length metadata, **not a full precompiled GXM DrawPacket**.
  The old INI did not enable this, and previous profiling found zero
  `prepared_hits`. Its speed benefit remains an experiment.

Reintroducing an expensive full-material state hash per draw would conflict
with the earlier observation that hashing ~2.5KiB was costlier than direct
translation. A5 instead eliminates *provably redundant invalidations* before
the existing prepared draw/state caches are consulted.

## Three independent OFF-by-default experiments

### 1. `gxm_prepared_dl=1`

Uses Aurora's preexisting direct single-draw path when the async FIFO segment
already pins immutable display-list bytes. It parses once per distinct source
identity and cached GX vertex stride, skips interpreting that draw through the
general command processor, and calls the same DrawSink with the current live
GX state. The original FIFO path handles unsupported or non-simple lists.
`gxm_dl_shadow=0` can stay unchanged: the asynchronous FIFO separately
pins stable display-list data to preserve lifetime/order.

No cached GXM commands, matrices or material bindings are replayed here.
This is a **bounded GX command parsing optimization**, not yet complete
per-material native draw packet compilation.

### 2. `gxm_xf_equal_matrix_writes=1`

Opt-in decoder guard for these *additional* XF banks (the separate earlier
`gxm_xf_equal_pos_writes` position-palette experiment stays OFF):

- 0x078-0x0EF, texture matrices: full 2x4 or 3x4 writes.
- 0x400-0x459, normal matrices: 3x3 encoded into a padded 3x4.
- 0x500-0x5EF, post-texture matrices: 3x4.

For every encoded word, compare the exact unsigned 32-bit value with the
current destination word. Publish modified words exactly, preserving bit
patterns including signed zero and NaN payload. If *all* words were already
identical, **do not** advance `StateDomain::Vertex`. Changes still do, with
the same semantics as the old decoder. Any unsupported, partial, or invalid
XF request follows the existing checked/unsupported path.

This may save repeated vertex-state and fixed uniform snapshot work in
`DrawSink::submit`. It cannot remove genuinely changing pose matrices,
blend/TEV, framebuffer, viewport or skinning work.

### 3. `gxm_tev_decoded_write_gate=1`

Three TEV decoder call sites: color combiner, alpha combiner, and two-stage
TEV order. For a *currently active* stage, only a changed decoded semantic
snapshot dirties the pipeline, as before. If a TEV write is equivalent or
modifies only an **inactive** stage, it does **not** dirty the entire GX
pipeline/fragment/vertex state. The decoded state and BP register cache are
still updated; when genMode changes to activate those stages, the existing
pipeline generation invalidation exposes them. Other BP/XF/CP writes, alpha,
depth and lights keep their original handling.

This is *not* a global early return for bit-identical registers: it is
restricted to the three decoded TEV call sites and gated by the new flag.

## Diagnostics and oracle

`PerformanceSnapshot` now carries cumulative triples:
`xfTexWritesInspected/Unchanged/Changed`,
`xfNormalWritesInspected/Unchanged/Changed`,
`xfPostWritesInspected/Unchanged/Changed`,
`tevDecodedWritesInspected/Skipped/Changed`.

The Strikers `native08-diagnostic.csv` consumer header appends the matching
12 fields after the AVNR-v2 fields:

```
xf_tex_inspected,xf_tex_unchanged,xf_tex_changed,
xf_normal_inspected,xf_normal_unchanged,xf_normal_changed,
xf_post_inspected,xf_post_unchanged,xf_post_changed,
tev_decoded_inspected,tev_decoded_skipped,tev_decoded_changed
```

All are monotonically cumulative since backend initialization; compute
**deltas** across completed frame boundaries.  A nonzero skipped count is not
an FPS improvement by itself.

Host differential regressions send actual big-endian GX FIFO XF/BP commands:

- All matrix destination bits are compared after *each* decoded write with
  candidate flags OFF/ON, including normal packed row gaps, texture 2x4,
  post matrices, negative zero and distinct NaN payloads.
- Inactive TEV stages become active through genMode, then are overwritten
  again; all decoded TEV stages and **actual GXM pipeline keys** are compared
  at every checkpoint for reference versus candidate.
- Dirty/revision counters may differ only in the intended direction;
  changed active state remains invalidated.
- Existing async prepared-display-list test verifies hit/miss/reject, in-order
  submission, correct CDRAM stable shadow token and fallback on nonzero tail.

All previously validated GPU feature flags and hardware-critical constraints
remain unchanged, including GXM-only rendering and no streamed vertex path.

### Completed host validation

- Aurora Vita host CTest: **23/23 PASS**, after the A5 code changes.
- Encoded-XF state oracle compares every retained bit after every command for
  default/reference and opt-in, including signed zero and distinct NaNs.
- Encoded-TEV state oracle compares every stage, stage count and the actual
  resolved GXM pipeline source key at every inactive-to-active transition.
- Existing async prepared-DL equivalence and fallback tests: PASS.
- A separately built **AddressSanitizer + UndefinedBehaviorSanitizer** host
  frontend test: **1/1 PASS**, `ASAN_OPTIONS=detect_leaks=0` on macOS.
- Port's first-run/managed-block INI config test: PASS with 52 keys.
- A1c view/draw parser 12 tests PASS, perf-analysis 2 PASS, native adapter
  3 PASS. Root and Aurora `git diff --check`: PASS.
- Diagnostic CSV schema checked: 56 base numeric headers exactly match 56
  emitted integer values; dynamic telemetry phases remain appended as before.
- No Vita-target GXM build or in-match A5 run yet. **No FPS improvement is
  asserted.**

## Hardware test protocol

After a **manual user Vita build**, keep the already installed native v2
PSARC and configured baseline. For isolation, use the same SELF, scenario,
CPU/GPU clocks (currently 500/222MHz), same seed, fixed_dt=0,
vita_frameskip=0, gxm_disable=0x8, native/SHADOWED/CHARACTERS all ON.
Keep A3/A4 off.

A/B matrix (restart between each run):

| run | prepared_dl | equal_matrix | tev_gate |
| --- | --- | --- | --- |
| A5-0 reference | 0 | 0 | 0 |
| A5-1 list parse | 1 | 0 | 0 |
| A5-2 XF | 0 | 1 | 0 |
| A5-3 TEV | 0 | 0 | 1 |
| A5-4 combined, **only after individual checks** | 1 | 1 | 1 |

Run at least three interleaved gameplay windows per candidate, 600 warmup
frames and 1,200 steady active frames each, all render views enabled.
Check animations, the indexed-PN CHARACTERS passes, SHADOWED, goal replay,
cutscenes, alpha HUD/transparency, depth, flicker and crash regressions.
Reject if any view/draw disappears, regardless of FPS.

In **separate** short diagnostic sessions, enable diagnostics=1 and capture
the 12 new counters plus prepared_hits/misses/rejected, state_translate_us,
state_pipeline_us, state_vertex_us and full GX duration. Normal FPS
comparisons must have diagnostics=0, and must not use an artificial fixed
timestep (the old device workflow's `run` helper sets fixed_dt=16.667).

User-run Vita build command:

```sh
cd /Users/robin994/Documents/Code/strikersVita/smstrikers-port
STRIKERS_GX_THREAD=ON ./build-vita-native-gxm.sh
```

A5 is never enabled by default. No Vita build, device changes or git push
are performed automatically. The frame-time effect remains unknown until
the user runs this A/B series.

## First A5 device test: quiet capture and next isolated run

- User manually built A5 (`strikers_vita.self` SHA-256
  `6eb528f50c14fdc9fd83e2e525eb56226af1240ce89e2a3e5ba3d5ab62503df2`).
  It was installed over the previous SELF, with the previous binary preserved
  at `ux0:/app/SMSVITA01/eboot.bin.backup-1b88497cb9f7`.
- A5-1 configuration on Vita: `gxm_prepared_dl=1`, equal-matrix=0, TEV gate=0;
  AVNR v2 selected and active. User reported that the game was run, without
  supplying a gameplay FPS or visual comparison.
- Remote `native08-diagnostic.csv` was captured at
  `2026-10-08T21:54:34Z`, 300 samples, **all `match=0`**, mean frame time
  16,636.76 us. These are menu samples, **not a valid gameplay benchmark**.
  `diagnostics=0` also means `prepared_hits/misses/rejected` were not exported.
  Original CSV saved locally under
  `ab-artifacts/native-gxm-static-20261008/device-a5-xf-20261008/`.
- Prepared next isolated A5-2 INI on console (validated by readback):
  `gxm_prepared_dl=0`, `gxm_xf_equal_matrix_writes=1`, TEV gate=0,
  `frame_capture=ux0:data/strikersVita/a5_xf_gameplay.csv`,
  `frame_capture_match_only=1`, `frame_capture_play_only=1`, skip 600,
  capture 1200 frames. `diagnostics=0`, fixed_dt=0, frameskip=0, A3/A4=0,
  gxm_disable=0x8, native v2 unchanged. New INI SHA-256:
  `6d793f7bcfc0ee3539886f2cb67579b0bd9249749bd253a0b0c3e526383111a9`.
- Prior INI backup:
  `ux0:/data/strikersVita/strikers.ini.backup-a5-prepared-ccc2858b5351`.
  No new Vita build, binary installation, PSARC modification or git push
  occurred during A5-2 preparation.

## Following tranche (not implemented yet)

### A5-2 first gameplay attempt and short-capture recovery (2026-10-09)

- User reported they had run the A5-2 console configuration. On device the
  requested `a5_xf_gameplay.csv` was **absent**, and the last previous
  `native08-diagnostic.csv` still contained only 300 menu rows (`match=0`).
  Thus A5-2 has **no measured in-match FPS result**, despite its INI being
  confirmed active at the time of the first inspection.
- The normal frame capture is emitted only when **all** requested eligible
  frames have been collected; with the original 600 skip + 1,200 recorded
  frames and both match/play filters ON it can fail to complete during a
  short test run. `diagnostics=0` also means no A5 write-elision counters.
- While preparing a shorter capture, the console INI was independently
  replaced (SHA-256 `a2dc4564a41f0ef8735308f1c51250471dc876a698ffb9ede61b5e5d59376ea6`),
  reverting A5 switches to zero, restoring menu capture and setting
  `vita_test_match=1`. The first attempted update was blocked on its
  precondition; no file was written. The external change's origin is unknown.
- After verifying no local install/test process remained active and two
  consecutive identical reads of the INI, a new staged, readback-verified
  **short A5-2 capture** was installed, preserving `vita_test_match=1` and
  every other effective option. Changed only the effective XF gate `0 -> 1`
  and frame capture filename/match/play eligibility; `skip=60` and
  `frames=300` were already active in the externally restored defaults.
  Path: `ux0:data/strikersVita/a5_xf_gameplay_short.csv`.
- New verified INI SHA-256:
  `82461733bc4a582188bb71ef2cf00a5d44efa0c4b4b53dd20b5b77b011fce9c6`;
  rollback:
  `ux0:/data/strikersVita/strikers.ini.backup-a5-short-a2dc4564a41f`.
- **Hardware gate still pending:** relaunch and allow at least 360 frames
  with `s_matchActive && s_playActive`. Retrieve the new CSV; reject any
  capture without match=1. Compare against a paired OFF capture in the
  same scene/seed/clocks before making a performance claim.
- No new Vita binary/PSARC build, installation, or push during this recovery.

### A5-2 complete 300-frame gameplay capture; A5-3 installed (2026-10-09)

- User ran the revised A5-2, and `a5_xf_gameplay_short.csv` was successfully
  created on device (SHA-256
  `c04a583b59da5bfed8372cace0fb70b03b490029ad9305f14d5a8d882d8c92e0`).
  It contains **300/300 gameplay rows** (`match=1` throughout), play-frame
  indices 60-359; `diagnostics=0`, `fps_overlay=0`, `fixed_dt=0` and
  `vita_frameskip=0`. The runtime reports **444 MHz CPU, 222 MHz GPU**, even
  though the requested `cpu_mhz` is 500. Do not normalize these timings to a
  hypothetical 500-MHz environment.
- Full run: mean **54.383 ms/frame** (aggregate **18.388 FPS**), median
  **49.5415 ms/frame** (~20.185 FPS), p95 **90.312 ms**. The first 60
  samples still had slower startup transients (mean 66.319 ms); the last
  200 samples averaged **50.988 ms/frame** (~19.613 FPS), p95 64.956 ms.
  Original captured CSV preserved locally at
  `ab-artifacts/native-gxm-static-20261008/device-a5-xf-20261008/a5_xf_gameplay_short.csv`.
- **No validated XF improvement claim**: there is no same-gameplay baseline
  recording at the same *actual* CPU/GPU frequencies, draw workload and
  seed, and the previously estimated ~25 FPS was not a paired capture.
- Following the documented sequential experiment matrix, the console was
  updated via staged FTP and full INI readback to **A5-3 TEV only**:
  `gxm_prepared_dl=0`, `gxm_xf_equal_matrix_writes=0`,
  `gxm_tev_decoded_write_gate=1`, capture
  `ux0:data/strikersVita/a5_tev_gameplay_short.csv`, match/play only,
  skip 60, frames 300. Native v2 archive, SELF, diagnostics=0, scene seed,
  `vita_test_match=1`, fixed_dt=0 and GPU/CPU clock requests all unchanged.
  INI SHA-256 `fe3e040272ff6d32d53c245592d2df579b6b4bdbc204027785309d9ae6ec0626`;
  rollback backup:
  `ux0:/data/strikersVita/strikers.ini.backup-a5-xf-188292830860`.
- The next hardware pass should replay the same test match for at least 360
  active play frames, collect the TEV CSV, and **then run a paired OFF/OFF/OFF
  reference capture** before comparing changes. A5-3 cannot be evaluated from
  the preceding menu-only A5-1 capture.
- No new Vita build, installation of executable/PSARC, or git push in this step.

### A5-3 TEV capture and code-path audit (2026-10-09)

- Hardware A5-3 produced a valid quiet gameplay capture of 300/300 `match=1`
  rows (`a5_tev_gameplay_short.csv`, SHA-256
  `8287ea6ee1b3ae2ae37c7ef52b1a6a09aef58df2c793a3eaf896c93a36e96e30`).
  Effective CPU 444 MHz, GPU 222 MHz, fixed_dt=0, frameskip=0; 60 active
  play frames were skipped. Full window mean **59.483 ms/frame**
  (**16.812 FPS**), median 51.240 ms, p95 111.162 ms. After removing the
  first 100 recorded frames the final 200 averaged **52.037 ms**
  (**19.217 FPS**). A5-2 XF-only final 200 averaged **50.988 ms**
  (**19.613 FPS**). No comparison is causal without paired A5-0 baseline.
- **Crucial architecture finding:** AVNR v2 native geometry is *only an
  optional GPU-buffer cache admission shortcut*. `glxSend.cpp` still calls
  `GXCallDisplayList` (or `GXBegin`); the FIFO, GX BP/CP/XF processing and
  DrawSink's dynamic state/pipeline translation all still run. On every
  native geometry cache hit DrawSink also builds/publishes fixed uniforms and
  creates a fresh `DrawPacket` (`aurora_vita_draw_sink.cpp:1110-1124`); the
  renderer later binds pipeline/textures/uniforms and calls `sceGxmDraw`.
  AVNR v2 is only queried for a cache **miss**, with a stable source, no
  independent index array, and exact GPU layout match
  (`vita_static_geometry.hpp:211-219`). Neither the existing PSARC count nor
  `gxm_native_gpu_static=1` proves an actual AVNR v2 cache hit. No diagnostic
  `native_gpu_geometry_hits/attempts` recording from this run is available.
- The direct GXM renderer itself is already native: `gxm_renderer.cpp`
  binds programs/samplers/uniforms and calls `sceGxmDraw` at ~1717. A real
  CPU bypass must be introduced **above the GX FIFO** while preserving
  render-owner thread ordering: give Strikers' native GLG/material IDs to a
  precompiled `NativeDrawRecipe` (immutable mesh and pipeline handles) and
  submit an ordered native draw command from the producer, with per-instance
  live palette/matrix/texture state supplied to the consumer. The legacy GX
  path remains for unsupported/dynamic content and difficult EFB effects.
  No claim that asset preprocessing alone removes GX translation is valid.
- The current v2 static pack has **zero indexed-PN records** and therefore
  does not cover the prior A1c hot `CHARACTERS` family. Animation-aware
  GPU-skinned native variants are essential if this becomes a production path.
- Following A5-3, a quiet **A5-0 baseline** was installed via guarded FTP
  promotion and readback (INI SHA-256
  `34fa39f552d441fbbb69e650b651dc1a97509d446c3d3dd9e468e6cfe535e888`).
  Effective A5 flags all 0, unchanged SELF, PSARC, seed, clocks,
  `vita_test_match=1`, 60 skipped + 300 captured match/live-play frames.
  Requested output: `ux0:data/strikersVita/a5_baseline_gameplay_short.csv`.
  Backup INI:
  `ux0:/data/strikersVita/strikers.ini.backup-a5-tev-8692ae59b3ad`.
  Baseline capture still requires a fresh user-run restart/playthrough.

### A5-0 quiet gameplay baseline recovered: first meaningful comparison (2026-10-09)

- User ran A5-0 and produced `a5_baseline_gameplay_short.csv` (SHA-256
  `e64ead9a6208e9e3a1c1fe1d5cb2fffd6ef8a0bf8fd0fb015c7d5acf2f840db0`).
  All **300/300 frames `match=1`**, play frame indices **60..359**, CPU
  **444 MHz actual**, GPU **222 MHz actual**, frameskip=0, fixed_dt=0 and
  diagnostics=0. Same nominal automatic test match configuration and SELF /
  native PSARC as A5-2 XF-only and A5-3 TEV-only. Remote INI was independently
  read again with all A5 switches still zero; gameplay baseline remains active.

  | Quiet window | A5-0 OFF | A5-2 XF only | A5-3 TEV only |
  | --- | ---: | ---: | ---: |
  | Full 300-frame aggregate FPS | **16.729** | 18.388 | 16.812 |
  | Last 200-frame aggregate FPS | **20.121** | 19.613 | 19.217 |
  | Last 150-frame aggregate FPS | **20.504** | 19.529 | 19.196 |
  | Last 200-frame mean ms | **49.701** | 50.988 | 52.037 |
  | Last 200-frame p95 ms | **64.889** | 64.956 | 68.058 |
  | First 60 captured FPS | **10.295** | 15.079 | 11.248 |

- **Interpretation:** first-window transients vary dramatically and distort
  full-300 averages. Last-200 means put XF at approximately **-2.5%** and TEV
  at approximately **-4.5%** relative to OFF, within differences that cannot
  be interpreted causally from only one non-interleaved run per option.
  Neither A5 change demonstrates a sustained meaningful FPS improvement.
  A5-1 prepared-DL still lacks a valid gameplay capture (only an earlier
  menu-only capture). Do **not** claim its impact has been measured.
- Keep all A5 options **OFF** until a compelling measured gain. The currently
  installed A5-0 INI already implements that recommendation, retains
  `gxm_native_gpu_static=1` and the validated native sidecar.
- Saved original frame CSV and machine-readable three-way comparison in
  `ab-artifacts/native-gxm-static-20261008/device-a5-reference-20261009/`.
  No Vita build, source code edit, or binary/PSARC installation was required.

### Direction after A5 — A6 Strikers-native draw dispatch (not yet implemented)

The higher-value approach is to bypass GX **before** packet state emission,
rather than eliding more equal BP/XF writes. Packet source `glxSend.cpp`
calls `glx_SwitchViews`, `glx_SwitchProgram`, `glx_SwitchTexConfig`,
`glx_SwitchUserData`, `glx_SwitchTexture`, `glx_SwitchRaster`,
`glx_SwitchMatrix`, `glx_SwitchStreams` and only then `glx_DrawPacket`
(`glx_SendFrame_cb:3011+`). An early hook *inside* `glx_DrawPacket` skips only
the final GX draw and preserves almost all producer-side overhead. For a true
bypass, resolve eligible immutable model and material identities at or before
`glx_SendFrame_cb`, **prior to** the GX state switch sequence.

Execution constraints:

1. Preprocess packet mesh layouts/indices **and** material/raster/TEV shader
   descriptors to a game-specific immutable `NativeDrawRecipe`, with asset
   identity/version/ownership, correct multi-view pass, and CPU/GPU residency.
   Never conflate GPU-ready mesh data with the still-GX-backed material.
2. Dynamic instance matrices, normal/position palettes (including indexed-PN),
   lights, texture selections and viewport/scissor must remain live. The
   static GLG v2 pack has no per-vertex PN selectors, so design a separate
   animation-aware branch for `CHARACTERS`; do not claim static VRAM mesh
   coverage is enough for Strikers' dominant draw families.
3. A native draw must execute **in order on the existing GX/GXM owner thread**
   (not via unsynchronized producer-thread `sceGxmDraw`), with a typed ordered
   native packet command, correct view changes/EFB side effects and error
   fallback. State invalidation must allow native and legacy GX draws to
   interleave without stale programs, viewport, uniform buffers or textures.
4. Make it **opt-in, OFF by default**, first prove bit-for-bit/replay-equivalent
   native output for one simple immutable unlit model in one view. Capture
   native-dispatched/legacy-fallback counts, draw order, pipeline/texture
   identity and 1:1 render-view coverage. Expand incrementally to SHADOWED
   and animated CHARACTERS only with targeted screenshot/trace regressions.
5. After functional equivalence, hardware-interleave paired native OFF/ON
   quiet captures with 600+ stable gameplay frames and instrumentation in
   **separate** sessions. No build automatically, no unconditional Aurora
   submodule performance change, no temporary VitaGL fallback.

A real **per-material native DrawPacket compiler** would require immutable
material identity plus lifetime/versioning of GXM shader/pipeline and textures,
and per-draw live matrices, viewport/scissor, fragment uniforms, skinning
and shadow state. The guarded intermediate work here is necessary but
**does not satisfy that larger goal**. Do not reuse a packet across materials
or character poses based only on a pointer/hash to the display list.
