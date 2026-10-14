// Widescreen effect sprites (smoke, puffs, explosions).
//
// Two routines draw the game's billboard sprites: ovl1 func_8016765C and
// func_80166BD0, both subsystem draw methods. Each projects a sprite with the
// camera's original 4:3 projection and:
//   - skips it when |x| - size reaches a half-width kept in the camera
//     (+0x1D8), the edge of the 4:3 view;
//   - clamps a rectangle that crosses the left or top edge to 0, moving the
//     texture start along with it, since G_TEXRECT can't hold negative
//     coordinates;
//   - writes it as G_TEXRECT, G_RDPHALF_1 (s, t), G_RDPHALF_2 (dsdx, dtdy).
// In Expand, RT64 places such rectangles where they belong relative to the
// widened 3D view, but none are drawn past the 4:3 edges, so smoke stopped at
// the old screen edge.
//
// Hooks: the cull check's half-width is widened by the widescreen factor; the
// unclamped rectangle is saved where it's computed; once a sprite's three
// commands are written they're replaced by an extended texture rectangle
// (gEXTextureRectangle, same size) with the unclamped coordinates and texture
// start; and the batch is drawn with the scissor opened to the widened view.
#include <atomic>
#include <cmath>
#include <cstdint>

#include "recomp.h"

#include "bz_recomp_hooks.h"
#include "bz_widescreen.h"

namespace {
    constexpr uint16_t kOriginLeft = 0x0;
    constexpr uint16_t kOriginRight = 0x400;
    constexpr uint16_t kOriginNone = 0x800;
    constexpr int kViewBorderX = 16 * 4;          // the game's 3D scissor, 10.2
    constexpr int kViewTop = 8 * 4;
    constexpr int kViewBottom = (240 - 8) * 4;

    // Per drawer: the unclamped rectangle of the sprite being written (10.2),
    // and whether this batch was opened up.
    struct Saved {
        int ulx, uly, lrx, lry;
        bool valid;
    };
    Saved saved[2];
    bool batch_open[2];

    bool active() {
        return bz::get_widescreen_scale() > 1.0f;
    }

    uint32_t pack16(int hi, int lo) {
        return ((uint32_t)(uint16_t)(int16_t)hi << 16) | (uint16_t)(int16_t)lo;
    }

    // Writes commands at the register-held write pointer `head` (with `tail`,
    // the drawer's second pointer, kept 4 bytes behind it).
    struct RegWriter {
        uint8_t* rdram;
        uint64_t* head;
        uint64_t* tail;
        void cmd(uint32_t w0, uint32_t w1) {
            const gpr h = (gpr)*head;
            MEM_W(0, h) = (int32_t)w0;
            MEM_W(4, h) = (int32_t)w1;
            *head += 8;
            *tail += 8;
        }
    };
}

extern "C" void bz_sprites_begin(uint8_t* rdram, int drawer, uint64_t* head, uint64_t* tail) {
    batch_open[drawer] = active();
    saved[drawer].valid = false;
    if (!batch_open[drawer]) {
        return;
    }
    const int border = (int)std::lround(kViewBorderX * bz::get_widescreen_scale());
    RegWriter dl{ rdram, head, tail };
    dl.cmd(0xE0525464, 0x10000064);                                   // gEXEnable
    dl.cmd(0x64000017, 0);                                            // gEXPushScissor
    dl.cmd(0x64000005, (kOriginLeft << 2) | (kOriginRight << 14));    // gEXSetScissor: the widened view
    dl.cmd(pack16(border, kViewTop), pack16(-border, kViewBottom));
}

extern "C" void bz_sprites_end(uint8_t* rdram, int drawer, uint64_t* head) {
    if (!batch_open[drawer]) {
        return;
    }
    batch_open[drawer] = false;
    uint64_t unused = 0;
    RegWriter dl{ rdram, head, &unused };
    dl.cmd(0x64000018, 0);                                            // gEXPopScissor
}

// The cull check's half-width (in $f2 just before the compare).
extern "C" void bz_sprites_cull_width(float* half_width) {
    *half_width *= bz::get_widescreen_scale();
}

// The sprite's centre and half-size, before any clamping (10.2).
extern "C" void bz_sprites_rect(int drawer, int32_t cx, int32_t half_w, int32_t cy, int32_t half_h) {
    saved[drawer] = { cx - half_w, cy - half_h, cx + half_w, cy + half_h, true };
}

// After a sprite's G_TEXRECT, G_RDPHALF_1, G_RDPHALF_2 are written, ending at
// `end` (the last command's second word).
extern "C" void bz_sprites_written(uint8_t* rdram, int drawer, int32_t end) {
    Saved& s = saved[drawer];
    if (!batch_open[drawer] || !s.valid) {
        return;
    }
    s.valid = false;
    const gpr rect = (gpr)end - 20;
    const uint32_t w0 = (uint32_t)MEM_W(0, rect);
    if ((w0 >> 24) != 0xE4 || ((uint32_t)MEM_W(8, rect) >> 24) != 0xE1 || ((uint32_t)MEM_W(16, rect) >> 24) != 0xF1) {
        return;
    }
    const uint32_t tile = ((uint32_t)MEM_W(4, rect) >> 24) & 0x7;
    const uint32_t st = (uint32_t)MEM_W(12, rect);
    const uint32_t steps = (uint32_t)MEM_W(20, rect);
    const int16_t dsdx = (int16_t)(steps >> 16), dtdy = (int16_t)(steps & 0xFFFF);
    int texs = (int16_t)(st >> 16), text = (int16_t)(st & 0xFFFF);
    // Undo the clamp's texture shift (the game's own arithmetic: (edge * step) >> 7).
    if (s.ulx < 0) texs += (s.ulx * dsdx) >> 7;
    if (s.uly < 0) text += (s.uly * dtdy) >> 7;

    MEM_W(0, rect) = (int32_t)0x64000002u;                            // gEXTextureRectangle
    MEM_W(4, rect) = (int32_t)(tile | (kOriginNone << 3) | (kOriginNone << 15));
    MEM_W(8, rect) = (int32_t)pack16(s.ulx, s.uly);
    MEM_W(12, rect) = (int32_t)pack16(s.lrx, s.lry);
    MEM_W(16, rect) = (int32_t)pack16(texs, text);
    MEM_W(20, rect) = (int32_t)pack16(dsdx, dtdy);
}
