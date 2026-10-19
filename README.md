# Vector Pinball for VectorPie

A port of [Vector Pinball](https://github.com/dozingcat/Vector-Pinball/) by Brian Nenninger (dozingcat) to
[VectorPie](https://github.com/Goldfinch2/VectorPie): a Raspberry Pi 5 with a USB-DVG (or BeamForge) driving a
**color vector monitor** in an arcade cabinet, and a window on the PC for development. The original is an Android game
drawn entirely with lines and circles; here it runs on a real vector monitor, in color.

This branch (`vectrex-port`) adds the port: [`vectorpie/`](vectorpie/) is the VectorPie version, and it shares its game
core with [`vectrex/`](vectrex/) (the same game for the Vectrex with the UVMC2 multicart). The original Android code under
`app/` is left unchanged: it is the reference and the source of the table data.

## What it is

- **All nine tables of the original**, plus a tenth: the table of **Spinball** (GCE, 1983), with the playfield read
  from the cartridge ROM and the rules following its manual.
- **The game core in C**: physics, table elements and the rules of every table, ported line by line from the Java
  original (`vectrex/core/`, built unchanged here).
- **Physics**: Box2D is replaced by a small solver that reproduces the original's values. **ARCADE** plays exactly like
  the original; **REALISTIC** (default) models the flippers after Visual Pinball X (coil ramp-up, rubber, bounce at the
  stop), so tip passes, post passes, dead bounces and live catches work. Ball speed and, per table, slope and flipper
  power are adjustable.
- **Color**: every element in the original's color from the table data, flashes in white; 3D bodies with occlusion
  (walls, blocks, bumpers and flippers are solid, what lies behind them is hidden); a ball that really rolls, a glowing
  trail, sparks and rising score values.
- **Landscape cabinet screen**: the table fills the picture width and scrolls with the ball; START switches the view
  (full width, closer, closest, whole table). `--vertical` for a monitor mounted upright.
- **Arcade machine**: coins or free play, attract mode with title, demo game and high scores, a 14-segment display for
  scores and messages, 1 to 4 players taking turns, initials, nudging with TILT.
- **Service menu** (F2): game settings, volumes and **freely bindable controls**, independent of `advmame.rc`: keys,
  joystick buttons, hats, axes and mouse buttons, several per function. Plunger digital (hold a button) or analog
  (a yoke axis), nudging by buttons or by an axis.
- **Sound**: the original's recordings and music, stereo by ball position; your own music per table can be added on
  the cabinet.
- **VectorPie menu**: installs as a package, with marquee and its high scores shown in the menu.

## Installing on the cabinet

Download `vpinball-<version>.vpk` from the [releases](https://github.com/Endprodukt/Vector-Pinball/releases) and copy it
to a USB stick's `games/` folder or to `/persistent/games/packages/` on the cabinet. Then in the VectorPie menu:
Settings -> GAMES -> INSTALL GAMES. Settings and high scores in `save/` are kept on an update.

Your own music (not in the package): WAV files named `<n>_name.wav` in `/persistent/games/vpinball/data/snd/`, `0_` for
the title screen, `1_` to `10_` for the tables (16 kHz stereo, 16 bit is enough). The game looks for them at start-up.

## Controls

Out of the box the keys and buttons AdvanceMAME gives these functions (flippers, plunger, nudges), plus coin, START
(also OK) and F2. The ball is launched only with the plunger. F2 opens the service menu at any time; under CONTROLS pick a line, press START, then the
key or button or move the axis you want. The bindings are kept in `save/settings.ini`. Everything else that can be set
by hand is explained in [`vectorpie/game.ini`](vectorpie/game.ini).

## Building

The VectorPie SDK is cloned next to the sources (not part of this repository):

```sh
cd vectorpie
git clone https://github.com/Goldfinch2/VectorPie.git sdk-repo
```

PC (Windows, MSYS2 MINGW64 with gcc and SDL2, CMake, Ninja):

```sh
cmake --preset win && cmake --build --preset win
build/win/pinball.exe --windowed
```

Cabinet (arm64, cross-built on the PC with clang and lld against a copy of the cabinet's libraries), then the package:

```sh
cmake --preset pi-clang && cmake --build --preset pi-clang    # -> build/pi/pinball
sh tools/make_vpk.sh                                         # -> build/vpinball-<version>.vpk
```

The sound recordings come from `vectrex/sdcard/pinball/snd` (`make sounds` in `vectrex/`). Details, the SDK's own Pi
build in WSL and testing tools: [vectorpie/docs/SETUP.md](vectorpie/docs/SETUP.md). Architecture and status:
[vectorpie/docs/ARCHITECTURE.md](vectorpie/docs/ARCHITECTURE.md).

## Credits and license

- Original game: Brian Nenninger (dozingcat), [github.com/dozingcat/Vector-Pinball](https://github.com/dozingcat/Vector-Pinball/).
- Sound, music and the original's audio code: [pdx of Twittering Machine](http://www.twittering.com).
- Table 8 by Jack Morgan; table 10 after Spinball (GCE, 1983).
- VectorPie version: Oliver Carell (Endprodukt), 2026.

Released under version 3 of the GPL, like the original; see [COPYING](COPYING.txt) for the license text. The VectorPie
SDK is under the MIT license.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

The Android original under `app/` includes [the libgdx library by Bad Logic Games](http://libgdx.badlogicgames.com/),
used under [the terms of Version 2.0 of the Apache License](https://www.apache.org/licenses/LICENSE-2.0); see
[devnotes.txt](devnotes.txt) for its code layout.
