// Widescreen HUD (RT64's Expand aspect ratio).
//
// In Expand, RT64 widens the 3D view but keeps 2D drawing in the centred 4:3
// area unless the display list anchors it to a screen edge (extended GBI
// gEXSetRectAlign and friends; the Graphics tab's HUD Ratio then sets how far
// out an anchored edge goes). Commands are written straight into the game's
// display lists around the HUD pieces that sit at the sides, and reset after
// each.
//
// The in-game frame (ovl1 func_80122FB0) draws into three display lists held
// by the graphics state at *(0x800A4768), each run with its own microcode
// (func_8002B12C sets them up): the main list (write pointer at +0x78), a
// line list (+0x84) and a 2D overlay list (+0x90). Anchored pieces:
//
// - Radar: func_80158A10 (backdrop, main list) and func_80158C6C (grid in the
//   line list, blips and compass letters; its own viewport and scissors),
//   called straight from func_80122FB0. Anchored left, viewport and scissors
//   included.
// - HUD elements: func_80163FEC walks the current layout's element list and
//   calls each element's virtual draw. The unit menu (draw func_8015C2A8) is
//   anchored left, the weapon panel (draw func_801623B0) right. The rest (the
//   unit icons and counters, the crosshair, the message box) stay centred.
// - Text: queued by everything above and drawn together by the boot segment's
//   func_80033604 (one 0x28-byte queue entry per string, x at +4), called from
//   func_801BB09C. Each string follows the element that queued it (hooks on
//   the queuing functions record which side was being drawn), so a panel's
//   text moves with its panel and a string is never split. The target
//   markers' names and distances (widescreen_markers.cpp) stay centred but
//   may go past the 4:3 edges: their scissor is opened to the widened view
//   and their letters are written as extended rectangles, which can start
//   left of the screen. It draws through a
//   register copy of the overlay list's write pointer ($s3), so commands go
//   through that register.
//
// Anchored pieces also move a little further out: the original layout keeps
// clear of the TV's overscan, which a PC display doesn't have.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>

#include "recomp.h"

#include "bz_recomp_hooks.h"
#include "bz_widescreen.h"

namespace {
    std::atomic<float> widescreen_scale{ 1.0f };

    constexpr uint16_t kOriginLeft = 0x0;    // G_EX_ORIGIN_LEFT
    constexpr uint16_t kOriginRight = 0x400; // G_EX_ORIGIN_RIGHT
    constexpr uint16_t kOriginNone = 0x800;  // G_EX_ORIGIN_NONE

    // The in-game framebuffer is 640x240 (pixels half as wide as tall).
    constexpr int kFbWidth = 640;
    constexpr int kFbHeight = 240;
    // RT64 adds the framebuffer width to right-anchored coordinates
    // (RDP::movedFromOrigin), so they're given relative to the right edge.
    constexpr int kRightShift = -kFbWidth * 4;   // 10.2 fixed point
    constexpr int kEdgeMargin = 4 * 4;           // moved this much further out
    // The game clips its 3D view (and everything else it means to show) 16
    // pixels in from the sides and 8 from the top and bottom, inside the TV's
    // overscan. HUD pieces it parks off-screen sit in that border, so anchored
    // drawing is clipped to the same view area -- widened like the 3D view.
    constexpr int kViewBorderX = 16 * 4;
    constexpr int kViewTop = 8 * 4;
    constexpr int kViewBottom = (kFbHeight - 8) * 4;

    constexpr uint32_t kGfxStatePtr = 0x800A4768;
    constexpr int kMainListHead = 0x78;      // F3DEX2
    constexpr int kLineListHead = 0x84;      // L3DEX2 (the radar grid)
    constexpr int kOverlayListHead = 0x90;

    // Wide: centred, but drawn across the whole widened view (marker text).
    enum class Side { None, Left, Right, Wide };

    constexpr uint16_t kOriginNoneBits = kOriginNone;
    Side main_side = Side::None;
    Side line_side = Side::None;
    Side overlay_side = Side::None;
    bool text_active = false;

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    bool active() {
        return widescreen_scale.load(std::memory_order_relaxed) > 1.0f;
    }

    struct DlWriter {
        uint8_t* rdram;
        gpr head;
        void cmd(uint32_t w0, uint32_t w1) {
            MEM_W(0, head) = (int32_t)w0;
            MEM_W(4, head) = (int32_t)w1;
            head += 8;
        }
    };

    uint32_t pack16(int hi, int lo) {
        return ((uint32_t)(uint16_t)(int16_t)hi << 16) | (uint16_t)(int16_t)lo;
    }

    // Switches the anchoring at the display-list write position `dl`.
    void write_side(DlWriter& dl, Side from, Side to) {
        if (from == Side::Wide) {
            from = Side::None;
        }
        if (to == Side::Wide) {
            to = Side::None;
        }
        if (from == to) {
            return;
        }
        // The view border and window width in anchored coordinates, which
        // count from the window edge in the game's (unwidened) pixels.
        const float scale = std::max(widescreen_scale.load(std::memory_order_relaxed), 1.0f);
        const int border = (int)std::lround(kViewBorderX * scale);
        const int window_width = (int)std::lround(kFbWidth * 4 * scale);
        dl.cmd(0xE0525464, 0x10000064); // gEXEnable (G_SPNOOP + RT64 magic, opcode 0x64)
        if (to != Side::None) {
            const uint16_t origin = (to == Side::Left) ? kOriginLeft : kOriginRight;
            const int shift = (to == Side::Left) ? -kEdgeMargin : (kRightShift + kEdgeMargin);
            if (from == Side::None) {
                dl.cmd(0x64000017, 0);                                     // gEXPushScissor
            }
            // RT64 clips to the current scissor, which the game set for the
            // centred 4:3 area (or for the other side): open it to the whole
            // widened view. The game's own scissors from here on follow the
            // anchor, kept inside the view by the bounds.
            dl.cmd(0x64000005, (kOriginLeft << 2) | (kOriginRight << 14)); // gEXSetScissor
            dl.cmd(pack16(border, kViewTop), pack16(-border, kViewBottom));
            dl.cmd(0x64000006, origin | (origin << 12));                   // gEXSetRectAlign
            dl.cmd(pack16(shift, 0), pack16(shift, 0));
            dl.cmd(0x64000008, origin | (origin << 12));                   // gEXSetScissorAlign
            dl.cmd(pack16(shift, 0), pack16(shift, 0));
            if (to == Side::Left) {
                dl.cmd(pack16(border, kViewTop), pack16(window_width - border, kViewBottom));
            }
            else {
                // Right-anchored coordinates end at the framebuffer width.
                dl.cmd(pack16(0, kViewTop), pack16(kFbWidth * 4 - border, kViewBottom));
            }
            dl.cmd(0x64000007, origin);                                    // gEXSetViewportAlign
            dl.cmd(pack16(shift, 0), 0);
        }
        else {
            dl.cmd(0x64000006, kOriginNone | (kOriginNone << 12));
            dl.cmd(0, 0);
            dl.cmd(0x64000008, kOriginNone | (kOriginNone << 12));
            dl.cmd(0, 0);
            dl.cmd(0, 0xFFFFFFFF);
            dl.cmd(0x64000007, kOriginNone);
            dl.cmd(0, 0);
            dl.cmd(0x64000018, 0);                                         // gEXPopScissor
        }
    }

    // Through a display list's write pointer kept in the graphics state.
    void set_list_side(uint8_t* rdram, int head_offset, Side& state, Side to) {
        if (state == to) {
            return;
        }
        gpr gfx = kseg0((uint32_t)MEM_W(0, kseg0(kGfxStatePtr)));
        gpr head_ptr = gfx + head_offset;
        DlWriter dl{ rdram, kseg0((uint32_t)MEM_W(0, head_ptr)) };
        write_side(dl, state, to);
        MEM_W(0, head_ptr) = (int32_t)(uint32_t)dl.head;
        state = to;
    }

    void set_side(uint8_t* rdram, Side to) {
        if (to != Side::None && !active()) {
            to = Side::None;
        }
        set_list_side(rdram, kMainListHead, main_side, to);
        set_list_side(rdram, kLineListHead, line_side, to);
        set_list_side(rdram, kOverlayListHead, overlay_side, to);
    }

    // The side of the HUD element being drawn (whether or not Expand is on),
    // and, per text queue slot, the side of the element that queued it.
    constexpr uint32_t kTextQueue = 0x800BF460;   // 0x28-byte entries
    constexpr uint32_t kTextQueueCount = 0x8006B150;
    // func_800338C0 writes a line's G_SETSCISSOR only when it differs from
    // the last one, kept here.
    constexpr uint32_t kTextScissorCache = 0x800BF458;
    Side element_side = Side::None;
    Side queued_side[256];
    bool wide_line = false;
}

void bz::set_widescreen_scale(float scale) {
    widescreen_scale.store(scale, std::memory_order_relaxed);
}

float bz::get_widescreen_scale() {
    return widescreen_scale.load(std::memory_order_relaxed);
}

extern "C" void bz_hud_radar_begin(uint8_t* rdram) {
    element_side = Side::Left;
    set_side(rdram, Side::Left);
}

extern "C" void bz_hud_piece_end(uint8_t* rdram) {
    bz::markers_end(rdram);
    element_side = Side::None;
    set_side(rdram, Side::None);
}

// func_80163FEC, before an element's virtual draw: the element in $v1.
extern "C" void bz_hud_element_begin(uint8_t* rdram, int32_t element) {
    gpr vtable = kseg0((uint32_t)MEM_W(4, (gpr)element));
    uint32_t draw = (uint32_t)MEM_W(0x3C, vtable);
    Side side = Side::None;
    switch (draw) {
    case 0x8015C2A8: side = Side::Left; break;   // unit menu
    case 0x801623B0: side = Side::Right; break;  // weapon panel
    default: break;
    }
    element_side = side;
    set_side(rdram, side);
    if (draw == 0x801ADCC0) {                    // target markers
        bz::markers_begin(rdram);
        if (bz::markers_drawing()) {
            element_side = Side::Wide;
        }
    }
}

// func_80033EA0 / func_80032DE4 / func_80032F70, which queue a string: it takes the slot at
// the queue's current count. The string follows the element queuing it, so a
// panel's text moves with its panel, and text placed in the 3D view (target
// labels) stays put.
extern "C" void bz_hud_text_queued(uint8_t* rdram) {
    queued_side[MEM_BU(0, kseg0(kTextQueueCount)) & 0xFF] = element_side;
}

// func_801BB09C (the text flush) is about to run for the in-game frame.
extern "C" void bz_hud_text_begin(uint8_t* /*rdram*/) {
    text_active = active();
}

extern "C" void bz_hud_text_end(uint8_t* /*rdram*/) {
    text_active = false;
}

// func_80033604, before each line of a queued string is drawn: the queue
// entry in $s0, the overlay list's write pointer in $s3.
extern "C" void bz_hud_text_line(uint8_t* rdram, int32_t entry, uint64_t* head_reg) {
    if (!text_active) {
        return;
    }
    const uint32_t slot = ((uint32_t)entry - kTextQueue) / 0x28;
    Side to = (slot < 256) ? queued_side[slot] : Side::None;
    DlWriter dl{ rdram, (gpr)*head_reg };
    write_side(dl, overlay_side, to);
    *head_reg = (uint64_t)dl.head;
    overlay_side = to;
    // Into or out of a wide line, the line's own scissor has to be written
    // (and is then rewritten by bz_hud_text_scissor, or sets the 4:3 one
    // back).
    if (to == Side::Wide || wide_line) {
        MEM_W(0, kseg0(kTextScissorCache)) = 0;
        MEM_W(4, kseg0(kTextScissorCache)) = 0;
    }
    wide_line = (to == Side::Wide);
}

// func_800338C0, after the line's G_SETSCISSOR (written at $t7 - 8 when it
// changed): for a wide line, the view's scissor widened like the 3D view.
extern "C" void bz_hud_text_scissor(uint8_t* rdram, uint64_t* head_reg) {
    if (!text_active || !wide_line) {
        return;
    }
    const gpr scissor = (gpr)*head_reg - 8;
    const uint32_t w0 = (uint32_t)MEM_W(0, scissor), w1 = (uint32_t)MEM_W(4, scissor);
    if ((w0 >> 24) != 0xED) {
        return;
    }
    const int border = (int)std::lround(kViewBorderX * std::max(widescreen_scale.load(std::memory_order_relaxed), 1.0f));
    DlWriter dl{ rdram, scissor };
    dl.cmd(0x64000005, (kOriginLeft << 2) | (kOriginRight << 14));    // gEXSetScissor
    dl.cmd(pack16(border, w0 & 0xFFF), pack16(-border, w1 & 0xFFF));
    *head_reg = (uint64_t)dl.head;
}

// func_800338C0, once a letter's G_TEXRECT, G_RDPHALF_1, G_RDPHALF_2 are
// written (ending at `end`, the last command's second word): for a wide
// line, an extended rectangle with the letter's unclamped position, kept by
// the line state at `line` ($t8; shifts to 10.2 on the stack).
extern "C" void bz_hud_text_glyph(uint8_t* rdram, int32_t end, int32_t line, int32_t sp) {
    if (!text_active || !wide_line) {
        return;
    }
    const gpr rect = (gpr)end - 20;
    const uint32_t w0 = (uint32_t)MEM_W(0, rect);
    if ((w0 >> 24) != 0xE4 || ((uint32_t)MEM_W(8, rect) >> 24) != 0xE1 || ((uint32_t)MEM_W(16, rect) >> 24) != 0xF1) {
        return;
    }
    const int sx = MEM_W(8, (gpr)sp), sy = MEM_W(0xC, (gpr)sp);
    const int ulx = MEM_W(0x404, (gpr)line) << sx;
    const int lrx = MEM_W(0x40C, (gpr)line) << sx;
    const int lry = MEM_W(0x410, (gpr)line) << sy;
    const int uly = lry - ((lry - (int)((uint32_t)MEM_W(4, rect) & 0xFFF)) & 0xFFF);
    const uint32_t tile = ((uint32_t)MEM_W(4, rect) >> 24) & 0x7;
    const uint32_t st = (uint32_t)MEM_W(12, rect);
    const uint32_t steps = (uint32_t)MEM_W(20, rect);
    MEM_W(0, rect) = (int32_t)0x64000002u;                            // gEXTextureRectangle
    MEM_W(4, rect) = (int32_t)(tile | (kOriginNoneBits << 3) | (kOriginNoneBits << 15));
    MEM_W(8, rect) = (int32_t)pack16(ulx, uly);
    MEM_W(12, rect) = (int32_t)pack16(lrx, lry);
    MEM_W(16, rect) = (int32_t)st;
    MEM_W(20, rect) = (int32_t)steps;
}

// func_80033604, after the last string, before it returns the write pointer.
extern "C" void bz_hud_text_done(uint8_t* rdram, uint64_t* head_reg) {
    for (Side& side : queued_side) {
        side = Side::None;
    }
    DlWriter dl{ rdram, (gpr)*head_reg };
    if (wide_line) {
        // The last line's scissor (the 4:3 view's) as the game wrote it.
        wide_line = false;
        dl.cmd((uint32_t)MEM_W(0, kseg0(kTextScissorCache)), (uint32_t)MEM_W(4, kseg0(kTextScissorCache)));
        *head_reg = (uint64_t)dl.head;
    }
    if (overlay_side == Side::None || overlay_side == Side::Wide) {
        overlay_side = Side::None;
        return;
    }
    write_side(dl, overlay_side, Side::None);
    *head_reg = (uint64_t)dl.head;
    overlay_side = Side::None;
}
