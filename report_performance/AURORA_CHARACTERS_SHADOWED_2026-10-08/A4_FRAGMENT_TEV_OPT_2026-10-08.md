# A4 - Native TEV fragment specialization (2026-10-08)

## Scope and motivation

The 2026-10-08 hardware A0/A1c capture shows:

| Render view | Dominant reference fragment hash | Draw/frame | TEV stages | Textures |
|---|---|---:|---:|---:|
| CHARACTERS | `bb9a168934f9e9dc` | 79.0 | 4 | 3 |
| SHADOWED | `d35dfa69a3f7e9bc` | 40.4 | 2 | 2 |
| SHADOWED | `e2b27aa4eb05ca46` | 20.0 | 5 | 4 |
| SHADOWED | `670925ff2d92a258` | 19.0 | 5 | 4 |

A TEV stage count larger than the number of textures **does not prove**
redundant samplings: the same texture may be sampled at different UVs.
The compiled shader generator must inspect exact texture-unit/TEV-coordinate
pairs, and later hardware instrumentation must report how many instructions
the compiler really saves. No GPU execution time is known from A0/A1c.

The experimental optimizer is **GXM native material only**. The original
generator remains the reference and is returned unchanged with mask=0.

## Independent toggles

`strikers.ini` key: **`gxm_a4_fragment_opt`**, default **0**.

| Value | Meaning |
|---|---|
| 0 | Identical original TEV native Cg code generator; reference |
| 1 | Share samples only when *both* texture unit and TEV coordinate match |
| 2 | Backwards liveness: remove TEV RGB and alpha register channels whose results have no dependency path to final RGB/alpha |
| 3 | Both transformations |

### Exact semantics of fetch reuse (mask bit 1)

- For the same input texture unit and TEV coordinate index, the resulting
  `tex2D` sample is cached in a local fragment variable.
- Texture UV transformations, native-vs-manual wrap, EFB copy-format conversions
  (R4, A8, I4, IA4, RA4, etc.) and forced alpha opacity run **once** on the
  initial fetch. Later uses retrieve the fully converted raw texel.
- Each later stage still applies **its own TEV swap table** to the cached
  texel. Stage-specific swap operations are not conflated.
- Different TEV coordinates or different texture units never share samples.
  There is no sampling across pixels, draw calls, frames or dynamic texture
  uploads. No vertex shader, geometry, EFB target or sampler state changes.

### Exact semantics of dead-channel pruning (mask bit 2)

- For native arithmetic TEV, the compiler walks the GX register graph backwards
  from **both** final destination registers (RGB and alpha), before alpha test.
- Every stage can write RGB and alpha to *independent* registers. A dependency
  on RGB from a previous stage and a dependency on alpha-to-RGB are tracked
  independently. Any observable channel is left untouched.
- Only a register component with no path to final RGB or alpha may be omitted.
  When both components are dead, the entire non-observable stage is omitted.
- Every retained TEV arithmetic expression is **byte-for-byte unchanged**:
  no reassociation, new approximation, clamp/bias/scale modification or loss
  of signed accumulator wrapping. Alpha-test predicate, GX destination alpha,
  fog, depth, blending, and scissor generation are unchanged.
- Native unsupported features (indirect TEV, comparison TEV and alpha bump)
  use the existing fallback generator without optimization.

### GXP compilation and fallback

- The cache key for each GXP binary is a hash of the **actual generated Cg
  source**. Different A4 masks naturally create different GXP identities.
  Original binaries remain in the cache and are never overwritten by A4.
- If VitaShaRK cannot compile a new fragment program, or the runtime cache
  is sealed and lacks that program, Aurora tries the original native Cg/GXP
  for that pipeline (and for the scissor-free variant).
- The original supported non-native fallback is still present if original
  native material compilation fails during a writable-cache run.
- Diagnostics=1 logs map `reference_frag` (the pre-A4 hash from prior A0
  traces) to `actual_frag` and record compile-time `reused_fetches`,
  `removed_rgb`, `removed_alpha`, `removed_stages` and GXP byte count.
  An attempted optimization that falls back shows no retained A4 savings.

Compile-time counters describe removed **Cg work**, not hardware GPU cycles:
VitaShaRK may already eliminate some operations. FPS gains are *unproven*.

## Source files

- `smstrikers-port/extern/aurora-vita/platforms/vita/gxm/gxm_tev_opt.hpp`:
  independent TEV dependency and texture-sample planning.
- `.../gxm/gxm_shader_gen.cpp/.hpp`: native Cg emission with mask 0/1/2/3.
- `.../gxm/gxm_renderer.cpp/.hpp`: GXP selection, fallback and diagnostic
  source-hash mapping.
- `.../gxm/gxm_facade.cpp`, `.../gfx/vita_renderer.hpp`,
  `.../aurora_vita_backend.cpp/.hpp`: config propagation.
- `smstrikers-port/src/Game/main.cpp`, `src/platform/config.c`,
  `strikers.ini.example`: Strikers opt-in configuration.

## Automated correctness tests

1. `vita_tev_opt_test`: 16,000 randomized multi-stage register graphs,
   three initial states each (**48,000 symbolic complete-versus-pruned
   evaluations**) with cross-channel dependencies and randomized register
   overwrites; explicit alpha-to-RGB edge; sampler index/coordinate guards.
2. `vita_native_material`: 320 randomized valid native TEV programs with
   all three candidate masks (960 reference-versus-candidate comparisons);
   all retained RGB and alpha expressions are text-identical to the original;
   scissor and alpha behavior, code masks, vertex source and discardAll agree.
3. Same native test checks **56** combinations of EFB texture copy mode,
   force-opaque alpha and hardware wrapping; the converted fetch is cached
   only after conversion and is used by the later TEV swap.
4. Existing independent scalar TEV arithmetic oracle, alpha predicate
   truth tables, host backend and GX regression suites.

These do not compile the generated Cg on a real Vita GPU. The device
compilation, shader cache, picture/depth correctness and quiet FPS remain
required validation gates.

### Verified local test results

- Aurora `vita-host-tests`: **23/23 PASS** (including `vita_tev_opt` and
  `vita_native_material` with the expanded A4 cases).
- Symbolic TEV planner built and run with macOS **ASan/UBSan**: PASS, with
  `ASAN_OPTIONS=detect_leaks=0` (leak detection is unsupported there).
- `vita_default_config_test.c`: PASS; new key defaults to zero and preserves
  existing first-value-wins overrides.
- `test_vita_view_draw.py`: **12/12 PASS**;
  `test_vita_performance_analysis.py`: **2/2 PASS**.
- Root and Aurora submodule `git diff --check`: PASS.
- Vita-target compilation, VitaShaRK optimized GXP compilation, GPU correctness,
  frame-time/FPS comparisons: **NOT YET EXECUTED**; no GPU gain is claimed.

## Manual build and A/B experiment (user runs build)

```sh
cd /Users/robin994/Documents/Code/strikersVita/smstrikers-port
STRIKERS_GX_THREAD=ON ./build-vita-native-gxm.sh
```

**First install with all experiments OFF.** The currently installed A3 test
was previously reported to have `gxm_uniform_delta_upload=1`; for an A4
isolation run reset it to 0. Use the same SELF for all A/B runs, and preserve
`gxm_disable=0x8`, `gxm_streamed_vertex_gpu=0`, `vita_frameskip=0`,
`fixed_dt=0`. All SHADOWED/CHARACTERS views and shader toggles ON.

Baseline/quiet run:

```ini
gxm_uniform_delta_upload=0
gxm_a4_fragment_opt=0
vita_view_draw_capture=0
vita_view_draw_payloads=0
diagnostics=0
vita_frameskip=0
fixed_dt=0
gxm_streamed_vertex_gpu=0
```

For the three candidate variants change *only* `gxm_a4_fragment_opt` to
**1**, then **2**, then **3**, restarting Strikers each time because Cg programs
are generated at pipeline creation. Ignore warmup/cold shader compilation;
warm up for at least 600 frames and compare at least 1,200 steady gameplay
frames in each run. Repeat interleaved baseline/candidate three times in
the same match/scenario. Compare FPS, frame-time median/P95, stutter, scene
correctness, players, shadows, depth, alpha/transparent HUD and goal/replay.

In separate short diagnostic runs (never for FPS), set `diagnostics=1` and
inspect the A4 mapping log for actual savings in the four reference hashes
listed above, then compare GXP binary sizes and optionally A1c hashes
(`--require-payloads`, exact capture identity and matching gameplay).
Recheck warm-cache fallback: a missing candidate GXP must not black-screen
or corrupt characters.

## State

A4 implementation and host regression suite are ready for **manual Vita build
and hardware validation**. No shader or view has been disabled; no GPU FPS
benefit is claimed. No automatic Vita build, installation or push is performed.

## First live A4 observation (2026-10-08, approximately 19:30 Europe/Rome)

- User reports rendering correct and ~**25 FPS in gameplay** with
  `gxm_a4_fragment_opt=3`, not a clear improvement over ~22-25 FPS from earlier
  runs. This is subjective live observation, **not** a controlled A/B result.
- Verified on console over FTP: `strikers.ini` first effective A4 setting is
  `gxm_a4_fragment_opt=3`, `gxm_uniform_delta_upload=0`, `gxm_disable=0x8`;
  frameskip, fixed dt and A0/A1c capture flags are all zero.
- Installed SELF SHA-256:
  `e5eb2eef9a3f133864424de656c4a5650a34450600506dbcdafe7ff2868c0335`.
- GXP cache currently contains **417 files** vs **280** in the captured A0/A1c
  baseline. All 280 earlier objects remain; of the 137 additions, 130 are
  fragment GXP binaries, with 96 newly created fragment GXP entries after
  `2026-10-08T17:24:00Z`. New source variants are evidently being generated;
  this does **not** yet attribute any of them to the four hot materials or
  prove that the optimized fragment was bound in live gameplay.
- `native08-diagnostic.csv` on the console still records 300 samples with
  `match=0` (menu); **no valid gameplay frame-time baseline is available**.
  Diagnostics are 0, so A4 source/reference-to-actual GXP mapping is not
  emitted. The user-reported 25 FPS must not be compared against the menu CSV.
- **Decision:** keep A4 as an opt-in candidate pending paired 0-versus-3
  gameplay captures (1200 frames after 600 warmup, match-only and play-only,
  same SELF, seed, view masks, GPU/CPU frequencies). Then run a separate short
  diagnostic capture to map the four original GXP hashes to the specialized
  ones and determine which instructions/fetches actually disappeared.
- No hardware configuration was modified and no build/install/push occurred
  during this investigation.
