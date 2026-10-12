# Progress / Roadmap

The goal is the original game, running natively: fixes are limited to what the
PC port needs (timing, rendering and runtime differences from the N64), and
leave the game's own behaviour, look and balance alone.

## Working (tested on Linux, llvmpipe software Vulkan)

- Boot: Controller Pak check (creates the save note), copyright screen
  ("Expansion Pak detected"), Crave and Climax logos, title screen.
- Attract-mode demos (in-game 3D, every few logo cycles).
- Menus: single player, campaign select, the Rumble Pak prompt.
- Campaign missions: the first American mission loads and plays; the tank
  drives and turns, the cockpit HUD and radar work. Ran several minutes
  without a crash or stall.
- Saves: Controller Pak emulation, standard `.mpk` files (from btgarecomp).
- Audio: the game's standard libultra `aspMain`, recompiled from the ROM.

## How this game differs from BattleTanx (and what was done)

- **Code layout.** A small boot segment (0x80028400) plus three modules the
  game's own segment table (0x80057A30) loads one at a time at 0x8010FFE0
  (menus, in-game, a small third), and 44 mission code blobs the in-game
  module loads at 0x801FD150 (94-entry table at 0x801F9C90). Each is an overlay
  section; `src/game/modules.cpp` tells the runtime which is loaded, from the
  game's ROM-read helper.
- **Function boundaries** come from a recursive-descent finder
  (`tools/find_functions.py`), not a gap-to-next-symbol split, so functions only
  reached through pointers don't merge into their neighbours (BattleTanx's
  biggest source of crashes). Data interleaved with code (jump tables, strings,
  floats) is told apart by what compilers never emit.
- **Busy-waits** are found by `tools/scan_spin_loops.py` and get yield hooks
  generated into the config (36 sites: an inlined wait-for-frame is
  everywhere), instead of one crash at a time.
- **Jump tables N64Recomp can't decode** (a table pointer stepped through in a
  loop) get their targets from the finder via a new `jr_targets` config option
  (`lib-patches/N64ModernRuntime/N64Recomp/0002`).
- **Divide by zero** gives MIPS results in all generated code
  (`lib-patches/.../0001`), instead of ~100 per-site hooks.
- **Direct RDP command lists** (`osDpSetNextBuffer`, unimplemented in the stock
  runtime) are drawn through RT64's raw-RDP path. The game clears the depth
  buffer and next framebuffer this way before each 3D frame; those lists run in
  the next task's RT64 workload so the depth clear is recognized
  (`lib-patches/N64ModernRuntime`, `RecompFrontend`, `rt64/0002`).
- **In-game video mode.** Menus run 320x240, but in-game the game switches to a
  640x240 mode (pixels half as wide as they are tall). RT64 assumed square
  pixels, so the picture came out squashed to 2.67:1 and Expand did nothing;
  `lib-patches/rt64/0004` takes the VI's pixel shape into account.
- **Widescreen (Expand).** The game clips its 3D view and full-screen layers
  16 pixels in from each edge, inside the TV overscan, but clears the whole
  width. RT64 only widened draws that span the full width, so the 3D view stayed
  4:3 between widened layers. `lib-patches/rt64/0006` lets draws that stop
  within a small margin of the edges count as full-width.
- **Widescreen HUD.** In Expand, the radar and unit menu are anchored to the
  left edge and the weapon panel to the right (RT64 extended GBI commands
  written into the game's display lists from hooks: `src/game/widescreen_hud.cpp`).
  The HUD is a list of C++ element objects drawn through virtual calls, and
  all text is queued and drawn in one batch, so each element and each string
  is anchored as a whole. The unit icons, counters, crosshair and message box
  stay centred. Anchored drawing is clipped to the (widened) 3D view area,
  so HUD pieces the game parks in the TV overscan border stay hidden.
- **TV-safe frame.** In game, the game clips its view 16 pixels in from the
  sides and 8 from the top and bottom, which an N64's TV hid in its overscan
  and a PC display shows as a black frame. RT64 crops it at presentation
  (`lib-patches/rt64/0007`); the game still renders the same.
- **Ground jitter.** The ground grid is rebuilt every frame relative to the
  camera, with the camera position truncated to whole vertex units once for
  the whole grid, so the ground shifted by up to a unit per frame against
  everything else (invisible at N64 resolution, a visible shake upscaled).
  Hooks in the three ground builders load a modelview that undoes the error
  (`src/game/terrain_precision.cpp`).
- **Effect sprites in widescreen.** Smoke, puffs and explosions are billboard
  rectangles the game projects itself and culls at the 4:3 edge, clamping
  any that cross the left or top edge. Hooks in the two sprite drawers widen
  the cull and rewrite each sprite as an extended texture rectangle with its
  unclamped coordinates, drawn with the scissor opened to the widened view
  (`src/game/widescreen_sprites.cpp`).
- **Text bleed.** Scaled, filtered rectangles (the in-game font) blended in
  the next row of their packed texture when upscaled, leaving dots under each
  letter; `lib-patches/rt64/0005` ends the sampled range at the last row the
  RDP itself samples.
- Smaller fixes in `battlezone.us.rev0.toml`: crt0's `break` (main returns under
  the runtime), uncached KSEG1 address constants, the developers' debug-output
  call (printed to stdout, `src/game/debug_output.cpp`), stubs for the game's
  own exception and cache code.

## Known issues

- In the turret view of the first campaign mission, the ground stops about 40
  pixels above the bottom of the screen, leaving a black band with the top edge
  of the turret model along the bottom. It isn't RT64 clipping (disabling all
  near clipping changes nothing), so it's most likely the game's own view;
  not yet confirmed against real hardware.
- Once, with a Controller Pak save present, a decompress got a bad destination
  (0xA0000000) and crashed. Not reproduced since the module unload-order fix.
- Windows builds run (tested by hand); no frame-time work for higher
  framerates yet (BattleTanx's later rounds).
- Widescreen HUD anchoring covers the gameplay HUD seen so far (training
  mission, first campaign mission); other vehicles' or modes' HUD elements may
  still sit in the centred 4:3 area. The message box stays centred.
- Only the first campaign mission has been played.

## Next

- Windows build and testing on real GPUs.
- Play through more missions; multiplayer.
- Frame pacing at higher framerates.

## Where things are

| Path | What |
|---|---|
| `battlezone.us.rev0.toml` | N64Recomp config; its tail is generated by `tools/update_config.py` |
| `BattlezoneSyms/` | Symbol file and jump tables (from `tools/find_functions.py`) |
| `aspMain.us.rev0.toml` | RSP audio microcode config |
| `tools/bzlayout.py` | ROM layout: segments, modules, mission blobs, known names |
| `tools/regen.sh` | Regenerate symbols, config tail and RecompiledFuncs/ |
| `src/main/` | Frontend: window, audio, input, config, crash reporting |
| `src/game/` | Game-specific code: module loading, pak HLE, debug output |
| `lib-patches/` | Patches to the submodules, applied at configure time or by `cmake/apply_lib_patches.cmake` |
