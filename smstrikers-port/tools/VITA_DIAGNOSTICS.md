# Runtime diagnostics comparison

The native GXM build accepts an initialization-time master switch in
`ux0:data/strikersVita/strikers.ini`. Restart SMSVITA01 after changing it.

```ini
diagnostics = 1
fps_overlay = 1
```

`diagnostics=1` is the default. It permits the selected diagnostic settings,
including `task_profile`, packet/view/FIFO profiling, probes, captures and
optional Aurora telemetry. It does not automatically enable expensive full
Aurora tracing or forced GPU waits. The individual selectors retain their
existing meaning.

Change only `diagnostics=0` for the second run. This suppresses diagnostic
selectors before their cached initialization, disables OSReport/crash-log
output and the Aurora logger, and avoids optional task/view/packet/FIFO,
vertex-worker, legacy frame-counter and native GXM timing collection. It also
suppresses diagnostic GXM mask bits 0x100/0x200/0x400, including their artificial
GPU synchronization. The configured mask and its optimization bits remain intact.
The INI is not rewritten; switching back to 1 restores its selected diagnostics
on the next launch.

Normal rendering, cache validation/invalidation, display synchronization, frame
pacing, CPU/GPU clocks and CPU3 admission/quota calculations remain active.
CPU3 clocks and system-idle samples implement the usage limit, so they must
remain enabled even in the quiet run. Cache/resource bookkeeping required by
the renderer is also retained.

The compact FPS counter is the sole default diagnostic exception in the quiet
run. It uses a running sum of the last 256 frame durations, includes deferred
limiter sleep and acquire, and needs two frame-clock reads with no detailed
snapshot or percentile sorting. Keep `fps_overlay=1` in both runs so its display
cost is common. For an external measurement with all optional timing/display
disabled, use `diagnostics=0` and `fps_overlay=0`.

Use the same SELF, stadium, teams and game phase; compare after caches and shaders
have warmed. Do not change the rendering experiment flags at the same time.
The quiet run will not append diagnostic files: existing files from the earlier
run can remain on the device and must not be mistaken for new samples.
Task/view/FIFO timers can overlap and are not complete frame or GPU timings;
use the frame FPS counter or an external capture for this comparison.

`build-vita-native-gxm.sh` compiles runtime logging support so the same SELF can
run both profiles. A manual `STRIKERS_VITA_NO_LOGS=ON` build still prevents logging
support from being restored by the INI. This switch requires a fresh build;
existing SELF files do not implement it.

## Automatic test match and frameskip

Both controls already use startup flags in `ux0:data/strikersVita/strikers.ini`.
Restart the game after changing them. Put overrides before the managed defaults
block; the configuration uses the first value of a key.

```ini
vita_test_match = 0
vita_frameskip = 0
fixed_dt = 0
```

`vita_test_match=1` starts the automatic CPU-versus-CPU friendly used in hardware
tests. `0` keeps the normal frontend. It leaves the original title-screen attract
demo behavior intact. AI, physics, audio, replay and rendering remain active in
the test match.

`vita_frameskip=1` enables the existing adaptive gameplay catch-up path. It uses
wall time for simulation and can discard up to three rendered frames; `0` disables
it. A nonzero `fixed_dt` disables this path so deterministic benchmarks retain a
fixed simulation step. Keep frameskip OFF for renderer performance comparisons;
skipping frames does not establish faster rendering.

## In-match Vita debug menu: per-fragment shader A/B

```ini
vita_debug_menu = 1
vita_frameskip = 0
fixed_dt = 0
```

The `vita_debug_menu` flag defaults to **0** and requires a restart. With `1`,
press **L + R + SELECT** during a match. The built-in pause menu opens on the
`FRAGMENT SHADERS` pages. The first two controls are **LIVE FPS COUNTER**
(toggle the lightweight FPS display without editing the INI) and **RESTORE ALL
FRAGMENTS**. Each following row identifies one actually used native GXM fragment
shader (`FS` index, low 32 bits of its stable 64-bit shader-source hash, ON/OFF,
TEV stage count and cumulative draw count). `NEXT`/`PREV` browse every captured
shader; entries are ordered by draw frequency. Reopen the menu to refresh the
counts and discover shaders first used since the previous visit. The toggle
state persists across pause/resume, but not across game restarts.

Only **game draw submissions** that use the selected fragment program are
skipped; internal clears, EFB blits and presentation draws are never filtered.
While the pause menu is open, filtering is bypassed so a disabled HUD fragment
cannot hide the menu. With `vita_debug_menu=0`, capture and per-draw counters are
inactive. This experiment works with `diagnostics=0` and does not enable the
diagnostic `sceGxmFinish` probes.

**Important:** OFF skips the *entire draw*, including its vertex work, depth
writes, blending and fragment shader. Resulting FPS changes are an upper bound
on that draw's cost, **not** the isolated fragment-program cost. Missing depth
can also change costs in subsequent passes. Do not treat a faster but incomplete
frame as a shippable optimization. For comparisons, select one fragment at a
time, close the menu, allow stable gameplay, compare on the same scene, and
restore it before the next test. Never promote OFF to a production default.
The FPS overlay averages the last 256 frames, so allow at least 256 gameplay
frames after resuming before comparing the displayed result.

## Ordered GX draw capture and A1c payload signatures

`vita_view_draw_capture=1` enables a **heavy, opt-in** in-game draw trace.
Markers travel in the GX FIFO; the consumer flushes deferred draw packets
before switching the assigned view. The diagnostic changes command-batch
boundaries and is **not a quiet FPS benchmark**. The output is raw JSONL v2,
which must be sealed with the exact SELF, INI and shader-cache artifacts before
the offline analyzer can accept it as complete.

With `vita_view_draw_payloads=1`, the GXM consumer additionally fingerprints
referenced indexed-vertex bytes, index order, GPU uniform snapshots (excluding
snapshot revisions) and viewport/scissor/texture sampler state. This performs
additional CPU reads and is therefore **OFF by default**, even during regular
view/draw capture. Use it only for short A1c diagnostics on identical scenes,
not to measure shader execution or FPS. The comparison command
`tools/compare_vita_view_draw.py control.jsonl candidate.jsonl --require-payloads`
rejects runs without signatures, mixed signature modes, divergent draw order
or incompatible SELF/cache identities. For A/B flag comparisons, review the
two INI files and explicitly add `--allow-ini-change`.

Matching signatures do not prove identical texture contents, depth outcomes or
pixel output. They are a CPU-side regression gate; screenshots and correctness
checks on Vita remain mandatory.

## Dynamic vertices on the GPU

`gxm_streamed_vertex_gpu=1` enables the existing native fixed-vertex shader for
eligible dynamic/animated geometry. The CPU still decodes and packs the source;
matrix, normal, lighting and supported texgen operations run in the vertex
shader. Unsupported draws retain their CPU path. This selector does not change
the simulation timestep or enable frameskip. `0` selects the CPU fallback.
Restart after changing the INI, and place overrides before the managed block.

The device helper records the selector in the run manifest and can compare both
paths using the same SELF and baseline INI:

```sh
python3 smstrikers-port/tools/vita_workflow_device.py run \
  --host 192.168.1.79 --baseline /path/to/device-backup \
  --out /path/to/cpu-run --label streamed-cpu \
  --asset-archive ux0:data/strikersVita/sms.psarc \
  --streamed-vertex-gpu 0 --frames 1200 --skip 600
```

Repeat with a different output/label and `--streamed-vertex-gpu 1`. Record warm
shader state, actual clocks, installed SELF and completed gameplay row count.
Use separate screenshot runs to check models, shadows, lighting, animation and
HUD; screenshot serialization and new shader compilation perturb frame times.
Restore the backed-up normal INI after the experiment.

## Performance plan experiments

All experiments use startup INI selectors, with zero as the default. Change one
selector per launch before combining measured gains:

| Selector | Effect | Original fallback |
|---|---|---|
| `gxm_prepared_dl` | Reuse validated single-draw metadata on the GX consumer | FIFO parser for other lists |
| `vita_game_core3` | Quota-controlled fourth lane for independent pose/matrix jobs | Three game lanes |
| `vita_skin_packets` | Prepare per-view skin matrices with private lane storage | Original per-draw matrices |
| `gxm_exact_bc1` | Lossless CMPR-to-BC1 subset and native allocation budget | Original compact/RGBA conversion |
| `gxm_resident_cdram` | Prefer CDRAM for immutable geometry | USER allocation fallback |
| `gxm_native_assets` | Read matching AVNR sidecars from PSARC on cold admission | Original GLT/GLG decoding |

The test helper accepts repeated `--set KEY=VALUE` overrides. It prefixes a
temporary INI, records its hash and the installed SELF, and enables the automatic
match through `vita_test_match=1` in that file. The game code never forces it on.
After every test series, restore the normal configuration:

```sh
python3 smstrikers-port/tools/vita_workflow_device.py restore-config \
  --host 192.168.1.79 --baseline /path/to/device-backup --out /path/to/restored
```

Attribution is a separate launch, for example:

```ini
diagnostics = 1
fps_overlay = 0
vita_test_match = 1
vita_frameskip = 0
performance_capture = ux0:data/strikersVita/consumer-phases.csv
performance_capture_frames = 300
performance_capture_skip = 600
```

Only live-play iterations enter the bounded collector. It samples the last
completed consumer snapshot; consumer frame indices can skip or repeat relative
to producer iterations. It adds no GX synchronization. The CSV is written once
after the final sample, so exclude that write from a separate FPS comparison.
Attribution disables per-command CP/XF/BP clock reads and full coverage/trace,
but draw/phase timers still perturb execution. Detailed diagnostic CSV frame
times are not quiet-run FPS evidence.

`gx_total_us`, geometry/prepared-list hit/miss counts, pool-busy fallbacks, finish
counts/waits and native asset counters are cumulative; compare consecutive
values or the interval endpoints. Draw/vertex/upload counts and named phase
times belong to the completed consumer frame. Producer/consumer waits overlap
other threads. Phase scopes are nested: never sum them into a frame duration.
`core3_total_pct_x100` reports the scheduler's system-idle-based total utilization,
not just this application's worker time. Report maxima and telemetry validity
before claiming the CPU3 total-core ceiling was respected.

Mask `0x108` permits the existing per-scene diagnostic finish mode with the
0x8 baseline optimization setting. Scene wait columns measure residual GPU
completion waits after submission; they serialize the pipeline and do not equal
full GPU execution time. Use them only for attribution, then restore `0x8` and
`diagnostics=0` for performance comparisons. Retain exact BeginScene state reset,
scissor, EFB order, full simulation and frameskip zero.

Native compiler and archive details: [NATIVE_ASSETS.md](NATIVE_ASSETS.md).

Summarize the completed-consumer capture separately from the quiet frame CSV:

```sh
python3 smstrikers-port/tools/analyze_vita_performance.py consumer-phases.csv \
  --out consumer-analysis.json
```

The analyzer removes repeated completed snapshots, reports skipped consumer
frames, validates monotonic cumulative counters and normalizes endpoint deltas
by the actual completed-frame interval. Producer/consumer waits are per-frame
values. CPU3 maxima include only valid telemetry samples and are sampled bounds,
not proof of utilization between samples. This report deliberately has no FPS
estimate; use `analyze_vita_frames.py` on diagnostics-OFF captures for that.
