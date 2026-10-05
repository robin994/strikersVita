# Native material/asset workflow candidate — 2026-10-05

## Current state

The user reported a **GPU crash in the latest build**. The exact dump
`psp2core-1791191504-GPUCRASH.psp2dmp` was downloaded and validated. It identifies
`SMSVITA01`; the installed SELF matches `0563a5c5…` and the module segment sizes
match its preserved ELF. GPU_INFO contains a BIF memory fault on page
`0x72e14000`, inside a 16 KiB `aurora-gxm` allocation. No CPU thread has an
exception stop reason. The GX thread was allocating a geometry buffer, but an
asynchronous GPU fault cannot be attributed to that CPU call alone.
The vertex-program refresh path has since been corrected: it retained stale CPU
and GPU draw recipes when XF channel/texgen state changed without a base-pipeline
generation change. A new transition test fails before the patch and passes after
it. The new SELF `be6d1a0e…` completed the play-frame-60 screenshot and a validated
1,200-frame live-play capture on Vita without a new dump. A second long run without
screenshot also completed 1,200 live-play frames: three sessions, 2,580 samples.
The original dump still cannot be causally tied to an individual draw;
the matched candidate/reference performance comparison remains pending.
See [GPU crash analysis](GPU_CRASH_ANALYSIS_2026-10-05.md) for evidence and limits.

The subsequent asset-pipeline build adds a verified stored PSARC backend and a
reusable host preparation tool, retaining that renderer fix. Its distinct SELF,
archive identity, source checks and hardware evidence are recorded in
[Asset pipeline status](ASSET_PIPELINE_STATUS_2026-10-05.md). Archive storage
savings must not be treated as a gameplay FPS improvement or as completion of
the pending native texture/geometry compiler.


The bounded source changes below pass local validation; device acceptance is
incomplete. In particular, a CDRAM geometry migration and the frontend routing
of exact CMPR/BC1 compression remain unimplemented.
The graphics candidate executed on Vita with verified CPU 444 / GPU 222 MHz.
Candidate/reference intro captures were pixel-identical. Initial 1,200-frame
captures exposed that renderer scene state includes the stadium introduction;
they are excluded from gameplay performance claims. A corrected collector uses
GS_GAMEPLAY/GS_OVERTIME and a monotonic live-play counter. The corrected candidate
was installed and byte-verified, then produced the reported GPU crash. No
live-play CSV or play-frame snapshot was generated for that original run. The
post-fix snapshot run measured 23.923 frame/s in its sampled live-play interval;
the second run without screenshot measured 24.932 frame/s.
No matched speedup or fullspeed result is established yet. LOD was not changed. The user
requested a source snapshot push with these device-validation caveats retained.

The request follows `PERFORMANCE_CPU_TRACE_2026-10-05.md`; those CPU changes remain
in this candidate. All results distinguish host checks, installed bytes, actual
executed graphics build and current live-play comparison.

## Original crash-producing source and artifact identity

- Root base at build time: `369c539dd7a5fc27f82e0a52c8b3df49773c8a40`, with the source changes in this delivery.
- Embedded Aurora base at build time: `910c4b89977652a8e3770c1f5e256fc87758db5a`, with the source changes in this delivery.
- Delivered Aurora source snapshot: `39fb53e9` on `origin/vita-experiment`; the parent repository records its full submodule commit.
- Build version: `1.3.0-native-workflow-20261005`.
- VPK SHA-256: `497d87ab0c7e784f4a673939c8dec0d920925f685d6881ab3bfd6bdf11eb73ef`.
- SELF / VPK eboot SHA-256: `0563a5c5e7fd9c12fdd8b3305859d71132778874f94bbe95ae5664145c0bd4c4`.
- ELF SHA-256: `91fd7ba1f77abd7230d3369d7d51ac8a0e1c7eb5931336649ec7b25b1f5e7b5e`.
- Original installed control SELF:
  `0aa49a3297c893167c5716ae6239473ca19878387d5b4d02ed337a67a712edd3`.
- Original INI: `7cb8317f6e1f77347d6cf3ba8bb3afb69eb251bffbc92678e6b00481109f8b50`.

Artifacts, build/test logs, device backups and session identities are under
`ab-artifacts/native-workflow-20261005/`. `manifest.json` describes provenance,
limitations and comparison configuration. The original INI was restored once
after the first interruption, then temporarily overridden for the current test.
After downloading the crash evidence, the original INI was restored and its
readback matched `7cb8317f…` (`config-restored-after-gpu-crash/`). The crash-producing
candidate binary was replaced by the post-fix build `1.3.0-gpu-fix-20261005`,
SELF `be6d1a0ebe17889181ff1de623c63e1864a81a9df3227be46d83f209ba9d643f`.
Its exact symbols, source patch and hardware sessions are under
`ab-artifacts/gpu-fix-20261005/`; see the GPU report for identities and test limits.
The original binary and INI remain backed up locally and on FTP. Build artifacts, dumps, logs and retail assets are local
ignored files and are not part of the source push.

## Adapting the Monster Hunter workflow

| Original item | Applicable Strikers implementation/evidence |
| --- | --- |
| Cortex-A9 benchmark on Raspberry Pi | Pi 3B can run AArch32 kernels. Portable real fixed-uniform reference/candidate benchmark added. Device game timing remains authoritative. SSH details and Pi measurements are pending. |
| FMA to VMLA | Native GCC already targets ARMv7/Cortex-A9/NEON/hard float. ELF audit finds VMLA and no VFMA or software FMA calls; no PPC floating-point recompiler to alter. |
| Remove software MMU / map mirrors | Game functions compile as native C/C++; the active port does not emulate PPC load/store through a software MMU. Guest asset byte order and memory revision tracking are required and retained. |
| Decompile hot CPU functions | Strikers already has native source. Trace-led vector copying, matrix wrapper and audio sample-view patches from the previous report are included. |
| Remove PPC register save/restore | Native functions already use the ARM calling convention. No PPC context register save/restore translator exists in this runtime. |
| Remove TEV dependency / simple fragment shaders | Arithmetic materials now lower to direct immutable expressions with constant specialization and SSA stage results. Compare/indirect effects keep the reference generator. Original GX callbacks/material state remain the compatibility input; this does not replace the entire rendering engine. |
| GXM texture preparation/compression | Explicit compact I8/IA8/RGB565 mip chains and exact CMPR-to-BC1 subset are supported. Unsupported/generated/NPOT chains or differing CMPR interpolation use RGBA8. This does not claim every game texture can be losslessly compressed to BC1. |
| Immutable scene objects in CDRAM | The existing 8 MiB resident geometry cache now reuses the prepared draw recipe. Source mutation and GPU retirement safeguards remain active. Its buffers still use `MemoryKind::CpuGpu` / uncached USER allocations; migration to the CDRAM resource pool remains pending. |

## Material implementation and coverage

`gxm_shader_gen.cpp` now emits arithmetic materials directly, preserving signed
accumulator D, wrapping of A/B/C, independent color/alpha destinations, bias,
scale, per-stage clamps and read-before-write behavior. Only literal zero/one
specialization is performed. Texture sampling, swaps, EFB copies, alpha test,
fog and pixel scissor share the existing implementation. Indirect/compare
materials retain the original source generator. Runtime shader compile failure
can fall back to the original arithmetic fragment generator.

The original on-device `pipeline_hot_v1.bin` contains 206 pipeline descriptions
and 44,082 recorded uses. All 206 descriptions qualify for the arithmetic
generator: **100% of that cache's recorded uses**, not all possible game states.
Generated fragment Cg text totals 669,988 → 553,754 bytes, about 17.35% smaller.
Source size is not a GPU time or FPS result; the existing Cg compiler can already
optimize parts of the reference expressions.

The independent host expression interpreter compares generated arithmetic to
separate scalar reference equations over randomized 1–16 stage programs,
register destinations, signed inputs, swaps, konst selectors, clamp transitions,
bias and scales: 4,052,432 checks passed. The legacy shader contract tests still
exercise the unchanged reference generator.

## Textures and resident geometry

Explicit compact mips are packed per-level in the established GXM Morton order;
single-level NPOT rows remain padded to eight texels. Generated and unsupported
mips retain the RGBA8 decoder. Texture mutation checks the hardware format as
well as dimensions, mip count, stride, layout and allocation size before reuse.

CMPR four-color interpolation uses GX's 5/8–3/8 weights; BC1 uses 2/3–1/3.
Transparent selectors can also retain different RGB, affecting filtered edges.
The new BC1 admission check rejects differing selected texels. Compression is
single-level, aligned, immutable and conservative. Broad lossy compression or
an offline rewrite of all disc texture bundles has **not** been performed. The
prototype log does not establish texture savings for the tested match. A later
source audit found that `TextureCache::get_or_upload` clears `nativeDesc.cacheable`
because the facade owns retirement, while exact CMPR admission requires it.
The new BC1 converter therefore remains unreachable through that frontend;
its host tests establish decoder equivalence only. Carrying immutable-source
eligibility separately from cache ownership is still pending.

Texture host tests compare each compact texel and every explicit mip to the
independent RGBA8 decoder, including rectangular/1D/small/NPOT cases, truncated
data, excessive mip counts, generated levels and unsafe BC1 inputs: 37,692 checks
passed. Prepared geometry tests verify the same resident object over 1,000
camera changes, invalidate it after a tracked source write and release storage.
The 8 MiB geometry budget and BeginScene context reset are unchanged.

## Clock and measurement fixes

The original INI requested `cpu_mhz=500`, but `main.cpp` only applied `444`; the
executed prototype logged CPU=333. The final candidate validates supported
requests, attempts the configured clock and reports the returned value. If the
kernel rejects 500 or reports another value, it attempts 444 and reports that
fallback. The executed graphics build verified the 500 request was rejected and the
fallback produced CPU=444; GPU=222, bus=222 and xbar=166 MHz. New CSV headers
query these frequencies again for the corrected collector.

The old benchmark CSV code is disabled by `#if 0`; old benchmark keys were not
a current acquisition mechanism. The new opt-in finite collector is independent
of diagnostics/FPS overlay and buffers a bounded sample in RAM. It writes once
after the sample. It identifies gameplay frames and supports a warmup skip.
CSV headers record actual Vita clock queries. CPU tasks and present phases are
not GPU execution measurements. I/O after the sample and screenshot sessions
must be excluded from performance comparisons.

The prototype CSV was malformed because Vita's small newlib printf does not
support `%zu`. The final collector uses `%lu` with an explicit cast. The strict
analyzer rejects malformed/nonconsecutive/incomplete rows and phase overlap.
**No prototype CSV is used as performance evidence.**

## Local validation

- Port host suite: 18/18 passed.
- Aurora host suite: 16/16 passed.
- ASan/UBSan: native material, compact texture and prepared geometry tests passed.
- ASan/UBSan finite frame collector test passed.
- Standalone `vita-gxm` cross-build passed.
- Full game VPK cross-build passed; packaged eboot equals SELF byte-for-byte.
- Native GXM binary boundary audit passed; 12,685 executable symbols inspected in the final collector build.
- Root and embedded-Aurora whitespace/diff checks passed.

Warnings include existing SDK/wchar/enum ABI, deprecated robin_hood builtins and
serial LTO notices. The build did not introduce an OpenGL renderer dependency.

## Hardware evidence and limitation

Executed prototype SELF:
`49f0155184b691734b5010d02b66062f98835f1d7c21394fc8b84d7b862ffdce`.
It used diagnostics=1 and CPU=333, compiled native fragments without a logged
`stage_compile_fail`, and wrote
`candidate-visual/debug_frame_3d_120.ppm` / `.png`. This shows the real stadium
and intro on device, but is not a matched visual parity or gameplay stability
test. Do not use this screenshot as a final SELF execution marker.

The graphics build SELF `5246625e72b62b571d7a050fadca93965dfd5375c83cf20438191ad466d819b2`
executed both native and reference materials. Its intro captures at renderer
frame 120 have zero differing pixels (960×544 RGB). Native run: 220 material
records, all native; reference: 217, all reference. Both logs have zero shader
compile failures. These are intro evidence, not full visual-gameplay coverage.

For 215 common compiled fragments, reference GXP totals 273,036 bytes and native
GXP 273,420; 158 have equal size, 21 shrink and 36 grow. The shorter Cg does not
establish simpler machine code or a GPU performance benefit.

The first diagnostic-free capture was interrupted when System12Vita replaced
Strikers. Later candidate/reference files each contain 1,200 rows, but a renderer
scene counter reset mixes the stadium intro and later match scene. The strict
analyzer rejected them; `excluded-mixed-captures.json` records why they are not
live-play performance evidence. No figures from them establish FPS or speedup.

The corrected collector filters directly through `cGame::IsGameplayOrOvertime()`.
`frame_capture_play_only=1` excludes pre-game, kickoff, replay/post-goal and
loading states; its live-play counter does not reset at renderer transitions.
CSV metadata identifies `play_only=1 counter=live_play`. The analyzer's
`--require-live-play` gate rejects older broad scene captures. The host test
covers intro exclusion, warmup, replay gaps and scene counter reset.

A subsequent FTP transfer failed with `550 Could not allocate memory` before
promotion. The installed original graphics SELF remained intact. The service
became unavailable, then recovered after a reboot request. The corrected SELF
was then installed and read back byte-for-byte (`install-live-play/`). A fresh
`play-candidate-1` session launch was requested with live-play filtering and a play-frame
60 screenshot during warmup. The device subsequently stopped responding on
FTP/command ports and the USB camera disappeared. The user was asked to confirm
power, USB and Wi-Fi state. No CSV or image from this final SELF has yet been
retrieved at that point. Connectivity subsequently recovered and the exact dump
was downloaded. It confirms a GPU crash in Strikers. The final live-play CSV
and play-frame image are absent on FTP. Transfer success alone is not runtime
proof. The original INI has now been restored with verified readback.

## Current hardware protocol

The following comparison is pending resolution of the GPU memory fault; it has
not been completed on the crash-producing build.

1. Keep installed SELF hash, effective INI and queried clocks with each run.
2. Candidate/control/candidate use the same final binary, diagnostics=0,
   overlay=0, WARM caches, seed `0x53545249`, fixed timestep 16.666666667 ms,
   existing skipfe AI Mario/Luigi friendly in Peach stadium and active audio.
3. Skip 600 **live-play** frames and record 1,200; masks `0x8` / `0x60008` / `0x8`.
   Capture play-frame 60 during warmup, then disable every further readback.
   The image/I/O frame is outside the timed sample, with 540 live-play frames
   remaining before recording. Shader cold loading and cinematic intros are
   excluded by the play-state filter.
4. Fetch unique CSV, effective INI, image and installed eboot before the next
   launch. Use `analyze_vita_frames.py --expected 1200 --require-live-play`.
   Compare median/p95/p99, measured-frame elapsed rate and deadline percentages.
   This compares fragment/texture paths, not the prior audio/vector patches or
   the clock correction. Rate is over eligible gameplay frame durations;
   gaps for cinematics/menus do not count toward the gameplay rate.
5. Test front-end navigation and PS suspend/resume, then restore the original
   INI. Keep the original SELF backup available for renderer regressions.

Reference bits, set before renderer initialization:

- `0x10000`: rebuild/clear the fixed uniform payload, previous CPU patch control.
- `0x20000`: reference fragment generator.
- `0x40000`: reference RGBA8 for new compact mip/CMPR preparation.
- Existing `0x8`: fixed snapshot sharing OFF; the incremental builder still runs.

## Pi 3B benchmark

The Pi 3B Cortex-A53 supports AArch32 Cortex-A9 code. Its cache, instruction
latency, memory subsystem and Linux scheduler differ from Vita, so a kernel
speedup is not a console frame-rate estimate. Use a 32-bit Linux compiler and
runtime; a default 64-bit compiler produces a different ABI. Do not run the
Vita SELF as a Linux executable.

The portable target is `aurora_vita_fixed_builder_bench`, enabled by the host
test configuration. It runs the real reference and incremental builder with
identical inputs, nine alternating trials and an escaping full payload. For a
32-bit Raspberry Pi OS/compiler, configure host tests with C/C++ flags
`-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard`, build that target and run its Linux
executable. Record compiler, pointer bits, architecture, clock/thermal state and
result JSON. A Mac smoke-run is saved as `mac-microbench-only.json`; it is not Pi
or Vita performance evidence. No SSH host/user has been provided for the Pi.
