# Vector Pinball for VectorPie

The game of the Vectrex version (`../vectrex`) on [VectorPie](https://github.com/Goldfinch2/VectorPie): a Raspberry Pi
with USB-DVG or BeamForge driving a color vector monitor, and a window on the PC for development. The same game - ten
tables, physics, 3D bodies with occlusion, zoom, sound - in color.

- Building and running: [docs/SETUP.md](docs/SETUP.md)
- Architecture, status, open points: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)

The game core and the sound come unchanged from `../vectrex`; this folder holds only the platform and the files that
carry color, picture format and controls.

License: GPL 3 like the original (Brian Nenninger, dozingcat). The VectorPie SDK is under the MIT license.
