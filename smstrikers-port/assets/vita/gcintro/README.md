# GameCube intro splash

Packed into the VPK under `app0:gcintro/` and played by `src/platform/gc_intro.c` while the renderer
prepares its shaders. `vita_gc_intro = 0` in `strikers.ini` turns it off.

- `scene.bin`, `texture_0.bin`..`texture_4.bin`: meshes, animation tracks and textures exported from
  the [vaexenc/gcintro](https://github.com/vaexenc/gcintro) GLB (MIT, a from-scratch recreation of
  the GameCube boot animation), by the exporter in Rinnegatamante's vitaGL `custom_splashscreen`
  sample.
- `gcintro_audio.pcm`: the soundtrack, 48 kHz stereo signed 16-bit PCM, prepared by the same sample.
- `*.gxp`: the sample's shaders, compiled from `shaders/*.cg` with `psp2cgc`
  (`-profile sce_vp_psp2` for `splash_v.cg`, `-profile sce_fp_psp2` for the rest).

The GameCube logo and boot jingle are Nintendo's.
