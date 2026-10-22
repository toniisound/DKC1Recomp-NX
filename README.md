# DKC1Recomp-NX

Native Nintendo Switch port of *Donkey Kong Country* (SNES), built from a
static recompilation of the original game.

By **Toniisound** — version 0.0.17. Based on
[elliotttate/DKC1Recomp](https://github.com/elliotttate/DKC1Recomp) and the
first Switch port by [souldbminerr](https://github.com/souldbminerr/DKC1Recomp-NX).

No game code or ROM is included. You need your own *Donkey Kong Country*
(USA) v1.0 ROM.

## Features

- **Locked 60 fps**: the game is paced by the console's real display refresh,
  so it no longer drops or repeats frames.
- **CPU boost to 1785 MHz** while playing, restored on exit and re-applied
  after docking/undocking or sleep.
- **HD upscaler**: the "Reconstruct" shader runs on the Switch GPU (OpenGL)
  and redraws pixel-art edges, slopes and dithering at full 720p/1080p for
  the whole game. Five detail levels: Pixels, Dither, Diagonals, Slopes,
  Slopes+. If OpenGL cannot start, the game falls back to the plain renderer.
- **Pause menu** (press **−**): save/load state, state slot (1–5), aspect
  ratio (4:3 / 16:10 / 16:9), scaling (Sharp / Smooth / HD), HD detail,
  volume, rumble on/off and exit. **+** is still the game's own pause.
- **Settings are remembered** between sessions.
- **Save states**: **L3** saves and **R3** loads the current slot during play,
  with an on-screen confirmation.
- **Safer in-game saves**: Candy's saves are written the moment the game saves,
  atomically, keeping the previous file as a backup.
- **HD Rumble**: a short tap when you stomp an enemy, a strong rumble when you
  lose a life and a light one when you gain one (Joy-Con, Pro Controller and
  handheld).
- **On-screen error** if the ROM is missing or is not the supported version.

## Controls

| Button | Action |
|---|---|
| D-Pad / Left stick | Move |
| A / B / X / Y | SNES A / B / X / Y |
| ZL / ZR | SNES L / R |
| + | Start (game pause) |
| L | Select |
| **−** | Pause menu |
| **L3** / **R3** | Save state / Load state |

## Installing

Copy the `.nro` and your ROM to the same folder on the SD card:

```
sdmc:/switch/dkc1/dkc1.nro
sdmc:/switch/dkc1/rom.smc     (rom.sfc is also accepted)
```

Files the game creates in that folder:

| File | Contents |
|---|---|
| `saves/save.srm` (+ `.bak`) | In-game save (Candy) |
| `quicksave.state`, `slot2.state` … `slot5.state` | Save states |
| `dkc1nx.ini` | Pause-menu settings |

### Supported ROM

Headerless *Donkey Kong Country* USA v1.0:

| Property | Expected value |
| --- | --- |
| Size | 4,194,304 bytes |
| SHA-256 | `fa8cacf5bbfc39ee6bbaa557adf89133d60d42f6cf9e1db30d5a36a469f74d15` |

## Building (devkitPro, Linux/WSL)

Requirements: devkitPro with devkitA64, libnx and the `switch-sdl2`
portlibs; Python 3; Rust (`cargo`) for the recompiler's analyzer.

```bash
git clone --recursive https://github.com/toniisound/DKC1Recomp-NX
cd DKC1Recomp-NX
sh scripts/setup_switch_framework.sh                             # Switch layer for snesrecomp
python3 scripts/generate_snesrecomp.py --rom /path/to/dkc1.sfc   # once, needs your ROM
make -f Makefile.switch
```

The result is `dkc1.nro`. The generated sources stay private to your machine
and are never committed.

## Licensing and credits

- Project host code is MIT licensed (see [`LICENSE`](LICENSE)).
- The `snesrecomp` framework is distributed under the PolyForm Noncommercial
  License 1.0.0 with its own third-party notices. Its Switch layer comes from
  [souldbminerr/snesrecomp](https://github.com/souldbminerr/snesrecomp) and is
  applied by `scripts/setup_switch_framework.sh`.
- Structural metadata in `recomp/*.cfg` derives from the GPL-3 Yoshifanatic1
  *Donkey Kong Country 1* disassembly; provenance is recorded in each file.
- The Reconstruct shader and in-game save handling come from
  [elliotttate/DKC1Recomp](https://github.com/elliotttate/DKC1Recomp), which
  in turn follows the MIT-licensed DKC2Recomp project.
- The pause-menu font is the public-domain font8x8 by Daniel Hepper.

See [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md) for details.

*Donkey Kong Country* is a trademark of Nintendo. This is an unofficial fan
project and is not affiliated with or endorsed by Nintendo.

## Donations

This emulator is completely free and open-source. Optional donations to support the development are always welcome:

[![Ko-fi](https://img.shields.io/badge/Ko--fi-Support-FF5E5B?style=for-the-badge&logo=ko-fi&logoColor=white)](https://ko-fi.com/toniisound)
[![PayPal](https://img.shields.io/badge/PayPal-Donate-00457C?style=for-the-badge&logo=paypal&logoColor=white)](https://paypal.me/toniisound)
