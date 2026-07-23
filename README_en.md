# Street Fighter III (NES) — UEFI Shell Reimplementation

> 🌐 Language: [中文](README.md) ｜ **English**

A faithful **C reimplementation** of the NES / Famicom fighting game **Street Fighter III (9 Fighter) / Super Fighter III**, translated from its 6502 assembly and rebuilt as a **UEFI Shell application**. Graphics are rendered through **GOP (Graphics Output Protocol)**; the target platform is **QEMU + OVMF**. No audio.

> ⚠️ **Copyright notice (please read before open-sourcing)**: This repository contains **only** the original reimplementation source code plus build / resource-extraction scripts. The original game **ROM, disassembly listings and artwork are copyrighted by their respective owners and are NOT included** in this repository. You must **legally obtain your own dump** of the ROM, place it at `Ref/orgrom.nes`, and run the extractors in `SFC3/resource_extract/` and the root generators to produce the embedded C data tables (`*_data.c`, palettes, sprite frames, …). This project is for **educational / reverse-engineering study**. The source code license is in the repository-root `LICENSE` file (please add your own; an OSI license such as MIT is suggested).

Current version: **v0.1.0 (build 27)** (work in progress — see the [Roadmap](#roadmap--todo)).

## Screens

| Title | Character Select |
|:---:|:---:|
| ![title](article/01_title.png) | ![select](article/02_select.png) |

| Versus (VS) | Fight |
|:---:|:---:|
| ![vs](article/03_vs.png) | ![fight](article/04_fight.png) |

**Animated captures (GIF)**

| Fight motion (walk + AI attacks) | Punch combo |
|:---:|:---:|
| ![fight gif](article/gif_fight.gif) | ![punch gif](article/gif_punch.gif) |

## What this is

The project translates the logic of an NES fighting game from its 6502 disassembly **function-by-function** into C, running inside a UEFI firmware environment: GOP integer-scales the emulated 256×240 PPU frame and `Blt`s it to the screen. The guiding philosophy is **fidelity to the original logic** — the state machine, animation frame sequences, collision and bank switching are translated against the disassembly rather than re-invented; graphics resources (CHR tiles, palettes, nametables) are extracted from the original ROM.

## Current status

- **Title**: logo + “PUSH START TO PLAY / EASY…HARD” + a movable difficulty cursor.
- **Character select**: world map + 3×3 portrait grid.
- **Versus (VS)**: VS logo + both fighters' portraits (portraits shown; full 8x16 portraits in the [Roadmap](#roadmap--todo)).
- **Fight**: the India-temple arena (multi-bank background restored via **per-scanline CHR segments**), both fighters walk / attack (multi-frame animation), simple AI.
- **Palette**: a composite LUT blending reference-screenshot samples with the standard NES table, taming the over-saturated raw RGB palette.

> Note: fighter poses are a *recognizable approximation* (exact per-frame poses, the crouch offset and a faithful AI remain on the roadmap); the VS portraits are currently drawn in 8×8 mode (banded) — full restoration is on the roadmap.

## Architecture / modules

The NES hardware is modelled as a set of C structures (`NES_STATE`: RAM + PPU registers + Mapper-91 banks + framebuffer). Each frame, `PpuRenderFrame` produces a 256×240 palette-indexed framebuffer; `GopPresent` maps it through the LUT to BGR and integer-scales it via `Blt`.

| Module | Role |
|---|---|
| [main.c](SFC3/main.c) | Entry point, main loop, GOP / input / timer init |
| [nes_state.c](SFC3/nes_state.c) / [nes_state.h](SFC3/nes_state.h) / [nes_types.h](SFC3/nes_types.h) | NES state struct, RAM / PPU / Mapper mapping |
| [ppu.c](SFC3/ppu.c) | PPU registers, Mapper 91, tile decode, background + sprite frame render, palette LUT |
| [gop_render.c](SFC3/gop_render.c) | GOP scaling + double-buffered `Blt` + version overlay |
| [background.c](SFC3/background.c) / [background_data.c](SFC3/background_data.c) | Screen nametables / palettes / **per-scanline CHR segment tables** |
| [game_state.c](SFC3/game_state.c) | State machine, scene transitions, fight loop, AI hook, per-screen NMI handlers |
| [fighter.c](SFC3/fighter.c) / [fighter_data.c](SFC3/fighter_data.c) | Fighter state machine (simplified), collision |
| [fighter_sprite.c](SFC3/fighter_sprite.c) / [fighter_sprite_data.c](SFC3/fighter_sprite_data.c) | **Faithful translation of sub_E4E8 sprite emission** + VS portrait render + baked frame/anim tables |
| [ai.c](SFC3/ai.c) | Simplified CPU opponent |
| [input.c](SFC3/input.c) / [timer.c](SFC3/timer.c) | UEFI keyboard → NES pad mapping, frame timing |
| [resource.c](SFC3/resource.c) | Load CHR / PRG from disk into memory |

## Resource extraction (C data generated from the ROM)

The embedded data tables are **not hand-transcribed** — they are generated from `Ref/orgrom.nes` by scripts:

- Background / nametables: `SFC3/resource_extract/` (`generate_background_data.py`, `decode_*`, `extract_resources.py`).
- Sprite frames / animation: root `extract_fighter_sprites.py` + `gen_fighter_sprite_data.py` + `build_fighter_timelines.py` (parse the original animation-script VM opcodes and bake per-frame triples + timelines).
- Palette: `gen_palette_selectsample.py` / `gen_soft_palette.py` (reference sampling blended with the standard table).

## Build

**Prerequisites**: EDK2 (with `OvmfPkg`; clone to `../edk2` or set `WORKSPACE` yourself), Visual Studio 2019, QEMU, NASM, Python 3.

The recommended path is the root [build_direct.ps1](build_direct.ps1) (it sets the error-prone env vars `VS2019_PREFIX`, `NASM_PREFIX`, … and copies the artefact `SFC3.efi` into `qemu_disk/`):

```powershell
powershell -ExecutionPolicy Bypass -File build_direct.ps1
```

Or a manual EDK2 build (after `edksetup.bat`):

```bat
build -p OvmfPkg\OvmfPkgX64.dsc -a X64 -t VS2019 -b DEBUG -m SFC3\SFC3.inf
```

> The EDK2 / VS / QEMU paths in the scripts are the author's local defaults; after open-sourcing, edit the script paths or set the corresponding environment variables for your machine.

## Run

Place `SFC3.efi` in `qemu_disk/` (with `startup.nsh`) and run windowed:

```powershell
powershell -ExecutionPolicy Bypass -File run_qemu_window.ps1
```

Headless + QMP (for automated screenshots): `run_qemu_qmp.ps1`.

**Controls**

| NES | Keyboard |
|---|---|
| Up / Down / Left / Right | W / S / A / D or arrow keys |
| A (punch) | K |
| B (heavy) | J |
| Start | Enter |

## Project layout

```
SFC3/                  UEFI app source (see "Architecture")
SFC3/resource_extract/ scripts that generate C data from the ROM
article/               runtime screenshots / GIFs of this project (docs)
build_direct.ps1       recommended build script
run_qemu_window.ps1    windowed run
run_qemu_qmp.ps1       headless + QMP run
CLAUDE.md              detailed engineering spec + original-addr memo
Ref/                   (not committed) original ROM / disassembly — supply your own
```

## Roadmap / TODO

- **Full VS portraits**: the original draws them as 8x16 sprites and/or as background tiles written into the nametable; the current 8×8 render is banded — needs 8x16 + dual-window CHR mapping or the nametable method.
- **Faithful AI & poses**: translate the original `sub_DA24` decision chain, add the crouch script-level dy offset, per-fighter pose verification.
- **Palette fine-tuning**: closer match to the reference look (incl. fighter sprite-palette tweaks).
- Audio: not planned.

## See also

- Detailed engineering spec + original address map: [CLAUDE.md](CLAUDE.md)
- Chinese documentation: [README.md](README.md)
