# Strikers Vita: public build and tester instructions

Download `strikers_vita.vpk` and the matching `strikers.ini` from
[GitHub Releases](https://github.com/robin994/strikersVita/releases).
The `release-candidate.json` manifest is generated alongside locally built
VPKs in `smstrikers-port/build-vita-gxm-latest` and is not required to install
the public release. Do not commit VPKs or distribute copyrighted game assets.
The game ISO and PSARC are not part of the release.
Testers must prepare their own archive following the repository's
[asset setup guide](https://github.com/robin994/strikersVita/blob/main/ASSET_SETUP.md).

Install the complete VPK with VitaShell and copy the supplied INI to
`ux0:data/strikersVita/strikers.ini`, backing up any personal settings first.
The new pipeline requires `ux0:data/strikersVita/sms.psarc`.
Before updating, also back up `cardA/` and `cardB/` from that directory.

This 1.4 profile uses the normal game flow, PlayStation button glyphs,
444 MHz CPU / 222 MHz GPU, vsync, asynchronous rendering and no render
skipping. The 3D scene renders at 72% of 960x544 (64% for two seconds after a
late frame) and is filtered up on presentation; intros and replays present at
30 FPS. Diagnostics, FPS overlay, profiler, captures, watchdog and probes are
disabled. To restore GameCube glyphs, set `vita_playstation_glyphs = 0` and
restart. `vitagl_render_scale = 100` restores native-resolution rendering at a
large performance cost.

Test startup and save loading, menus and controls, one full match, stadium
entrance, goals/replays, trophy presentations and movies. Include movie
skipping and returning to the menu. For issues, report the scene and exact
reproduction steps, video/audio quality and synchronization, Vita model and
firmware. Include the manifest if you built locally, and preserve the core dump
if the game crashes.
Do not enable profiling during normal-performance tests.

Measured on a retail Vita (test match, 300 live gameplay frames after 600
frames of warm-up, real-time simulation): 58.4-58.9 FPS, mean frame 17.0-17.1 ms,
p95 20.5-21.7 ms, every frame within 33.3 ms. The previous build measured about
16.4 FPS in the same scene. Stable 60 FPS in every scene is not certified;
please report scenes that stutter or drop.

To rebuild from a clean checkout with VitaSDK configured:

```sh
smstrikers-port/build-vita-vitagl.sh --release-candidate
```

The script reuses the shared Vita build directory, resets experimental
CMake options and writes a manifest containing the source commit, build
settings and SHA-256 hashes.
