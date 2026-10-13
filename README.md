# Battlezone: Rise of the Black Dogs (2000 N64) Recompiled and Port  
  
You will need the game file from a licensed copy in order to play it.  
  
A native PC port of Battlezone: Rise of the Black Dogs, made by statically
recompiling the N64 game with [N64Recomp](https://github.com/N64Recomp/N64Recomp)
and running it on [N64ModernRuntime](https://github.com/N64Recomp/N64ModernRuntime)
with [RT64](https://github.com/rt64/rt64) for rendering.  

Unofficial, and not affiliated with the rights holders. **No game data is
included.** You need your own dump of the game to build or play this.  

Made the same way as my [BattleTanx: Global Assault](https://github.com/wootbeer/btgarecomp) port.  
  
## Game Files  
You need your own data files Battlezone - Rise of the Black Dogs (USA)  
SHA-1:  
CDD1D1DEC9763CE6B7B1A70668EFFCEAD0A185A0 (.n64)  
85B4FEBBE6FBD1ECFC883905E43E68E7188C44F9 (.z64)  
Place in an accessible folder onto your device.  
  
Add a blank portable.txt in the application's folder to make portable if you want to save settings locally.  
  
## Building  
See [BUILDING.md](BUILDING.md).
  
## Changelog  
See [CHANGELOG.md](CHANGELOG.md).
  
## Issues and Limitations

Known:  
- Beta status. Tested on Windows.  
  
Else:  
- Please see the “Issues” section in GitHub.  
  
## Credits  
- [N64Recomp and N64ModernRuntime](https://github.com/N64Recomp) by Mr-Wiseguy
  and contributors, which this port is built with
- [RT64](https://github.com/rt64/rt64) by Dario and contributors, the renderer
- [RecompFrontend](https://github.com/N64Recomp/RecompFrontend) by the N64Recomp
  contributors, the launcher and input layer
- [n64sym](https://github.com/shygoo/n64sym) and
  [rabbitizer](https://github.com/Decompollaborate/rabbitizer), used by the
  function analysis in `tools/`
- The CRT filter is based on Timothy Lottes' CRT shader, and the LCD filter on
  LCD3x by Gigaherz (both public domain)

## License  
The project's own code is GPL-3.0; see [COPYING](COPYING). The game itself remains
the property of its rights holders.  
