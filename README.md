# Vector Pinball for Vectrex

A port of [Vector Pinball](https://github.com/dozingcat/Vector-Pinball/) by Brian Nenninger (dozingcat) to the
**Vectrex** with the **Ultimate Vectrex Multicart 2** (UVMC2, RP2350). The original is an Android game drawn
entirely with lines and circles; here it runs on the Vectrex's vector tube.

This branch (`vectrex-port`) adds the port in [`vectrex/`](vectrex/). The original Android code under `app/` is left
unchanged: it is the reference and the source of the table data.

## What it is

- **All nine tables of the original**, plus a tenth: the table of **Spinball** (GCE, 1983), with the playfield read
  from the cartridge ROM and the rules following its manual.
- **The game core in C** (`vectrex/core/`): physics, table elements and the rules of every table, ported line by line
  from the Java original. The same sources build the console image and a PC simulator.
- **Physics**: Box2D is replaced by a small solver that reproduces the original's values (ball, walls, flippers with
  motor and stops). Two modes:
  - **ARCADE**: exactly like the original.
  - **REALISTIC** (default): flippers modelled after Visual Pinball X (coil ramp-up, rubber, bounce at the stop),
    so tip passes, post passes, dead bounces and live catches work; rubber that loses bounce at higher speed and a
    little scatter on bumpers and kickers.
  - **BALL SPEED** SLOW / MEDIUM (the original's pace) / FAST (default, like a real table's slope), and per table
    **TABLE SLOPE** and **FLIPPER POWER**.
- **3D bodies with occlusion**: walls, blocks, bumpers and flippers are drawn as solid bodies; what lies behind them
  is hidden. Optional **ZOOM** that follows the ball.
- **Nudging and TILT**: nudge the table left, right or up; two warnings, the third is a tilt, sensitivity adjustable.
- **1 to 4 players** taking turns like on a real machine, **high score tables** per table with initials, saved on the
  SD card.
- **Sound**: effects on the Vectrex's own sound chip; through the UVMC2's audio jack the original's recordings and
  music (WAV files on the SD card), plus your own music per table.
- **Intro, 3D menu, pause page, screen adjustment** (size, position, brightness) on the console.

## Installing on the console

Download the files from the [releases](https://github.com/Endprodukt/Vector-Pinball/releases):

1. Copy `pinball.um2` onto the UVMC2's SD card.
2. For the original sounds and music through the audio jack, unpack `pinball-sdcard.zip` onto the card, so that the
   card holds `pinball/snd/*.wav` (13 files). Without them the game runs too, the jack then plays chip effects.
   OPTIONS shows `JACK: 13/13 SOUND FILES` when they are loaded.
3. Your own music: WAV files in `pinball/snd/` named `<n>_name.wav`, `0_` for the menu, `1_` to `10_` for the tables
   (16 bit; best 32 kHz mono).

The game writes `pinball/settings.bin` and `pinball/scores.bin` itself.

## Controls

| | Console | Simulator (PC) |
|---|---|---|
| Left flipper | button 1 | left Shift or Ctrl, Y/Z |
| Right flipper | button 4 | right Shift or Ctrl, Minus, Enter |
| Launch ball | button 3 or a flipper | Space, C |
| Nudge the table | stick left / right / up | arrow left / right / up |
| Pause | button 2 | P, X |
| Menu: pick a line / change a value | stick | arrow keys |
| Menu: OK / back | button 4 (or 3) / button 1 | Enter, Space / Backspace |

## Building

Windows with Git Bash, the [UVMC2 Starter Kit](https://github.com/tullulah/uvmc2-starter-kit) with pico-sdk 2.2.0, the
Arm GNU Toolchain, CMake, Ninja, Python 3, Rust and MSYS2 (gcc + SDL2 for the simulator). In `vectrex/`:

```sh
source tools/env.sh
mingw32-make host < /dev/null     # simulator  -> build/pinball_host.exe
mingw32-make test < /dev/null     # long run of the game core over all tables
mingw32-make uvm2 < /dev/null     # console    -> build_uvm2/pinball.um2
mingw32-make sounds < /dev/null   # SD card sounds -> sdcard/pinball/snd/
```

Details (setup, simulator options, analysis tools): [vectrex/docs/SETUP.md](vectrex/docs/SETUP.md).
Architecture, physics, status and open points: [vectrex/docs/ARCHITECTURE.md](vectrex/docs/ARCHITECTURE.md).

## Status

Everything is done and checked in the simulator: ten tables with their rules, both physics modes, zoom, nudging and
tilt, 1-4 players, menu, high scores, screen adjustment, chip effects and the original sound with music. The game
runs well on the console; frame rate per table, picture stability with ZOOM and the sound loading through the jack
are still being checked there. Multiball sequences and rare table rules (jackpots, card hands, constellations) are
ported from the Java code but not checked by the automatic long runs.

## Credits and license

- Original game: Brian Nenninger (dozingcat), [github.com/dozingcat/Vector-Pinball](https://github.com/dozingcat/Vector-Pinball/).
- Sound, music and the original's audio code: [pdx of Twittering Machine](http://www.twittering.com).
- Table 8 by Jack Morgan; table 10 after Spinball (GCE, 1983).
- Vectrex version: Oliver Carell (Endprodukt), 2026.

Released under version 3 of the GPL, like the original; see [COPYING](COPYING.txt) for the license text.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

The Android original under `app/` includes [the libgdx library by Bad Logic Games](http://libgdx.badlogicgames.com/),
used under [the terms of Version 2.0 of the Apache License](https://www.apache.org/licenses/LICENSE-2.0); see
[devnotes.txt](devnotes.txt) for its code layout.
