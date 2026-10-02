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
