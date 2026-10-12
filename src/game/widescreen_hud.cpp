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
//   func_801BB09C. Each string is anchored by its own x, so a panel's text
//   follows its panel and a string is never split. It draws through a
//   register copy of the overlay list's write pointer ($s3), so commands go
//   through that register.
//
// Anchored pieces also move a little further out: the original layout keeps
// clear of the TV's overscan, which a PC display doesn't have.
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstdio>

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
    constexpr int kEdgeMargin = 12 * 4;          // moved this much further out
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

    // Text queue entries are in 320-wide coordinates.
    constexpr int kTextLeftBelow = 107;    // left third
    constexpr int kTextRightFrom = 220;    // the weapon panel's column

    enum class Side { None, Left, Right };

    Side main_side = Side::None;
    Side line_side = Side::None;
    Side overlay_side = Side::None;
    bool text_active = false;
    bool log_text = false;

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

    Side text_side(int x) {
        if (x < kTextLeftBelow) return Side::Left;
        if (x >= kTextRightFrom) return Side::Right;
        return Side::None;
    }
}

void bz::set_widescreen_scale(float scale) {
    widescreen_scale.store(scale, std::memory_order_relaxed);
}

float bz::get_widescreen_scale() {
    return widescreen_scale.load(std::memory_order_relaxed);
}

extern "C" void bz_hud_radar_begin(uint8_t* rdram) {
    set_side(rdram, Side::Left);
}

extern "C" void bz_hud_piece_end(uint8_t* rdram) {
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
    set_side(rdram, side);
}

// func_801BB09C (the text flush) is about to run for the in-game frame.
extern "C" void bz_hud_text_begin(uint8_t* rdram) {
    text_active = active();
    static bool checked = false;
    if (!checked) {
        checked = true;
        log_text = std::getenv("BZ_LOG_HUD_TEXT") != nullptr;
    }
}

extern "C" void bz_hud_text_end(uint8_t* rdram) {
    text_active = false;
}

// func_80033604, before each line of a queued string is drawn: the queue
// entry in $s0, the overlay list's write pointer in $s3.
extern "C" void bz_hud_text_line(uint8_t* rdram, int32_t entry, uint64_t* head_reg) {
    if (!text_active) {
        return;
    }
    int x = (int16_t)MEM_H(4, (gpr)entry);
    if (log_text) {
        int y = (int16_t)MEM_H(6, (gpr)entry);
        fprintf(stderr, "[hud text] x %d y %d id %u\n", x, y, (uint16_t)MEM_HU(0, (gpr)entry));
    }
    Side to = text_side(x);
    DlWriter dl{ rdram, (gpr)*head_reg };
    write_side(dl, overlay_side, to);
    *head_reg = (uint64_t)dl.head;
    overlay_side = to;
}

// func_80033604, after the last string, before it returns the write pointer.
extern "C" void bz_hud_text_done(uint8_t* rdram, uint64_t* head_reg) {
    if (overlay_side == Side::None) {
        return;
    }
    DlWriter dl{ rdram, (gpr)*head_reg };
    write_side(dl, overlay_side, Side::None);
    *head_reg = (uint64_t)dl.head;
    overlay_side = Side::None;
}
