# Vita hardware performance record - 2026-09-16

## Result and remaining gate

60 FPS gameplay and faithful final graphics have NOT been reached or verified.
There is a measured CPU improvement, a corrected shader compilation error, and
an opt-in GPU geometry prototype. Do not enable that prototype by default on the
strength of host tests or successful compilation.

The console later stopped before the match. The UVC feed showed the date/time
lock screen, and synthetic touch gestures did not close it. The same earlier
CPU-control executable, restored byte-for-byte with its original test settings,
also stopped in startup (last sample frame 239). This does not establish a
GPU-path regression. An unlocked, interactive console is required to continue
the comparison. No GPU-path 3D speedup or image-equivalence result is available.

## Reproducible measurements

Starting source revisions:

- Strikers: `29e9746a557378f2948d43f738c00c0d10f87338`
- Aurora: `fc69c1c3d5aa9e87adb35be6676c7a7fc003ba15`

The controlled color-path runs used seed 1337, a fixed timestep, one CPU lane,
cached source resources (32 MiB), split decode/transform instrumentation and
CPU geometry. CPU clock was reported as 333 MHz. GPU frequency was not verified;
a zero returned by one getter must not be reported as a GPU running at 0 MHz.

Matched frame samples, measured in microseconds:

| Frame | Triangles, both runs | Transform before | Transform after | Whole frame before | Whole frame after |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 909 | 60,585 | 94,996 | 68,295 | 451,437 | 426,708 |
| 919 | 60,640 | 95,313 | 69,232 | 409,668 | 384,921 |
| 929 | 60,806 | 95,817 | 69,021 | 469,088 | 447,802 |
| 939 | 60,956 | 96,466 | 68,981 | 454,816 | 426,507 |

Transform medians: **95.565 -> 69.001 ms**, approximately 28% less time in that
phase. Whole-frame medians: **453.1265 -> 426.6075 ms**. These are four matched
samples of the introduction scene, not a sustained gameplay benchmark.

Executable SHA-256 for those two runs:

- Before: `37a2605a8161d67ae0ce209397aaf14f9c25d9f754f22c5a06754a3b828e89d6`
- After: `ad43f14b130470bed1c14fe8aa363cf4f19777f9c1a00068801eae53cd63ccaa`

A later CPU-control run, with vitaGL CDRAM pool 64 MiB and texture cache 32 MiB,
produced the following six warm samples (frames 1119 through 1169, every ten):

| Field | Median |
| --- | ---: |
| Whole frame | 379.1175 ms |
| Vertex decode | 83.7245 ms |
| Vertex transform | 68.358 ms |
| Command build | 85.3675 ms |
| Vertex packing, nested in command build | 28.4295 ms |
| Buffer upload | 18.119 ms |
| Texture resolve | 5.7975 ms |
| EFB copy, may include submission | 3.0435 ms |
| Triangles | 61,309 |

The sample range was **377.805-420.851 ms/frame**, approximately 2.4-2.6 frames/s
of rendering throughput. It remains far from the 16.67 ms budget for 60 FPS.
This run changes multiple settings and uses a later scene window: do not assign
its entire difference to color quantization or to cached source RAM.

Paired CPU/GPU test binary:
`f39fbec817ba80f4ab5396979fffeb2a1c465684af30f53a6affcfda2651941e`.
Only `static_geometry_mb` changed between its CPU and GPU configurations. The
CPU run completed; the subsequent GPU run did not reach a valid 3D measurement.
A later repeat of the exact CPU binary/settings stopped before the match too.

The native CPU-control snapshot at heavy-frame 200 / Aurora frame 1098 showed
stadium geometry during the opening transition. Magenta regions remained even
with `shader_fail=0`; the graphics fidelity problem is not declared resolved.
The new bounded `palette_oob` diagnostic is intended to identify one possible
cause. A palette failure has not yet been confirmed by that new diagnostic.

## Default behavior and optional profiles

The new fixed-GPU geometry path and program-binary cache default to OFF. The
existing CDRAM source-resource allocation is also retained by default: cached
CPU source resources require an explicit memory budget and opt-in. Direct
mapped streaming stays OFF and the existing absolute U16 index path remains.
No flip or projection workaround is added to the normal rendering path.

For a new paired test, use the same built eboot on both runs. Place this
configuration in the game's data directory only for the test:

```ini
benchmark=5
benchmark_seconds=150
seed=1337
fixed_dt=1
gfx_resource_cached=1
gfx_resource_mb=32
aurora_cpu_workers=0
profile_vertex_phases=1
vgl_cdram_mb=64
texture_cache_mb=32
static_geometry_mb=0
shader_cache=0
vita_snapshot_3d_frame=200
```

Change only `static_geometry_mb=16` for the GPU variant. Validate shader output,
actual GPU cache hits, buffer lifetime and a newly written snapshot before
interpreting throughput. Test program-binary caching separately by changing
only `shader_cache`, with a cold and then a warm run.

The cached-memory opt-in is explicit in the current source. Older experimental
binaries used cached memory by default; their manifests and actual boot logs,
not a current default, define the historical measurements.

Optional CPU/GPU clock controls are exposed as `cpu_mhz=444` and `gpu_mhz=222`.
They were not used in the matched samples above. Split-phase profiling changes
cache locality and worker dispatch; turn it off for the final performance run,
but do not mix split and fused measurements without noting the difference.

## Test hygiene

- Verify `SMSVITA01` and read back the uploaded eboot before launching it. Keep
  the previous eboot and configuration. Do not write game assets, saves or
  plugin configuration as part of a binary deployment.
- Preserve VPK, SELF and ELF together, plus source revisions/diff and the actual
  configuration. An eboot alone is insufficient for reliable symbolization.
- Separate telemetry sessions using a recorded starting byte offset or a
  confirmed frame-counter reset. The telemetry file appends across launches.
- A previously generated PPM can remain on the console. Accept a snapshot only
  after the CURRENT runtime log confirms that this run wrote it. A stale image
  must never be used to claim CPU/GPU image equivalence.
- Capture UVC without taking a configuration lock from OBS. A local AVFoundation
  capture session can use the active `PSVita` format and sample frames without
  calling `lockForConfiguration`. Respect camera authorization and avoid
  opening the user's other cameras or capturing unrelated desktop content.
- Restore the original configuration when ending a test session. Do not leave
  automatic demo launch, fixed timestep or benchmark-driven exit enabled.

## Local evidence

Session artifacts are under `.tmp/perf-20260916/` in the source checkout, not
versioned with the code. Key evidence:

- `matched-color-samples.json`, `measured-samples.json`
- `profile-default-manifest.json`, `profile-default-2-telemetry.log`
- `profile-colors-manifest.json`, `profile-colors-end-telemetry.log`
- `fixed-control-manifest.json`, `fixed-control-final-telemetry.log`
- `fixed-control-frame200.ppm` / `.png`
- `fixed-gpu-final-runtime.log`, `known-control-retry-manifest.json`
- `known-control-retry-telemetry.log`, `uvc-warmed.png`
- `vita-verification.log`, `full-verification.log`, `build-final-probe.log`

The focused Vita tests passed **37/37**. The full host suite was **279/281**;
the two remaining failures are the previously observed
`FrameInterpolationContract.IndexedPaletteHistoryKeepsAbsoluteVertexSlots` and
`TevRegisterLivenessContract.PacksOneUniformWhenBothHalvesNeedInitialValue`.
The standalone Vita probe built successfully, with the existing linker ABI
warnings. Neither test result certifies the experimental GPU path on hardware.
