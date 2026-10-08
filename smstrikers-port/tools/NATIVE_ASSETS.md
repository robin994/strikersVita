# Native assets in PSARC

The original `files/` and `sys/` assets remain byte identical. Aurora can consume
optional `native/v1/` records for cold texture upload and immutable geometry cache
admission. Original callbacks, model materials, animation and GX fallback remain.

Build the reusable host compiler from embedded Aurora:

```sh
cd smstrikers-port/extern/aurora-vita
cmake --preset vita-host-tests
cmake --build --preset vita-host-tests --target aurora_vita_compile_native_assets
```

Prepare a separate candidate archive from a legally obtained game disc:

```sh
python3 smstrikers-port/tools/vita_prepare_assets.py \
  --profile strikers --iso sms.iso --output sms-native.psarc \
  --native-compiler smstrikers-port/extern/aurora-vita/build/vita-host-tests/aurora_vita_compile_native_assets
```

The compiler is independent of Strikers. It accepts versioned `.avrq` requests
through its command-line directory interface; another port supplies a bounded
adapter and a `NativeAssetReader` callback. The generic PSARC writer supports
`compress_prefixes` for cold sidecars. Original game blocks stay stored, avoiding
extra decompression on the normal DVD path. Native blocks use zlib only when
smaller; Vita's existing archive reader decompresses them on resource admission.

AVNR v1 stores fixed-width little-endian fields, the complete conversion request
and its prepared payload. XXH64 selects a filename; the consumer compares all
source bytes exactly and checks payload checksum and bounds before use. No host
pointer, enum width or GPU pipeline handle is serialized. Unsupported versions,
changed source, palette/array updates and corrupt records use the original path.

The default `--native-textures compact` emits lossless I/I+A/RGB565 and the
conservative exact CMPR-to-BC1 subset. `--native-textures all` also emits Aurora's
exact RGBA outputs. The adapter now supports Nintendo TPL image/palette tables
and each mip prefix; legacy GLT headers and unsupported layouts keep their
original path. No texture downsampling or lossy recompression is introduced.
RGBA can enlarge the native pack, so the report records its actual compressed size.

GLG conversion excludes skin and vertex animation chunks, matrix selectors,
unsupported stream IDs and primitive expansion. It decodes original model-wide
indices into canonical object-space attributes using Aurora's actual decoder.
The consumer packs only the current GPU layout when admitting resident geometry;
matrices, lighting, texgen and materials remain live. It removes cold indexed
attribute decoding, not the first native layout pack. Runtime palette/source
revision validation and GPU retirement barriers remain in the geometry cache.
Legacy/foreign model layouts are reported with a fallback reason.

For a smaller independently updated cache archive, add `--native-only` to the
preparation command and use `--output sms-native-cache.psarc`. This packages only
native records; the original `sms.psarc` remains the game's data source. Select it
with `native_asset_archive`. If that setting is absent, the reader uses the
combined `asset_archive` instead. Both layouts use the same checked records.

Enable one experiment at a time in `strikers.ini`:

```ini
asset_archive = ux0:data/strikersVita/sms.psarc
native_asset_archive = ux0:data/strikersVita/sms-native-cache.psarc
gxm_native_assets = 1
# Independent experiments; defaults remain zero until hardware comparison.
gxm_exact_bc1 = 0
gxm_resident_cdram = 0
```

`gxm_native_assets=0` restores ordinary conversion. CDRAM residency affects only
immutable buffers; dynamic streams retain CPU-visible USER allocations. CDRAM
allocation retains the existing USER fallback and resource lifetime rules.

A sidecar is an optimization candidate. Count `native_texture_hits` and
`native_geometry_hits` in an attributed capture before claiming it is used.
Resident resources may already dominate steady gameplay, so reduced load/decode
work need not improve frame rate. Host equivalence establishes conversion
correctness; Vita visuals, stability and timing require separate captures.

## Audio, movies and shader programs (8 October 2026)

The same PSARC writer accepts separate bounded game adapters. It does not infer
asset layouts from filenames or pass retail data to an online service. The new
input option `--source-archive sms.psarc` derives a pack from an existing game
archive; ISO input remains supported. The source archive, original `files/` and
`sys/` are unchanged. Publication happens only after complete container checks
and every packed file's SHA-256 roundtrip. Source/sidecar records are local and
are excluded from Git.

The Strikers native audio compiler is a host-only executable built with the
existing port tests:

```sh
cmake --build <host-build> --target strikers_compile_native_audio
```

`--native-audio bank` prepares compatible MusyX SDIR/SAMP sound effects.
`--native-audio all` also prepares mono DSP and stereo IDSP streams. The decoder
uses the actual Strikers mixer's 32-bit wrapping accumulation and saturation to
[-32768,32767], rather than WiiCompiled's AX arithmetic. Loop starts use the
existing mixer's history order. IDSP stops at the original StreamLength; unused
partial frames/trailers remain in the original file.

PCM is split into at most 1,024 ADPCM frames per `STPCM001` record. The catalogue
`STAIDX01` selects records by a prefix key and SHA-256 pathname. At runtime each
8-byte source frame, all 16 coefficients and both history samples must match
before PCM is copied. Corrupt records, refill/loop/history changes and catalogue
misses use the original decoder. The existing mixer, resampling, filtering,
voice timing and callbacks remain live. A private archive handle and 2 MiB LRU
serve the audio worker. Cold record admission currently performs file I/O on
that worker: measure cold starts/underruns before enabling the experiment.

`--native-video` uses local FFmpeg/libx264 to add independent H.264 Baseline IDR
pictures to a `WAVC v1` component inside the original THP envelope. Original
resolution, frame rate/count and audio bytes remain exact. The video
recompression is lossy (default CRF 18); the original THP is retained. The Vita
consumer uses SceAvcdec, bounded physically contiguous NV12 memory and the
existing GX I8 plane renderer. It validates every output pointer, geometry,
pitch and crop. No stale picture is published when the decoder accepts a packet
without producing output. Planes still need tiling/upload through the existing
movie renderer. Missing or unsupported sidecars use the original movie path;
Vita builds without FFmpeg skip those movies, as before.

`--shader-cache DIR` packages checked `gxm-cg-gxp-v1` programs already compiled
on Vita. A PC does not replace the Vita Cg compiler. Each AVGX record validates
ABI, exact shader-source hash, stage, length and checksum; GXM checks the program
before registration. Existing loose shader cache files are tried first, the
PSARC cache second, and the compiler handles unseen variants. Pack coverage is
limited to the game/material states exercised when collecting the cache.
Precompiled programs remove cold compilation, not fragment execution costs.

A space-conscious pack prepares all exact compatible graphics, SFX and movies,
while keeping music in its original compressed format:

```sh
python3 smstrikers-port/tools/vita_prepare_assets.py \
  --profile strikers --source-archive sms.psarc --output sms-native-balanced.psarc \
  --native-only --native-textures all \
  --native-compiler <aurora-host-build>/aurora_vita_compile_native_assets \
  --native-audio bank --audio-compiler <host-build>/tests/strikers_compile_native_audio \
  --native-video --video-crf 18 --shader-cache <collected-cache-directory>
```

Replace `bank` with `all` to prepare every supported audio source. PCM costs
substantially more storage; the actual report must determine whether that
profile fits the desired PSARC saving. There is no automatic default promotion.

```ini
asset_archive = ux0:data/strikersVita/sms.psarc
native_asset_archive = ux0:data/strikersVita/sms-native-balanced.psarc
gxm_native_assets = 1
vita_native_audio = 0
vita_native_video = 0
vita_test_match = 0
vita_frameskip = 0
```

Graphics/shader sidecars share `gxm_native_assets`; audio and video have separate
INI gates. Test one category per run. Log markers `native_shader hits`,
`native-audio block_hits` and `native-video hardware pictures` establish actual
usage; opening a catalogue or selecting a movie alone does not establish it.
Shaders, animation, lighting, skinning and current GPU-layout packing retain
their runtime responsibilities. These assets do not establish 60 FPS.
