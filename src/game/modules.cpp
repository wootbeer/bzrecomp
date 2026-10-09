// Battlezone's code modules: three overlays that the main loop
// (func_800295D0) loads one at a time into the same slot at 0x8010FFE0,
// reading {vram, bss_end, rom_start, rom_end, rom_end_code_only} from its
// segment table at 0x80057A30 (see tools/bzlayout.py).
//
// The runtime only knows which module's recompiled functions to run if it is
// told when one is loaded, so the game's ROM-read helper (func_8002A120) calls
// bz_on_rom_read on entry (a [[patches.hook]] in battlezone.us.rev0.toml).
// A read starting at a module's ROM address is that module being loaded, in
// full or (negative module index) its code-only part.
#include <cstdint>
#include <cstdio>

#include "librecomp/overlays.hpp"

namespace {
    struct Module {
        uint32_t rom_start;
        uint32_t rom_end;
    };

    constexpr int32_t kModuleSlot = (int32_t)0x8010FFE0;
    constexpr Module kModules[] = {
        { 0x045540, 0x0DFDB0 },
        { 0x0DFDB0, 0x1CCF20 },
        { 0x1CCF20, 0x1D0440 },
    };
    // The biggest module's text+data, for clearing the slot.
    constexpr uint32_t kSlotSize = 0x1CCF20 - 0x0DFDB0;

    int loaded_module = -1;
}

extern "C" void bz_on_rom_read(uint8_t*, int32_t dst, uint32_t pi_addr, uint32_t) {
    uint32_t rom = pi_addr & 0x0FFFFFFF;
    for (int i = 0; i < (int)(sizeof(kModules) / sizeof(kModules[0])); i++) {
        const Module& m = kModules[i];
        if (rom != m.rom_start || dst != kModuleSlot) {
            continue;
        }
        if (loaded_module != i) {
            unload_overlays(kModuleSlot, kSlotSize);
            load_overlays(m.rom_start, kModuleSlot, m.rom_end - m.rom_start);
            loaded_module = i;
            fprintf(stderr, "[bz] module %d loaded\n", i);
        }
        return;
    }
}
