# Strikers Vita: latest Aurora and experimental packet state

The direct checkout on `main` contains the Strikers CPU3, retirement, uniform
pool, geometry preflight and profiling work checkpointed in `7ed33c74`.
The embedded Aurora branch is `perf/strikers-cpu3-retire-20260929`; its
`a4989df5` merge already contains the latest fetched `origin/vita-experiment`
commit `ff5b2cf5` (GX state translation and GXM submission changes).
Do not replace it with the upstream branch and lose the Strikers worker work.

This integration exposes Aurora's `gxm_local_draw_batching` through
`STRIKERS_GXM_LOCAL_DRAW_BATCHING` / `strikers.ini`. The default remains OFF.
The user selected shared immutable packet state and local batching for the
first hardware comparison. No GX display-list bytes are changed.

## Matched profiles

| Setting | Control | Experimental |
| --- | --- | --- |
| `gxm_disable` | `0x18` | `0x8` |
| `gxm_local_draw_batching` | `0` | `1` |
| `gxm_streamed_vertex_gpu` | `0` | `0` |
| `gxm_dl_shadow` | `0` | `0` |
| `gxm_bp_cache` | `0` | `0` |
| `gxm_fragment_prepare_cache` | `0` | `0` |
| `vita_skin_packets` | `0` | `0` |
| Uniform pool / geometry preflight | ON | ON |
| CPU / GPU clock | 444 / 222 MHz | 444 / 222 MHz |
| CPU3 | vertex only, auto, total target 50%, guard 5% | same |
| Packet profiling | buffered, one frame in 16 | same |

The complete INI files are in `smstrikers-port/configs/vita/`. Preserve other
device-specific settings when applying their values to the installed INI.
Switch profiles by replacing `ux0:/data/strikersVita/strikers.ini` and restarting
`SMSVITA01`; no rebuild is needed for the control fallback.

Aurora couples producer state sharing to native fragment-uniform reuse:
`GxmDisableUniformRevision` (`0x10`) disables both. The experimental profile
must remove that bit to exercise shared state. It retains `0x8` so the existing
fixed snapshot optimization remains disabled. The control is the same updated
binary with the previous sharing/uniform policy; it is not the older SELF.
GX state domain translation and native setter differences are enabled in both.

The four additional Aurora reference controls are `0x1000` (state domains),
`0x2000` (native state differences), `0x4000` (local merge), and `0x8000`
(shared packet state), together `0xF000`. The existing `0x0800` still disables
the single-draw display-list bypass. The upstream refactor document previously
listed the wrong offsets; this integration corrects documentation only.

## What the experiment can change

Immutable packet state retains uniform and texture snapshots until command
execution. Local merging only handles adjacent compatible streamed triangle
lists with contiguous live stream storage, valid indices and identical state.
It cannot reorder across EFB copies, clears, target changes or barriers.
Fixed geometry cache hits alone do not establish that these draws can merge.
Measure a hardware gain; enabling the flag does not prove one.

`[native-state-profile]` in `task_profile.log` reports setter calls, skipped
setters and uniform uploads for one completed renderer frame at each task
report. These are single-frame counts, not 120-frame totals or GPU execution
timings. It uses Aurora's published completed snapshot without draining the
worker. Existing task/view and cumulative worker/cache measurements retain
their previous semantics. The lightweight profiles do not enable Aurora's
per-draw phase telemetry; zero batch counters in that mode would be unmeasured.

## Build and hardware comparison

Use `smstrikers-port/build-vita-native-gxm.sh`: Release, native GXM, direct
stream write/submit ON, audio worker ON, GX FIFO worker OFF, 60 Hz pacing ON.
The user's request on 2026-10-02 explicitly authorizes this rebuild. The
permanent preference against unsolicited Vita builds still applies afterward.

Before promotion, retain the previous SELF, INI and available logs. Audit the
unstripped ELF and linker map with Aurora's `tools/check_vita_gxm_binary.py`;
check that VPK `eboot.bin` equals the built SELF and verify FTP content before
renaming the candidate into place. Keep binaries, device logs and backups
under ignored `ab-artifacts/`, outside the commits.

Compare the same stadium, teams, camera and gameplay phase after shader warmup,
for at least 1-2 minutes per profile. Keep SELF hash, INI hash, log offsets and
session identity with each capture. Exclude initialization, loading, backend
restarts, pause and heavily diagnostic samples from steady gameplay comparisons.
Do not sum nested task/view/GX times or infer frame pacing from aggregate means.

The previous stable `260f917a` gameplay capture had five steady 120-frame
windows: median Characters 14.193 ms, Shadowed 16.356 ms and `send_views`
38.078 ms. Sampled callback work was dominated by draw submission: Characters
11.903 ms / 82.6%, Shadowed 12.955 ms / 75.7%. These historical measurements
motivate the experiment; they are not evidence for the new SELF or stable 60 FPS.

Validation and delivery artifacts for this integration are recorded under
`ab-artifacts/aurora-update-20261002/`. Hardware visual correctness and
performance must be recorded separately after the rebuilt SELF is run.

Integration validation: Aurora host CTest 11/11 PASS; Strikers host CTest
10/10 PASS; Vita compiler syntax checks for `main.cpp` and `nlTask.cpp` PASS;
native GXM Release build and VPK packaging PASS. The ELF/map audit inspected
12,643 executable symbols and found native GXM draw/present with no GL, vgl,
vita2d entry points or libraries. The VPK SELF matches the standalone SELF.
Both repositories pass `git diff --check`. Full hashes and build options are
in the ignored artifact manifest; commit/push and device read-back records
are kept there separately from visual correctness and FPS evidence.

## First hardware result

The installed SELF is
`f294e41140a336a4be3fcee4df6031cbe6a9e14d02fb4612fc65dea0e0b53922`,
built from Strikers code commit `19e7fd2e` and Aurora `7b3ee782`.
FTP read-back verified the exact binary and INI contents. The startup and
gameplay log confirms mask `0x8`, runtime features `0xf5` (local batching ON,
streamed fixed vertex GPU OFF) and packet sampler period 16. The user confirmed
stable characters and shadows during a match, with no reported flashing,
missing geometry or crash.

The 2026-10-02 08:22:42 capture contains 50 complete 120-frame report windows,
including 40 active gameplay windows / 4,800 frames. Applying the same prior
filter (both views active and sampled, fixed game update >=2 ms, geometry
misses plus fallbacks <=50 per window) selects 21 windows / 2,520 frames.

| CPU elapsed measurement | Previous SELF `260f917a` | Updated SELF `f294e411` | Observed difference |
| --- | ---: | ---: | ---: |
| Characters | 14.193 ms | 9.318 ms | -34.3% |
| Shadowed | 16.356 ms | 15.318 ms | -6.3% |
| `send_views` | 38.078 ms | 31.406 ms | -17.5% |

These are medians of 120-frame window means after the stated cache filter,
not per-frame percentiles. The entire active sample is retained as well:
Characters median 9.508 ms, Shadowed 15.505 ms, `send_views` 32.269 ms.
The selected geometry hit rate is 99.9876%. CPU3 total-use samples range
10.69-35.14% in the selected windows and 10.69-48.30% over all active windows;
this is observed utilization, not proof of a hard cap.

Within 157 sampled frames, Characters draw callbacks average 6.853 ms with
110.45 calls/frame, versus the earlier 11.903 ms with 114.13 calls/frame.
Shadowed draw callbacks average 11.637 ms with 114.72 calls/frame, versus
12.955 ms with 111.28 calls/frame. Draw submission still dominates sampled
callback time (74.0% and 73.1%). Vertex worker wall time is broadly unchanged
(3.025 ms/frame versus 2.933 ms/frame in the previous selected sample).

This is a preliminary reduction in the view-submission timers relative to a
historical sample; it does not establish lower total render cost or higher FPS.
The deferred submission correction below supersedes the initial interpretation.
The stadium, teams, camera and exact phase were not logged for a matched A/B, and
the code revision and two coupled experimental controls changed together.
The cache filter excludes cold geometry and many spikes. No improvement can
yet be attributed specifically to batching or state sharing, and light
telemetry does not establish how many draws actually merged.

Stable 60 FPS is not reached or established: `send_views` alone still exceeds
the 16.67 ms whole-frame budget. The next causal comparison is the same new
SELF with `gxm_disable=0x18` and `gxm_local_draw_batching=0` in the same match
conditions, followed by restoring the experimental profile. Do not sum
nested measurements or infer actual FPS from these window averages.

The immutable capture, analysis, full selected-window list and comparison are
in `ab-artifacts/aurora-update-20261002/gameplay-20261002-082242/`.

## Correction: deferred submission explains the low FPS

After the user reported little perceived FPS improvement, the enclosing
`End Frame` task and `swap_post` were checked on the same samples:

| Median of window means | Previous reference | Initial new sample | Extended new sample |
| --- | ---: | ---: | ---: |
| `send_views` | 38.078 ms | 31.406 ms | 31.957 ms |
| `swap_post` | 0.019 ms | 6.709 ms | 6.712 ms |
| `End Frame` task | 38.768 ms | 38.131 ms | 38.731 ms |
| Game Fixed Update | 11.752 ms | 11.634 ms | 11.956 ms |

The initial 6.672 ms reduction inside `send_views` is almost entirely offset by
6.690 ms added to `swap_post`. The enclosing `End Frame` task barely changes;
in the extended sample its median is essentially the same as the historical
reference. These medians must not be added as if they were a single frame.
`End Frame` already includes view submission and the post phase.

The code explains a real change in where work is measured:

- With local batching enabled, the `AURORA_VITA_GXM_DIRECT_DRAW_SUBMIT` path
  calls `queueStreamed()` instead of immediately calling `renderer_->draw()`.
- `DrawSink::flush()` eventually executes the retained command stream.
- `glxSwapPost()` calls `GXCopyDisp()` in both swap modes. The native
  `vita_copy_disp_task()` flushes the DrawSink before processing the display
  copy, so pending draw execution is charged to `swap_post`.

Consequently, smaller Characters/Shadowed or `send_views` timers can reflect
deferred GXM submission rather than removed work. The post phase also includes
copy/synchronization bookkeeping; these logs do not separately time every
component or establish GPU execution time. No substantial throughput gain or
average FPS improvement has been demonstrated by the earlier table.

The extended read-only capture at 08:40:21 verified the same SELF and INI hashes.
It contains 102 complete windows, 57 active gameplay windows / 6,840 frames,
and 32 selected cache-stable windows / 3,840 frames under the previous filter.
Later complete windows are menu work and are excluded from the gameplay set.
Some selected windows also contain multi-second stalls; their cause is not
established and the full records are retained. There is no per-frame wall-time
CSV for this binary: the old benchmark recorder is compiled out. The overlay
uses the live complete-frame timer, but those rolling samples are not in the
task log. An exact average FPS or p95/p99 cannot be recovered from these phase
averages alone.

The next isolated comparison should keep the same SELF and `gxm_disable=0x8`,
changing only `gxm_local_draw_batching` from 1 to 0. This retains shared state
and native uniform reuse while restoring direct streamed submission. The
prepared profile is `smstrikers-port/configs/vita/gxm-shared-state-only.ini`.
No rebuild is needed. Compare complete-frame pacing and `End Frame`, with
`send_views` and `swap_post` as attribution details, in the same match conditions.
The previous `0x18` / batching OFF preset remains the full fallback, but changes
two experimental policies and therefore does not isolate batching.

Evidence is in `ab-artifacts/aurora-update-20261002/fps-audit-20261002-084014/`,
especially `frame-phase-audit.json`. This audit changes no device settings and
requires no additional Vita build.
