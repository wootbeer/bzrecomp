// Ground jitter.
//
// The ground is rebuilt every frame by ovl1 func_8012F864 (or its
// alternates, func_8012EAA8 and func_8012F2C0, all called from
// func_80130C44) as a grid of N64 vertices relative to the camera,
// in units of a tenth of a world unit. The grid's starting X and Z and the
// camera height are each truncated to whole units once, and every vertex
// reuses them, so the whole ground sits up to one unit off where it should,
// by an amount that changes every frame as the camera moves. On the N64's
// screen that was a fraction of a pixel; upscaled, the ground visibly shakes
// against everything else, worst near the camera and when it swings.
//
// The builders still hold the exact float values next to the truncated ones
// when the grid starts. A hook there loads a modelview matrix that moves the
// ground back by the error (the block's own static setup list has just loaded
// the usual one from segment 0x0A), with the matrix data stored in the
// display list itself behind a branch. Where the ground block ends, the
// segment 0x0A matrix is loaded again for whatever follows.
#include <cmath>
#include <cstdint>

#include "recomp.h"

#include "bz_recomp_hooks.h"

namespace {
    constexpr int kMainListHead = 0x78; // in the graphics state

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    uint32_t fixed(float value) {
        return (uint32_t)(int32_t)std::lround(value * 65536.0f);
    }

    // N64 Mtx: 4x4 s15.16, the integer halves of all sixteen elements first,
    // then the fractions, two elements to a word.
    void write_translation_mtx(uint8_t* rdram, gpr mtx, float x, float y, float z) {
        uint32_t elements[16] = {
            fixed(1.0f), 0, 0, 0,
            0, fixed(1.0f), 0, 0,
            0, 0, fixed(1.0f), 0,
            fixed(x), fixed(y), fixed(z), fixed(1.0f),
        };
        for (int i = 0; i < 8; i++) {
            const uint32_t a = elements[2 * i], b = elements[2 * i + 1];
            MEM_W(4 * i, mtx) = (int32_t)((a & 0xFFFF0000u) | (b >> 16));
            MEM_W(32 + 4 * i, mtx) = (int32_t)((a << 16) | (b & 0xFFFFu));
        }
    }

    // Writes the corrected modelview at `head`; returns the new write position.
    // Vertices store x_int + n*step, z_int + n*step and height - y_int, where
    // the ints are the exact values truncated (trunc.w.s, as C's conversion),
    // so the error is undone by translating by (x - x_int, y_int - y, z - z_int).
    uint32_t write_correction(uint8_t* rdram, uint32_t head, float x, float y, float z) {
        const gpr h = kseg0(head);
        MEM_W(0x0, h) = (int32_t)0xDA380003u;      // gSPMatrix(modelview, load)
        MEM_W(0x4, h) = (int32_t)(head + 16);
        MEM_W(0x8, h) = (int32_t)0xDE010000u;      // gSPBranchList past the matrix data
        MEM_W(0xC, h) = (int32_t)(head + 16 + 64);
        write_translation_mtx(rdram, h + 16, x - (float)(int32_t)x, (float)(int32_t)y - y, z - (float)(int32_t)z);
        return head + 16 + 64;
    }

    // The usual modelview again (the setup lists load it from segment 0x0A).
    uint32_t write_restore(uint8_t* rdram, uint32_t head) {
        const gpr h = kseg0(head);
        MEM_W(0x0, h) = (int32_t)0xDA380003u;
        MEM_W(0x4, h) = (int32_t)0x0A000000u;
        return head + 8;
    }
}

// Before the grid is built, with the exact camera-relative start (x, z) and
// camera height (y): func_8012EAA8 / func_8012F2C0 write through the graphics
// state's main-list pointer, func_8012F864 through a register copy of it.
extern "C" void bz_terrain_begin(uint8_t* rdram, int32_t gfx_state, float x, float y, float z) {
    const gpr state = (gpr)gfx_state;
    MEM_W(kMainListHead, state) = (int32_t)write_correction(rdram, (uint32_t)MEM_W(kMainListHead, state), x, y, z);
}

extern "C" void bz_terrain_begin_reg(uint8_t* rdram, uint64_t* head_reg, float x, float y, float z) {
    *head_reg = (uint64_t)(int64_t)(int32_t)write_correction(rdram, (uint32_t)*head_reg, x, y, z);
}

// Where the ground block ends.
extern "C" void bz_terrain_end(uint8_t* rdram, int32_t gfx_state) {
    const gpr state = (gpr)gfx_state;
    MEM_W(kMainListHead, state) = (int32_t)write_restore(rdram, (uint32_t)MEM_W(kMainListHead, state));
}

extern "C" void bz_terrain_end_reg(uint8_t* rdram, uint64_t* head_reg) {
    *head_reg = (uint64_t)(int64_t)(int32_t)write_restore(rdram, (uint32_t)*head_reg);
}
