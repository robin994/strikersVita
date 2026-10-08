# A3 - Indexed-PN vertex uniform delta experiment (2026-10-08)

## Motivation

The first complete hardware A0/A1c trace identifies fragment program
`bb9a168934f9e9dc` as the dominant CHARACTERS material:

- ~79 indexed-PN draws per gameplay frame;
- ~96.6 KiB/frame of vertex and fragment uniform setters/copies attributed
  to those draws in the **diagnostic** run;
- 948 different aggregate uniform payloads in 948 draw calls;
- 4 TEV stages and 3 sampled textures.

The entire uniform payload is generally different, so comparing/reusing a
complete snapshot alone is not useful. The candidate below independently
compares every parameter *used by the compiled vertex program*.

## Implemented A3 candidate

`gxm_uniform_delta_upload=0` (default and reference) preserves the original
`sceGxmSetUniformDataF` path.

`gxm_uniform_delta_upload=1` activates a bounded, per-vertex-program CPU
cache **only** for GXM pipelines using indexed-PN fixed vertex transformation:

1. Reserve a **new** GXM vertex default-uniform buffer on each changed draw,
   exactly as before. Never retain or reuse a pointer to an old reservation.
2. After the first complete successful uniform upload, keep a CPU-owned image
   of the entire default buffer (max 4 KiB) and the bit-exact input bytes of
   each active parameter group (max 4 KiB, 32 groups).
3. For the next draw of that vertex program, copy the entire prepared image
   into its fresh reservation. This preserves **all** GXP offsets, padding and
   previously initialized data; no private shader-layout assumptions.
4. Compare each active input group byte-for-byte. Call the original
   `sceGxmSetUniformDataF` only for changed groups, preserving its parameter
   reflection, component counts and order.
   If **every** group changed, skip the buffer copy and use full setters;
   never pay for a clone that cannot eliminate even one upload call.
5. Commit the new prepared image and group inputs **only after** all uploads
   succeed. Allocation/layout failures fall back to the original full setters;
   never accept a partially populated fresh GXM buffer.

Non-indexed draw paths, all fragment programs, all scene/target/depth setup,
skinning and draw order remain untouched. The existing GXM vertex reservation
reuse (when the *entire* payload is already identical) remains unchanged.
Shared scissor-free material variants share the same vertex program and CPU
preparation cache; their fragment programs retain separate fragment caches.
No STREAMED GPU VERTEX or VitaGL path is enabled.

Caveat: the approach copies the entire prepared buffer per changed draw. It
might be **slower** if most parameter groups change. It is an A/B experiment,
not a claim of improved FPS or a validated production default.

## Instrumentation

Five **per-frame**, independently exposed metrics are appended to the consumer
CSV (in this order, after the previous fields):

- `vertex_delta_copies`: fresh GXM reservations initialized from a complete
  CPU-owned prepared buffer;
- `vertex_delta_saved_calls`: unchanged parameter groups that avoided
  `sceGxmSetUniformDataF`;
- `vertex_delta_fallbacks`: no safe span layout/available cache memory;
- `vertex_delta_copied_bytes`: bytes written by copying prepared images;
- `vertex_delta_saved_bytes`: parameter-source bytes whose setter was skipped.

These counters are **not** GPU time or FPS. They are available through
`PerformanceSnapshot` and the diagnostic consumer capture. The prepared-buffer
copy bytes and eliminated setter source bytes are not directly comparable as
GPU traffic.

## Testing status / next hardware gate

- Host helper oracle: byte-for-byte comparison of the reference (full setters)
  and candidate (complete shadow + changed setters), stale reservation overwrite,
  material and matrix updates, 5 simulated frames x 91 draws, signed-zero,
  NaN payloads, layout mismatch, failed setter and invalidation.
- Host Aurora suite tests GX and the helper but **does not compile the Vita GXM
  device implementation**. The full GXM build and live gameplay/goal/replay
  smoke-test require the user's manual Vita build and install.
- Do **not** activate the experiment in the hardware-validated defaults before
  the gate passes. No automatic Vita build, install or push.

Hardware comparison: use the *same SELF*, all gameplay views and shaders ON,
`vita_frameskip=0`, `fixed_dt=0`, `gxm_streamed_vertex_gpu=0`, and identical
scene/seed. The currently enabled `vita_view_draw_capture=1` and
`vita_view_draw_payloads=1` are diagnostic and must both be set to **0** for
quiet FPS runs. Use three interleaved A/A and OFF/ON pairs after at least 600
warmup frames with >=1200 gameplay frames measured. The only intended INI
difference is `gxm_uniform_delta_upload`. Compare median, P95, frame pacing,
goal/replay, skinning poses, shadows, depth and HUD.

For separate **diagnostic** runs (not FPS), enable the bounded consumer capture
to read `vertex_delta_saved_calls`, `vertex_delta_saved_bytes`,
`vertex_delta_copied_bytes` and `vertex_delta_fallbacks`. If the saved
setters are negligible, reject this candidate and concentrate on frontend
uniform build/publish time or shader cost instead. For full A1c correctness
comparison, use `--require-payloads` and the verified SELF/INI/cache identities,
then screenshots and depth-sensitive scenes; the A1c input hash alone does not
certify that GXM output is identical.

### Manual build (user only)

```sh
cd /Users/robin994/Documents/Code/strikersVita/smstrikers-port
STRIKERS_GX_THREAD=ON ./build-vita-native-gxm.sh
```
