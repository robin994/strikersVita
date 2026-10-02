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
