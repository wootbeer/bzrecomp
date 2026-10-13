# Draw distance: notes for later

A Draw Distance option was tried and taken out (October 2026): with the ground
the game builds, objects could only go about 15% further without floating, and
distant mountains still popped in. A real increase needs a bigger ground grid.
The full attempt is in git history (commits dd94993 to 82bb0d5,
`src/game/draw_distance.cpp`); what it found:

## Objects and the far plane
- A camera's far distance is camera + 0x30 (200 world units in game). Every use
  reads it fresh, so it can be changed at each read without touching memory:
  - `func_8002B8FC`, `lwc1 $f0, 0x30($s5)` at 0x8002B978: the projection's far
    plane (`guPerspectiveF`); its matrix is stored at camera + 0x10C.
  - `func_8002B8FC`, 0x8002C0FC: the fog's depth mapping, stored at camera +
    0x1E4 (int) and + 0x1E8 (float) as z = A + B / distance.
  - `func_80030624`, 0x8003064C: the depth buckets, one per world unit,
    trunc(far) of them, allocated below graphics state + 0x68.
  - `func_80030824`, 0x80030888: the object cull (depth minus radius against
    far). The object is at ($f12, $f10) from the camera there, radius $f8.
  - `func_80030410` (0x8003043C) and `func_800304F4` (0x80030528): the effect,
    beam and particle checks; the point is their $a1, the radius $a2.
- Depth in the culls is x * c34 + z * c38 and the side distance
  x * c38 - z * c34 (c34 = camera + 0x34 etc.), visible while under
  c50 * depth (camera + 0x50, the 4:3 half-width slope).

## Fog
- `func_8002B12C` sets the fog every frame: start and end from 0x800A478C and
  0x800A478E (128 and 200 in game, end = far), plus a time-varying offset.
- Its multiplier is 16-bit: a farther plane squeezes the fog into less depth
  and pushes it past 32767, which wraps to full fog unless clamped (hook before
  `trunc.w.s` at 0x8002B5B8).
- The mapping at camera + 0x1E4/0x1E8 must come from the same far value as the
  projection, or the fog slides away.

## The ground
- `ovl1_func_8012F864` builds a grid of a fixed size, about 38 x 34 cells of
  10 world units ($s4 x $s0 vertices at 0x8012FAAC), placed off-centre around
  the view: it ends about 225-250 units out across the 4:3 view and nearer at
  a widescreen view's sides. Its start and size come from camera fields (+0x84,
  +0x88 and others), set up at the top of the function.
- Vertices go in the frame's vertex pool (graphics state + 0x64/0x68, 128 KB
  each); a grid twice as far needs about four times the vertices.
- The ground is drawn relative to a fixed anchor (`terrain_precision.cpp`), so
  a bigger grid only changes how many cells are built, not how they're placed.

## Next time
Find what sets the grid's size and placement (the camera fields read at the top
of `ovl1_func_8012F864`), whether the height data for farther cells is in
memory, and whether the vertex pool has room. Then the object/fog work above
can follow the bigger grid.
