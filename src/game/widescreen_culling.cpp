// Culling for the widescreen view.
//
// The game drops objects and effects outside its 4:3 view before drawing
// them: the object cull (func_80030824) by a half-width slope (camera +
// 0x50), the effect, beam and particle checks (func_80030410,
// func_800304F4) by a half-width on screen (camera + 0x1D8). In Expand the
// view is wider than that, so things at its sides vanished before reaching
// the screen edge. Both are widened by the widescreen factor where they're
// read. (The smoke sprites' own check is widened in widescreen_sprites.cpp.)
//
// The ground is built to cover the view too: the camera setup
// (func_8002B8FC) works out the corners of the view's footprint on the ground,
// two at a near and two at a far distance (camera + 0x5C to + 0x78, x and z
// from the camera), and the box around them (+ 0x7C to + 0x88), which sets
// the ground grid's extent; the ground builders (ovl1 func_8012F864,
// func_8012EAA8, func_8012F2C0) then skip cells outside the view by the same
// half-width slope. The corners are moved out from their midpoints by the
// widescreen factor (the box follows), and the builders' slope is widened, so
// the ground reaches a widescreen view's sides instead of ending short of
// them (plain to see in split screen).
#include <algorithm>
#include <cstring>

#include "recomp.h"

#include "bz_recomp_hooks.h"
#include "bz_widescreen.h"

// A read of a cull's view half-width (slope or screen units), just loaded.
extern "C" void bz_cull_half_width(float* half_width) {
    *half_width *= bz::get_widescreen_scale();
}

namespace {
    float load_f32(uint8_t* rdram, gpr base, int offset) {
        const int32_t bits = MEM_W(offset, base);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    void store_f32(uint8_t* rdram, gpr base, int offset, float value) {
        int32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        MEM_W(offset, base) = bits;
    }
}

// func_8002B8FC, before it stores the footprint's box: the camera, and the
// box (min x, min z, max x, max z) in registers.
extern "C" void bz_cull_ground_footprint(uint8_t* rdram, int32_t camera, float* min_x, float* min_z,
                                         float* max_x, float* max_z) {
    const float scale = bz::get_widescreen_scale();
    if (scale <= 1.0f) {
        return;
    }
    const gpr cam = (gpr)camera;
    // Corner pairs, left and right at one distance: (+0x5C, +0x64), (+0x6C, +0x74).
    const int pairs[2][2] = { { 0x5C, 0x64 }, { 0x6C, 0x74 } };
    float x[4], z[4];
    int n = 0;
    for (const auto& pair : pairs) {
        const float ax = load_f32(rdram, cam, pair[0]), az = load_f32(rdram, cam, pair[0] + 4);
        const float bx = load_f32(rdram, cam, pair[1]), bz_ = load_f32(rdram, cam, pair[1] + 4);
        const float mx = (ax + bx) * 0.5f, mz = (az + bz_) * 0.5f;
        x[n] = mx + (ax - mx) * scale; z[n] = mz + (az - mz) * scale; n++;
        x[n] = mx + (bx - mx) * scale; z[n] = mz + (bz_ - mz) * scale; n++;
        store_f32(rdram, cam, pair[0], x[n - 2]);
        store_f32(rdram, cam, pair[0] + 4, z[n - 2]);
        store_f32(rdram, cam, pair[1], x[n - 1]);
        store_f32(rdram, cam, pair[1] + 4, z[n - 1]);
    }
    *min_x = std::min({ *min_x, x[0], x[1], x[2], x[3] });
    *min_z = std::min({ *min_z, z[0], z[1], z[2], z[3] });
    *max_x = std::max({ *max_x, x[0], x[1], x[2], x[3] });
    *max_z = std::max({ *max_z, z[0], z[1], z[2], z[3] });
}
