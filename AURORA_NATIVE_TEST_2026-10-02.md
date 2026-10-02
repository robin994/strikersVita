# Strikers Vita — Aurora Vita native test build

## Source and scope

The new upstream Aurora implementation is `daee62e657ffcad3e8a79345859ef1df26772d7d`
on `vita-experiment`. AVN-00–08 are implemented; AVN-09–11 deliver the decision
and service/retirement specifications. Hardware promotion is still open.

Strikers has additional CPU3 quota, fixed-uniform pool, geometry preflight and
runtime diagnostic work. The embedded submodule preserves that work and merges
the new upstream implementation at `db53dea1` on
`codex/strikers-vita-native-20261002`; do not replace it with upstream alone.
The integration preserves physical helper IDs, sparse CPU3 probing, quota
admission and the vertex-only dynamic queue. Distinct filtering applies to
both static partitioning and dynamic CPU3 jobs, before any wake. Unknown caller
CPU falls back to the caller. The fixed-uniform pool retains its budgeted slots;
its new scratch/publish interface acquires a slot only for a changed snapshot.
Snapshots remain valid until execution/reset, and revisions increase across resets.

The Strikers diagnostic changes already present in the checkout are included:
`diagnostics=0` suppresses optional logs and timers while preserving CPU3 safety
measurements and renderer/cache behavior. The lightweight FPS overlay remains
independently selectable. See [the diagnostic protocol](smstrikers-port/tools/VITA_DIAGNOSTICS.md).

## Rebuild

```sh
git submodule update --init smstrikers-port/extern/aurora-vita
JOBS=8 smstrikers-port/build-vita-aurora-native.sh
```

Version string: `1.3.0-aurora-native-20261002`. Package title ID stays `SMSVITA01`,
SFO version `01.00`; the code version and artifact hash identify this test build.
The script uses the current embedded checkout, Release/VitaSDK/GameCube ABI,
GXM, direct stream write/submit ON, distinct CPU cores and immediate draw view
ON. Audio worker ON; GX worker OFF retains the synchronous Strikers baseline.
This is deliberately different from Aurora's standalone async candidate preset.
Normal renderer/game caps stay three lanes; existing CPU3 quota admission stays
vertex-only and must still verify CPU3 on the console. Native GX textures stay
ON as in the previous Strikers build; CMPR remains OFF. Internal render size is
960x544 (GameCube native resolution OFF), presentation pacing 60 Hz. These are
compiled/default settings, not observed device settings.

Shader profile default is SEALED; INI can select WARM. Static geometry default
8 MiB. Runtime log support is present. `NATIVE_CANDIDATE=OFF` rebuilds with both
new options OFF; the same binary also accepts the runtime controls below.
The script audits native ELF/map isolation and checks SELF against packaged
eboot while producing `build-vita-gxm-latest/vita-native-manifest.json`.

## Matched runtime comparison

Use the new [control](smstrikers-port/configs/vita/gxm-vita-native-control.ini)
and [candidate](smstrikers-port/configs/vita/gxm-vita-native-candidate.ini) on
the same SELF, restarting after each INI change. They preserve the shared-state
baseline mask `0x8`, batching OFF, 444/222 MHz requests, WARM shader profile,
CPU3 auto/50% total target/5% guard and diagnostics/FPS ON. Only the two new
flags differ:

```ini
aurora_distinct_cpu_cores = 1
gxm_immediate_draw_view = 1
gxm_local_draw_batching = 0
```

Setting both to 0 selects the candidate controls; compare them individually
as well for attribution. Immediate draw views apply only to direct streamed
draws. Local batching must stay OFF to exercise this path. Mask `0x8` disables
fixed snapshot reuse; test AVN-05 separately by using `gxm_disable=0` in both
profiles. Do not change other experiments during a comparison.

No new device installation, read-back, screenshots, FPS or latency measurements
were performed for this build. Compare the same match/scene/input after warmup,
including EFB/shadows, transient geometry and suspend/resume. Record installed
eboot and INI hashes. Do not sum task/view/GX timings; batching can move work to
swap_post without reducing enclosing frame cost.

## Local evidence and delivery

The integrated Aurora host suite passes 12/12, including new pooled snapshot
publication (10000 duplicate publishes per storage budget) and distinct CPU2
caller with sparse helper/CPU3 quota checks. The isolated Strikers host suite
passes 12/12, including diagnostics/FPS, configuration preservation and game
skin matrix packet equivalence. Sanitizer and final binary results are recorded
in the ignored delivery bundle after the build completes.

Build outputs: `smstrikers-port/build-vita-gxm-latest/strikers_vita.vpk` and
`strikers_vita.self`. The previous outputs were preserved under
`ab-artifacts/aurora-native-20261002/previous/`. The new delivery bundle under
`ab-artifacts/aurora-native-20261002/` holds exact revisions, cache, ELF/SELF/VPK
hashes and test/audit logs; generated artifacts and logs are excluded from Git.
