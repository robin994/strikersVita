# Asset pipeline provenance

The PSARC writer/parser in `psarc.py` is adapted from
[Rinnegatamante/re4, tools/vita_psarc.py](https://github.com/Rinnegatamante/re4/blob/dee72109dd06c0cf52e2fd21c095b3d09b23cad7/tools/vita_psarc.py)
at commit `dee72109dd06c0cf52e2fd21c095b3d09b23cad7`, inspected on 2026-10-05.
The upstream repository applies CC0-1.0; its license is included unchanged in
`LICENSE-CC0.txt`. No proprietary host packer or SDK source is used.

The upstream
[vita_prepare_data.py](https://github.com/Rinnegatamante/re4/blob/dee72109dd06c0cf52e2fd21c095b3d09b23cad7/tools/vita_prepare_data.py)
extracts two RE4 discs, identifies identical cross-disc files, builds native room
sidecars and packages stored PSARC blocks. Its
[vita_native_room_compiler.py](https://github.com/Rinnegatamante/re4/blob/dee72109dd06c0cf52e2fd21c095b3d09b23cad7/tools/vita_native_room_compiler.py)
decodes RE4 SMD/BIN/TPL data and emits VRM meshes and VRT textures for its matching
runtime. Only fully eligible immutable rooms use that path; other rooms fall
back. The texture compiler evaluates CMPR-to-BC1 color error and alpha behavior.

Local changes to the container tool:

- Reject unsafe paths, symlinks and names that collide under the case policy.
- Reject corrupt compressed streams rather than interpreting them as raw data.
- Stream validation and source comparison in bounded blocks.
- Separate game-specific extraction/validation from packaging.
- Keep original byte order and payloads; deduplicate by SHA-256 and length.
- Publish the finished archive only after full container/source validation.

The portable C reader in `src/platform/asset_archive.c` is a local implementation
of the same public container layout. It does not use the RE4 FIOS ABI shim or
change Vita thread scheduling. Independent compressed fixtures and the adapted
writer both exercise it through the real Strikers DVD adapter.

RE4 native sidecars are not Strikers assets. Strikers uses GLT texture bundles
and GLG/model packets, different animation/material semantics, and Aurora's
native GXM renderer. A native-asset compiler for those formats needs a matching
consumer, source identity and resource lifetime validation. Packaging original
files does not eliminate TEV, convert all textures to BC1, or precompile models.
