// Culling for the widescreen view.
//
// The game drops objects and effects outside its 4:3 view before drawing
// them: the object cull (func_80030824) by a half-width slope (camera +
// 0x50), the effect, beam and particle checks (func_80030410,
// func_800304F4) by a half-width on screen (camera + 0x1D8). In Expand the
// view is wider than that, so things at its sides vanished before reaching
// the screen edge. Both are widened by the widescreen factor where they're
// read. (The smoke sprites' own check is widened in widescreen_sprites.cpp.)
#include "bz_recomp_hooks.h"
#include "bz_widescreen.h"

// A read of a cull's view half-width (slope or screen units), just loaded.
extern "C" void bz_cull_half_width(float* half_width) {
    *half_width *= bz::get_widescreen_scale();
}
