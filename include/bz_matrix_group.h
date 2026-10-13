// RT64 extended GBI matrix groups (gEXMatrixGroup), which tell RT64's frame
// interpolation how to pair a matrix with last frame's, written straight into
// the game's display lists by hooks.
#ifndef BZ_MATRIX_GROUP_H
#define BZ_MATRIX_GROUP_H

#include <cstdint>

#include "recomp.h"

namespace bz {
    constexpr uint32_t kIdIgnore = 0x0;           // G_EX_ID_IGNORE: never interpolated
    constexpr uint32_t kIdAuto = 0xFFFFFFFFu;     // G_EX_ID_AUTO: RT64 pairs it by itself

    // RT64's default group: decomposed; position, rotation, scale, skew,
    // perspective, tiles and look-at automatic; vertices and texcoords
    // skipped; automatic ordering.
    constexpr uint32_t kDefaultGroupParams =
        (1u << 2) | (2u << 3) | (2u << 5) | (2u << 7) | (2u << 9) | (2u << 11) | (2u << 15) | (1u << 17) | (2u << 24);

    constexpr uint32_t kGroupBytes = 16;
    constexpr uint32_t kEnableBytes = 8;

    // gEXEnable.
    inline void write_gbi_enable(uint8_t* rdram, gpr h) {
        MEM_W(0x0, h) = (int32_t)0xE0525464u;
        MEM_W(0x4, h) = (int32_t)0x10000064u;
    }

    // gEXMatrixGroup for the modelview, no push: replaces the top of RT64's
    // group stack, which applies to matrices loaded from then on.
    inline void write_matrix_group(uint8_t* rdram, gpr h, uint32_t id, uint32_t params) {
        MEM_W(0x0, h) = (int32_t)0x6400000Cu;
        MEM_W(0x4, h) = (int32_t)id;
        MEM_W(0x8, h) = (int32_t)params;
        MEM_W(0xC, h) = 0;
    }
}

#endif
