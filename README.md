# strikersVita - Super Mario Strikers for PS Vita

![](media/strikers-gameplay.webp)

> [!IMPORTANT]
> **Work in progress.** This repository is an active PS Vita port and is not yet a finished release. The current milestone is a hardware-testable native VPK with the original game booting through the Vita backend.

`strikersVita` is an unofficial native port of **Super Mario Strikers / Mario Smash Football** for PlayStation Vita.

The project builds on the existing native Strikers port and the community decompilation work led by [Yannick Suter](https://github.com/yannicksuter). The Vita renderer is based on our fork of [Aurora](https://github.com/encounter/aurora), [aurora-vita](https://github.com/robin994/aurora-vita), with a dedicated vitaGL/vitashark backend.

No game assets are included. You must provide game data from your own legally obtained copy of Super Mario Strikers.

## Current status

The Vita bring-up is under active development. The current port includes:

- ARM32/VitaSDK build support.
- A dedicated PS Vita CMake/VPK target.
- `aurora-vita` integration using the `vita-experiment` backend.
- Native 960x544 output and 60 Hz display setup.
- vitaGL/vitashark rendering infrastructure.
- Native PS Vita controls through `sceCtrl`.
- Vita filesystem, timing, memory and runtime adaptations.
- Game data loading from `ux0:data/strikersVita`.
- Region-aware support inherited from the native port for USA, Europe and Japan game data.
- Aurora diagnostics and telemetry for real-hardware debugging.

The port is **not yet considered fully playable**. Rendering coverage, remaining GameCube-to-Vita platform assumptions, audio and full-game stability are still being brought up on real hardware.

## Requirements

- A homebrew-enabled PlayStation Vita or PlayStation TV.
- VitaShell or another way to install VPK files and copy game data.
- A copy of your own Super Mario Strikers / Mario Smash Football GameCube disc data.
- Enough free storage for the game data. An extracted `files` directory is roughly 618 MB; a full GameCube image is larger.

## Game data

The Vita port uses:

```text
ux0:data/strikersVita/
```

The preferred layouts are either a disc image directly in that directory:

```text
ux0:data/strikersVita/strikers.iso
```

or the extracted GameCube `files` directory:

```text
ux0:data/strikersVita/files/common.ini
ux0:data/strikersVita/files/art/...
ux0:data/strikersVita/files/audio/...
```

The disc reader supports plain `.iso` / `.gcm` images and GameCube CISO. GCZ support depends on whether the build was linked with zlib. For Vita development and testing, a normal `.iso` or extracted `files` directory is recommended.

Do **not** open an issue asking for game files, disc images, copyrighted assets or download links.

## Controls

The current GameCube-to-Vita mapping is:

| GameCube | PS Vita |
| --- | --- |
| A | Cross |
| B | Circle |
| X | Square |
| Y | Triangle |
| L | L |
| R | R |
| Z | Select |
| Start | Start |
| Control Stick | Left Stick |
| C-Stick | Right Stick |
| D-Pad | D-Pad |

During development, **Start + Select** exits the application cleanly.

## Building for PS Vita

### Dependencies

You need a working [VitaSDK](https://vitasdk.org/) installation with the libraries used by the Vita backend, including:

- vitaGL
- vitashark
- SDL3
- VitaSDK system stubs/toolchain

Clone the repository together with its submodules:

```sh
git clone --recursive https://github.com/robin994/strikersVita.git
cd strikersVita/smstrikers-port
```

If the repository was already cloned without submodules:

```sh
git submodule update --init --recursive
```

Build the Vita target with:

```sh
make -f Makefile.vita
```

The Vita build uses `VITASDK=/usr/local/vitasdk` by default. Override it when your SDK lives elsewhere:

```sh
make -f Makefile.vita VITASDK=/path/to/vitasdk
```

The generated VPK is produced in the Vita build directory as:

```text
smstrikers-port/build-vita/strikers_vita.vpk
```

## Renderer

The desktop port uses upstream Aurora and modern desktop graphics APIs. This fork instead has a dedicated Vita path using [robin994/aurora-vita](https://github.com/robin994/aurora-vita).

The Vita backend is designed around the hardware available on PS Vita rather than trying to reproduce the desktop Dawn/WebGPU stack. Current work includes GX translation, vitaGL draw submission, texture decoding/caching, vertex conversion, EFB handling, shader generation, pipeline caching and memory-budget tracking.

Runtime diagnostics are written under:

```text
ux0:data/strikersVita/aurora_telemetry.log
ux0:data/strikersVita/aurora_coverage.log
ux0:data/strikersVita/aurora_trace.log
```

These logs are especially useful when reporting rendering or boot failures from real hardware.

## Contributing

The focus of this repository is the **PS Vita port**. Useful contributions include:

- GX/Aurora Vita rendering fixes.
- ARM32 correctness and alignment fixes.
- vitaGL/vitashark performance work.
- Memory reduction and allocator improvements.
- Audio backend work.
- Input and PS TV compatibility.
- Reproducible real-hardware crash reports and logs.

When reporting a crash, include the relevant runtime/Aurora logs and, when available, the Vita core dump. Do not include copyrighted game data.

## Credits

- [Yannick Suter](https://github.com/yannicksuter) and all contributors to the Super Mario Strikers decompilation.
- The authors and contributors of the native PC Strikers port this fork is based on.
- The [Aurora](https://github.com/encounter/aurora) project and its contributors.
- [aurora-vita](https://github.com/robin994/aurora-vita) for the PS Vita graphics backend used by this port.
- The VitaSDK, vitaGL and vitashark communities.

## Licensing and legal notice

This repository contains material with different licences and rights statuses.

Original porting code, tools and documentation retain their applicable licences. Reconstructed game code is an unofficial source reconstruction and is **not** an official source release. Third-party components such as MusyX, ODE, Aurora and other dependencies retain their respective licences and notices.

No copyrighted game assets or disc images are distributed by this project. You must supply game data from your own lawfully obtained copy.

This is an unofficial fan project. It is not affiliated with, sponsored by or endorsed by Nintendo, Next Level Games or Sony Interactive Entertainment.
