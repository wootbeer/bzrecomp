// Controller Pak (N64 memory card) filesystem internals N64Recomp's
// built-in `ignored_funcs` list expects some runtime to provide under a
// `_recomp` suffix, but stock N64ModernRuntime doesn't implement -- see
// stock_runtime_compat.cpp's file-level comment for the full context.
//
// These are libultra's low-level pak internals. The game reaches them only
// through the osPfs*/osMotor* entry points, which are replaced at a higher
// level -- src/game/controller_pak_hle.cpp emulates the Controller Pak file
// API and the runtime handles the Rumble Pak -- so nothing below should run.
// Each just fails the way a missing pak would.

#include "ultramodern/ultra64.h"
#include "recomp.h"

// __osContAddressCrc computes a 5-bit CRC over an 11-bit Controller Pak
// address for SI bus framing -- pure arithmetic, no I/O, so it's safe to
// leave in regardless of whether a pak is connected. NOT independently
// verified against a primary libultra source (this is from general
// familiarity with the publicly-documented N64 controller pak address CRC,
// not a checked reference) -- low risk either way since nothing here
// reports a real pak connected, but revisit this specific function first
// if real Controller Pak I/O is ever added.
extern "C" void __osContAddressCrc_recomp(uint8_t*, recomp_context* ctx) {
    uint16_t address = (uint16_t)ctx->r4;
    uint16_t crc = 0;
    uint16_t shifted = (uint16_t)(address << 1);

    for (int i = 0; i < 16; i++, shifted <<= 1) {
        if (shifted & 0x8000) {
            crc ^= 0x0015;
        }
        if (crc & 1) {
            crc = (uint16_t)(((crc ^ 0x0500) >> 1) | 0x8000);
        } else {
            crc = (uint16_t)(crc >> 1);
        }
    }

    ctx->r2 = (crc >> 8) & 0x1F;
}

// Everything below performs actual SI-bus I/O with a Controller Pak on
// real hardware. With no pak ever reported connected, these should never
// be reached by well-behaved game code that checks pak presence first --
// but return a consistent non-zero failure code rather than 0 (success) in
// case something calls in anyway, so it's treated as "the operation
// failed" rather than silently believed to have succeeded against pak
// contents that don't exist. -1 is used as a generic MIPS/libultra failure
// idiom here rather than one of the real named PFS_ERR_*/CONT_ERR_*
// constants, since those exact numeric values weren't independently
// verified against a primary libultra source.

extern "C" void __osPfsSelectBank_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (uint32_t)-1;
}

extern "C" void __osContRamWrite_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (uint32_t)-1;
}

extern "C" void __osContRamRead_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (uint32_t)-1;
}

extern "C" void __osCheckPackId_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (uint32_t)-1;
}

extern "C" void __osPfsRWInode_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (uint32_t)-1;
}

extern "C" void __osRepairPackId_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (uint32_t)-1;
}
