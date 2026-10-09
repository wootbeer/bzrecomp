// The game's debug output, printed to stdout.
//
// func_8003C700 (named bz_debug_write in the symbol file) is how the game
// writes debug text: (const char* buf, u32 len) -> len. On hardware it goes
// through the developers' own dev-cartridge library (0x8003C700-0x8003EB3C),
// which pokes PI addresses that don't exist here, so it is replaced outright
// (`ignored` in battlezone.us.rev0.toml) and the text is printed instead.
#include <cstdio>
#include <string>

#include "recomp.h"

extern "C" void bz_debug_write(uint8_t* rdram, recomp_context* ctx) {
    gpr buf = ctx->r4;
    uint32_t len = (uint32_t)ctx->r5;
    std::string text;
    text.reserve(len);
    for (uint32_t i = 0; i < len && i < 0x10000; i++) {
        text.push_back((char)MEM_B(i, buf));
    }
    fputs(text.c_str(), stdout);
    fflush(stdout);
    ctx->r2 = (gpr)(int32_t)len;
}
