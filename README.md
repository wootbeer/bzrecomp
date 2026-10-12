# Battlezone: Rise of the Black Dogs (2000 N64) Recompiled and Port

A native PC port of Battlezone: Rise of the Black Dogs, made by statically
recompiling the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp)
and running it on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
with [RT64](https://github.com/rt64/rt64) for rendering. A sister project of
[btgarecomp](https://github.com/wootbeer/btgarecomp) (BattleTanx: Global Assault),
built the same way.

Unofficial, and not affiliated with the rights holders. **No game data is
included.** You need your own dump of the game to build or play this.

## Game Files
You need your own dump of Battlezone - Rise of the Black Dogs (USA). Either byte
order works; the game converts it on first launch:

| Dump | SHA-1 |
|---|---|
| `.n64` / `.v64` (byte-swapped) | `CDD1D1DEC9763CE6B7B1A70668EFFCEAD0A185A0` |
| `.z64` (big-endian) | `85B4FEBBE6FBD1ECFC883905E43E68E7188C44F9` |

Add a blank `portable.txt` in the application's folder to keep settings and
saves next to the exe.

## Status
Early development. It boots, plays the intro and attract demos, runs the menus,
and plays campaign missions with keyboard or controller. Tested on Linux so far
(including software Vulkan); Windows builds are set up as in btgarecomp but not
yet tested. See [PROGRESS.md](PROGRESS.md).

On a gamepad, the default controls put the N64 stick on the right stick
(vertical axis inverted) and the C buttons on the left stick (click
for C-Up), with the left trigger as R and the right trigger as Z. Saved
controls are kept; use "Reset to defaults" on the Controls tab to pick these up.

Like on the N64, a controller holds one pak: the game asks for a Rumble Pak when
a mission starts while the Controller Pak (saves) is selected. Press B to
continue without it, or choose the pak per player in Settings > General.

## Building
See [BUILDING.md](BUILDING.md).

## Credits
- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy
  and contributors, which this port is built with
- [RT64](https://github.com/rt64/rt64) by Dario and contributors, the renderer
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp
  contributors, the launcher and input layer
- [n64sym](https://github.com/shygoo/n64sym) and
  [rabbitizer](https://github.com/Decompollaborate/rabbitizer), used by the
  function analysis in `tools/`

## License
The project's own code is GPL-3.0; see [COPYING](COPYING). The game itself remains
the property of its rights holders.
