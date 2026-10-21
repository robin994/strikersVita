# Strikers Vita C9 - snapshot async ONLY checkpoint (21 October 2026)

## Scope and source

This checkpoint is based on reconstructed, hardware-benchmarked C9 source
(commit `512e9cc5a2fc044ba6ab5447d5cc4eb520631caa`) through the
isolated, host-tested async scheduling change
(`1e3623b3f3141c20f9c4d0795c8af1866e543e35`).

The Vita generated `strikers.ini` managed defaults select exactly this
experiment:

- `vita_async_snapshot = 1`: overlap warmed character snapshot grabs and
  blends on the existing native CPU0 job worker.
- `vita_glx_cache = 0`: the optional marker lookup cache remains disabled,
  even though its gated implementation exists in the parent source commit.
- `vita_frameskip = 0`: no render frame dropping.
- `diagnostics = 0`, `fps_overlay = 0`, `task_profile = 0`:
  no default-on packet, task, or frame-capture profiling.

The default profile revision is
`2026-10-21-c9-async-snapshot-only`. The Vita configuration loader
refreshes its managed block when the revision changes and preserves content
outside that block. Explicit environment variables can override the managed
defaults; verify the effective `strikers.ini` before interpreting any device
measurement.

The source changes in this branch consist only of this default profile,
its example INI, a release validation script and this handover. In particular,
the experimental video transcoding/AVPlayer and three-mode frameskip work
are **not** included.

## Clean, isolated build

The worktree is
`.tmp/strikers-c9-async-only-source-20261021` (relative to the repository
root). It does not alter the dirty root checkout or the protected C9 binary.

A clean CMake configure, then `--clean-first` Release build of
`strikers_vita.vpk-vpk`, produces the four requested artifacts in the
existing **isolated** `smstrikers-port/build-vita-async-delivery` directory
(no repeated full artifact sets under `ab-artifacts`):

- `eboot.bin` - extracted and verified from the VPK.
- `strikers_vita.vpk` - installable package for title `SMSVITA01`.
- `strikers_vita.self` - SELF used as VPK eboot.
- `strikers.velf` - Vita ELF, needed for debugging/symbol workflows.

After the build, execute:

```sh
python3 smstrikers-port/tools/test_c9_async_glx_integration.py
python3 smstrikers-port/tools/validate_c9_async_only_delivery.py
```

Run commands from the worktree. The delivery validator enforces the
source/default flags, Release non-profiling CMake cache, VPK CRC and resource
presence, exact `eboot.bin == SELF` bytes, and SHA-256 for every deliverable.
It writes `build-vita-async-delivery/snapshot-async-only-manifest.json`,
which is ignored by Git along with the binaries.

## Acceptance state and rollback

The async implementation had host sanitizer and VitaSDK syntax gates in
`C9_ASYNC_GLX_OPTIMIZATION_2026-10-21.md`. A **build success alone does not
establish hardware correctness or an FPS gain**. This checkpoint is a
source/package delivery, not a certified on-device regression pass.
Hardware acceptance remains pending: compare against reconstructed C9 using
equal clocks, 600 warmup + 300 play frames, and check snapshot/animation
equivalence, input, audio, goals, saving, FPS and p95/p99. The original C9
source ref `recovery/c9-hardware-27fps-20261020` remains the rollback.
Do not deploy, overwrite saves, or advance `main` as part of this build.
