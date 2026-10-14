// Widescreen target markers (the brackets, names, distances and edge arrows
// over units and places in the 3D view).
//
// They're a HUD element (draw ovl1 func_801ADCC0, in the overlay list) that
// projects each marked object with the game's 4:3 projection and:
//   - hides a marker, or pins its name to the screen edge with an arrow, by
//     comparing against the 4:3 view rectangle (func_801BBF10/func_801BBF94,
//     a local rectangle in 320-wide units; func_801AEAFC, called from it,
//     does the same);
//   - draws images with G_TEXRECT (func_801BA01C), boxes with G_FILLRECT
//     (func_801BBD04) and clips some of them with a scissor (func_801BBB94),
//     then sets the 4:3 view scissor back (func_801BBC54). Rectangles can't
//     hold negative coordinates, so the game clamps them at the left and top
//     edges;
//   - queues the names and distances as text (see widescreen_hud.cpp).
// In Expand, RT64 places all of this relative to the widened 3D view, but it
// stopped at the 4:3 edges.
//
// Only views that span the screen's width are widened by RT64 (in four-player
// split screen the picture stays 4:3), so only those markers are opened up,
// and only within their own view's height.
//
// Hooks: the view rectangle is widened by the extra width on each side, each
// rectangle the drawers write is rewritten as RT64's extended rectangle with
// its unclamped coordinates, the scissors as extended scissors (the view's
// one widened like the 3D view), and the whole element is drawn with the
// scissor opened to the widened view.
#include <cmath>
#include <cstdint>

#include "recomp.h"

#include "bz_recomp_hooks.h"
#include "bz_widescreen.h"

namespace {
    constexpr uint16_t kOriginLeft = 0x0;
    constexpr uint16_t kOriginRight = 0x400;
    constexpr uint16_t kOriginNone = 0x800;
    constexpr int kFbWidth = 640;
    constexpr int kViewBorderX = 16 * 4;          // the game's 3D scissor, 10.2

    constexpr uint32_t kGfxStatePtr = 0x800A4768;
    constexpr int kOverlayListHead = 0x90;
    constexpr uint32_t kShiftX = 0x800A4758;      // framebuffer width / 320, as a shift
    constexpr uint32_t kShiftY = 0x800A4759;

    bool drawing = false;
    int fill_args[4];
    int scissor_args[2];

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    uint32_t pack16(int hi, int lo) {
        return ((uint32_t)(uint16_t)(int16_t)hi << 16) | (uint16_t)(int16_t)lo;
    }

    void put(uint8_t* rdram, gpr at, uint32_t w0, uint32_t w1) {
        MEM_W(0, at) = (int32_t)w0;
        MEM_W(4, at) = (int32_t)w1;
    }

    // The widened view's scissor (gEXSetScissor, 16 bytes) at `at`.
    void put_view_scissor(uint8_t* rdram, gpr at, int uly, int lry) {
        const int border = (int)std::lround(kViewBorderX * bz::get_widescreen_scale());
        put(rdram, at, 0x64000005, (kOriginLeft << 2) | (kOriginRight << 14));
        put(rdram, at + 8, pack16(border, uly), pack16(-border, lry));
    }

    // The view being drawn: its rectangle (framebuffer pixels: x0, y0, x1, y1
    // as halfwords at +0x10) hangs off the current view, *(0x801BD2C0) + 0x18.
    constexpr uint32_t kCurrentView = 0x801BD2C0;
    constexpr int kViewEdgeTolerance = 16;

    gpr overlay_head_ptr(uint8_t* rdram) {
        return kseg0((uint32_t)MEM_W(0, kseg0(kGfxStatePtr))) + kOverlayListHead;
    }
}

bool bz::markers_drawing() {
    return drawing;
}

void bz::markers_begin(uint8_t* rdram) {
    drawing = false;
    if (bz::get_widescreen_scale() <= 1.0f) {
        return;
    }
    const gpr view_ptr = kseg0((uint32_t)MEM_W(0, kseg0(kCurrentView)));
    if (view_ptr == 0) {
        return;
    }
    const gpr view = kseg0((uint32_t)MEM_W(0x18, view_ptr));
    if (view == 0) {
        return;
    }
    const int x0 = MEM_H(0x10, view), y0 = MEM_H(0x12, view);
    const int x1 = MEM_H(0x14, view), y1 = MEM_H(0x16, view);
    if (x0 > kViewBorderX / 4 + kViewEdgeTolerance || x1 < kFbWidth - kViewBorderX / 4 - kViewEdgeTolerance) {
        return;                                                        // not a full-width view
    }
    drawing = true;
    const gpr head_ptr = overlay_head_ptr(rdram);
    gpr head = kseg0((uint32_t)MEM_W(0, head_ptr));
    put(rdram, head, 0xE0525464, 0x10000064);                          // gEXEnable
    put(rdram, head + 8, 0x64000017, 0);                               // gEXPushScissor
    put_view_scissor(rdram, head + 16, y0 * 4, y1 * 4);
    MEM_W(0, head_ptr) = (int32_t)(uint32_t)(head + 32);
}

void bz::markers_end(uint8_t* rdram) {
    if (!drawing) {
        return;
    }
    drawing = false;
    const gpr head_ptr = overlay_head_ptr(rdram);
    gpr head = kseg0((uint32_t)MEM_W(0, head_ptr));
    put(rdram, head, 0x64000018, 0);                                   // gEXPopScissor
    MEM_W(0, head_ptr) = (int32_t)(uint32_t)(head + 8);
}

// After func_801BBF94: the view rectangle {x0, y0, x1, y1} (words) the
// markers are kept in.
extern "C" void bz_markers_view_rect(uint8_t* rdram, int32_t rect) {
    if (!drawing) {
        return;
    }
    const int width = kFbWidth >> MEM_BU(0, kseg0(kShiftX));
    const int extra = (int)std::lround(width / 2 * (bz::get_widescreen_scale() - 1.0f));
    MEM_W(0, (gpr)rect) = MEM_W(0, (gpr)rect) - extra;
    MEM_W(8, (gpr)rect) = MEM_W(8, (gpr)rect) + extra;
}

// func_801BA01C, once its G_TEXRECT, G_RDPHALF_1, G_RDPHALF_2 are written
// (ending at `head`): the unclamped rectangle (10.2) and texture steps.
extern "C" void bz_markers_texrect(uint8_t* rdram, int32_t head, int32_t ulx, int32_t uly,
                                   int32_t width, int32_t height, int32_t dsdx, int32_t dtdy) {
    if (!drawing) {
        return;
    }
    const gpr rect = (gpr)head - 24;
    if (((uint32_t)MEM_W(0, rect) >> 24) != 0xE4 || ((uint32_t)MEM_W(8, rect) >> 24) != 0xE1 ||
        ((uint32_t)MEM_W(16, rect) >> 24) != 0xF1) {
        return;
    }
    const uint32_t tile = ((uint32_t)MEM_W(4, rect) >> 24) & 0x7;
    put(rdram, rect, 0x64000002, tile | (kOriginNone << 3) | (kOriginNone << 15)); // gEXTextureRectangle
    put(rdram, rect + 8, pack16(ulx, uly), pack16(ulx + width, uly + height));
    put(rdram, rect + 16, 0, pack16(dsdx, dtdy));                      // texture from its start
}

// func_801BBD04 (a box), on entry: its corners in 320x240 units.
extern "C" void bz_markers_fill_args(int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    fill_args[0] = x0;
    fill_args[1] = y0;
    fill_args[2] = x1;
    fill_args[3] = y1;
}

// func_801BBD04, once its G_FILLRECT (at `fill`) and the G_RDPPIPESYNC after
// it are written.
extern "C" void bz_markers_fill(uint8_t* rdram, int32_t fill) {
    if (!drawing || ((uint32_t)MEM_W(0, (gpr)fill) >> 24) != 0xF6) {
        return;
    }
    const int sx = MEM_BU(0, kseg0(kShiftX)) + 2;
    const int sy = MEM_BU(0, kseg0(kShiftY)) + 2;
    put(rdram, (gpr)fill, 0x64000003, kOriginNone | (kOriginNone << 12)); // gEXFillRectangle
    put(rdram, (gpr)fill + 8, pack16(fill_args[0] << sx, fill_args[1] << sy),
        pack16(fill_args[2] << sx, fill_args[3] << sy));
}

// func_801BBB94 (a scissor; it keeps the y edges in the view), on entry:
// its x edges in 320-wide units.
extern "C" void bz_markers_scissor_args(int32_t x0, int32_t x1) {
    scissor_args[0] = x0;
    scissor_args[1] = x1;
}

// func_801BBB94, before it returns: its G_SETSCISSOR at `scissor`.
extern "C" void bz_markers_scissor(uint8_t* rdram, int32_t scissor, uint64_t* head) {
    if (!drawing) {
        return;
    }
    const uint32_t w0 = (uint32_t)MEM_W(0, (gpr)scissor), w1 = (uint32_t)MEM_W(4, (gpr)scissor);
    const int sx = MEM_BU(0, kseg0(kShiftX)) + 2;
    put(rdram, (gpr)scissor, 0x64000005, (kOriginNone << 2) | (kOriginNone << 14)); // gEXSetScissor
    put(rdram, (gpr)scissor + 8, pack16(scissor_args[0] << sx, w0 & 0xFFF), pack16(scissor_args[1] << sx, w1 & 0xFFF));
    *head = (uint64_t)(int64_t)(scissor + 16);
}

// func_801BBC54 (the view's scissor set back), before it returns: its
// G_SETSCISSOR at `scissor`.
extern "C" void bz_markers_view_scissor(uint8_t* rdram, int32_t scissor, uint64_t* head) {
    if (!drawing) {
        return;
    }
    const uint32_t w0 = (uint32_t)MEM_W(0, (gpr)scissor), w1 = (uint32_t)MEM_W(4, (gpr)scissor);
    put_view_scissor(rdram, (gpr)scissor, w0 & 0xFFF, w1 & 0xFFF);
    *head = (uint64_t)(int64_t)(scissor + 16);
}
