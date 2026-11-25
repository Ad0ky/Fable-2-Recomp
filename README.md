<div align="center">

<img src="tools/switch/icon/fable2_icon.jpg" alt="Fable II for Nintendo Switch icon" width="112">

# Fable II for Nintendo Switch

**Fable II (Xbox 360), recompiled to run natively on the Nintendo Switch.**

[![Release](https://img.shields.io/github/v/release/Ad0ky/Fable-2-Recomp?include_prereleases&label=release)](https://github.com/Ad0ky/Fable-2-Recomp/releases)
[![Downloads](https://img.shields.io/github/downloads/Ad0ky/Fable-2-Recomp/total?label=downloads)](https://github.com/Ad0ky/Fable-2-Recomp/releases)
[![License: GPL-3.0](https://img.shields.io/badge/license-GPL--3.0-blue)](LICENSE)
![Nintendo Switch](https://img.shields.io/badge/Nintendo%20Switch-homebrew-E60012)
![deko3d](https://img.shields.io/badge/graphics-deko3d-5E5E5E)
[![Discord](https://img.shields.io/badge/Discord-join-5865F2?logo=discord&logoColor=white)](https://discord.gg/8TQUZpPNeR)

### [Download](https://github.com/Ad0ky/Fable-2-Recomp/releases) · [Report a problem](https://github.com/Ad0ky/Fable-2-Recomp/issues) · [Discord](https://discord.gg/8TQUZpPNeR)

By **Ad0ky** and **[StevensND](https://github.com/StevensND)**, based on [himdo's Fable 2 Recomp](https://github.com/himdo/Fable-2-Recomp) and
[renut_nx_ultimate_base](https://github.com/ChanseyIsTheBest/renut_nx_ultimate_base).
Free and open source: if you paid for this, ask for a refund.

</div>

## What this is

A homebrew build of Fable II for Switch consoles running custom firmware (Atmosphère). The game's Xbox 360 code is
statically recompiled to native ARM64 with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), and its graphics
run on **deko3d**, the Switch's own graphics API, through the reNut Direct3D-level renderer.

You need your own copy of Fable II. This project contains no game files.

> [!WARNING]
> **Work in progress.** The prologue plays through to the end without crashing, but performance in busy areas is still
> low and some graphics are wrong (see [Known issues](#known-issues)). Report crashes and anything that looks different
> from the Xbox 360 ([how](#reporting-a-problem)).

## Performance

Measured on the console at stock clocks (GPU 460 MHz), handheld, the scene rendered at 540p:

| Scene | Frame rate |
| :--- | ---: |
| Menus, intro and light areas | **30 FPS**, the game's cap |
| Busy outdoor areas and towns | **14–21 FPS** |

Work on speed is ongoing: the renderer's cost per draw, the game's own culling (occlusion queries) and the GPU's heaviest
passes are being worked on in that order. The game's logic keeps its normal speed even when the frame rate drops.

## Getting started

You need:

- a Switch with **Atmosphère** and the Homebrew Menu;
- a **microSD card (exFAT)** with room for the game's files;
- your own **Fable II** disc as an ISO: the Game of the Year edition for USA and Europe (title ID 4D5307F1), the version
  [himdo's PC recomp](https://github.com/himdo/Fable-2-Recomp) supports;
- a **Windows PC**, to extract the disc once.

Then:

1. **Extract your ISO.** The PC recomp's launcher has an ISO extractor; any Xbox 360 ISO extractor works too. You need
   the disc's files (`default.xex`, `data/` and the rest), not the ISO itself.
2. **Copy the release ZIP's contents** to `sdmc:/switch/fable2-nx/`.
3. **Copy the extracted disc files** into `sdmc:/switch/fable2-nx/game_root/`.
4. **Start the Homebrew Menu through title takeover:** hold **R** while starting any installed game, then choose
   **fable2-nx**. Starting from the Album doesn't work, because the game needs the full application memory.

SD card layout:

```
sdmc:/switch/fable2-nx/
├── fable2-nx.nro          the program
├── fable2.toml            your settings (read over the built-in ones)
├── deko3d_shaders.bin     the compiled shaders
├── pipeline_list.bin      the pipelines built ahead of play
├── cache/                 the game folder's index
└── game_root/             your disc's files: default.xex, data/, ...
```

## Settings

Settings live in `fable2.toml`, next to the NRO. The defaults are compiled into the program; a line in `fable2.toml`
replaces the matching default. The port's own settings start with `fable2_`. Among them:

| Setting | What it does |
| :--- | :--- |
| `fable2_occlusion_queries` | How the game's visibility tests are answered: `2` measured on the GPU (the game skips what's hidden, and the sun glare fades behind walls), `1` only the sun glare measured, `0` everything counted as visible. |
| `fable2_fetch_range_vs` | Particle shaders that copy only the vertex data each draw reads. Empty turns it off. |
| `fable2_cpu_terrain_tess` | The terrain's tessellation pass on the CPU: `0` off, `1` checked against the GPU (logs only), `2` on. |
| `fable2_smaa_after_ps` | The game's final composite shaders, after which SMAA and the 3D upscale run. |

## Known issues

- Parts of the ground can be missing their texture in some areas.
- The sky, the sun and some shadows can flicker.
- Item previews (for example quest cards) can show a black shape.
- Distant trees can show as dark blocks.
- Short pauses when entering new areas, while shaders and pipelines are prepared.

## Reporting a problem

[Open an issue](https://github.com/Ad0ky/Fable-2-Recomp/issues) with:

- the release version;
- handheld or docked;
- your `fable2.toml`;
- what was on screen when it happened (a screenshot or a short video helps).

Also attach the logs the game writes in `sdmc:/switch/fable2-nx/` (`renut_*.log`, `deko3d.log`, `rex_stderr.log`,
`banjo_profiler.log`). Don't attach the game's files (`game_root/`).

Questions and help: the [Discord](https://discord.gg/8TQUZpPNeR).

## For developers

The Switch build runs in Docker on Windows:

```
run-switch-build.bat                build (code generation and shaders the first time)
run-switch-build.bat codegen        run the code generator and the post-codegen passes again, then build
run-switch-build.bat shaders        only the shaders and the DKSH cache
run-switch-build.bat clean          delete the build folder first
```

It reads your extracted game from `GAME_DIR` (by default `..\Fable 2 1.5.0`, a folder holding `default.xex` and
`data/`) and writes `switch-build.log`. The game's shader bank is read from your game files at build time
(`tools/fable2_shader_bank.py`); nothing from the game is stored in this repository.

- [docs/fable2-rebase-plan.md](docs/fable2-rebase-plan.md): how the port sits on renut_nx_ultimate_base.
- [docs/building.md](docs/building.md), [docs/ultimate-fixes.md](docs/ultimate-fixes.md),
  [docs/off-by-default.md](docs/off-by-default.md): the base's build, its fixes and their switches.
- [docs/renut_nx_ultimate_base-README.md](docs/renut_nx_ultimate_base-README.md): the base's own README.
- `tools/fable2_cpu_samples.py`: reads the console's CPU profile (`cpu_samples.bin`) against the build's ELF.

## Credits

- **Switch port:** [Ad0ky](https://github.com/Ad0ky) and [StevensND](https://github.com/StevensND).
- **Fable 2 Recomp (PC):** [himdo](https://github.com/himdo/Fable-2-Recomp) and its contributors; the CPU terrain
  tessellation pass comes from its SDK fork ([himdo/rexglue-sdk](https://github.com/himdo/rexglue-sdk)).
- **Base and renderer:** [renut_nx_ultimate_base](https://github.com/ChanseyIsTheBest/renut_nx_ultimate_base) by
  ChanseyIsTheBest and the ports it gathers; [reNut](https://github.com/masterspike52/reNut) by masterspike52.
- **Research:** [Fable2RecompMac](https://github.com/itar3/Fable2RecompMac) by itar3 (the occlusion query findings).
- **Tools and libraries:** the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) and [Xenia](https://github.com/xenia-project/xenia),
  [XenosRecomp](https://github.com/hedge-dev/XenosRecomp), [plume](https://github.com/renderbag/plume),
  [uam](https://github.com/devkitPro/uam); on the Switch: [devkitPro](https://devkitpro.org),
  [libnx](https://github.com/switchbrew/libnx), [deko3d](https://github.com/devkitPro/deko3d) and
  [Atmosphère](https://github.com/Atmosphere-NX/Atmosphere).

[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) lists every project this is built on and its license.

Fable II and its assets belong to their respective owners. This is an unofficial, non-commercial project under the
GPL-3.0; dependencies keep their own licenses. Use data from a disc you own, and don't commit game executables,
archives, textures, audio, video, generated game code or captures to this repository.
