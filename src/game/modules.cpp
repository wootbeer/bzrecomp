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
//
// The in-game module (1) in turn loads one mission's code blob at a time into
// a second slot, 0x801FD150, from the ROM-address table at 0x801F9C90 in its
// data (tools/bzlayout.py). Each blob is its own overlay section; its extent
// runs to the next blob, so the table (in RDRAM by then) gives the size.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "librecomp/overlays.hpp"
#include "recomp.h"

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

    constexpr int32_t kBlobSlot = (int32_t)0x801FD150;
    constexpr uint32_t kBlobSlotSize = 0x5400;
    constexpr int32_t kBlobTable = (int32_t)0x801F9C90;
    constexpr int kBlobTableEntries = 94;
    constexpr uint32_t kLastBlobEnd = 0x22DDE8;
    uint32_t loaded_blob = 0;

    // A blob load: register just that blob (a 0x5400 read also covers the
    // start of the next one).
    void on_blob_read(uint8_t* rdram, uint32_t rom) {
        std::vector<uint32_t> starts;
        for (int i = 0; i < kBlobTableEntries; i++) {
            starts.push_back((uint32_t)MEM_W(4 * i, (gpr)kBlobTable) & 0x0FFFFFFF);
        }
        std::sort(starts.begin(), starts.end());
        auto it = std::find(starts.begin(), starts.end(), rom);
        if (it == starts.end()) {
            fprintf(stderr, "[bz] read into the mission slot from unknown ROM 0x%06X\n", rom);
            return;
        }
        auto next = std::upper_bound(starts.begin(), starts.end(), rom);
        uint32_t end = next != starts.end() ? *next : kLastBlobEnd;
        if (loaded_blob != rom) {
            unload_overlays(kBlobSlot, kBlobSlotSize);
            load_overlays(rom, kBlobSlot, end - rom);
            loaded_blob = rom;
            fprintf(stderr, "[bz] mission blob 0x%06X loaded\n", rom);
        }
    }
}

extern "C" void bz_on_rom_read(uint8_t* rdram, int32_t dst, uint32_t pi_addr, uint32_t) {
    uint32_t rom = pi_addr & 0x0FFFFFFF;
    if (dst == kBlobSlot && loaded_module == 1) {
        on_blob_read(rdram, rom);
        return;
    }
    for (int i = 0; i < (int)(sizeof(kModules) / sizeof(kModules[0])); i++) {
        const Module& m = kModules[i];
        if (rom != m.rom_start || dst != kModuleSlot) {
            continue;
        }
        if (loaded_module != i) {
            unload_overlays(kModuleSlot, kSlotSize);
            if (loaded_blob != 0) {
                unload_overlays(kBlobSlot, kBlobSlotSize);  // it belonged to module 1
                loaded_blob = 0;
            }
            load_overlays(m.rom_start, kModuleSlot, m.rom_end - m.rom_start);
            loaded_module = i;
            fprintf(stderr, "[bz] module %d loaded\n", i);
        }
        return;
    }
}
