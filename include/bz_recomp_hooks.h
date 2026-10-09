#ifndef BZ_RECOMP_HOOKS_H
#define BZ_RECOMP_HOOKS_H

// Force-included into every file in RecompiledFuncs/ (see CMakeLists.txt),
// for whatever the [[patches.hook]] text in battlezone.us.rev0.toml needs
// beyond what N64Recomp's own generated recomp.h already provides (ctx,
// S32/S64/U32/U64, lo/hi). Add declarations here as new hooks need them.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// src/main/scheduler_workaround.cpp: lets a busy-wait polling loop in the
// original code hand the CPU to the thread it's waiting on. Call it from a
// [[patches.hook]] placed inside such a loop.
void bz_yield_via_priority_drop(uint8_t* rdram);

// src/game/modules.cpp: called on entry to the game's ROM-read helper
// (func_8002A120: dst, PI address, size) so a module load registers that
// module's recompiled functions.
void bz_on_rom_read(uint8_t* rdram, int32_t dst, uint32_t pi_addr, uint32_t size);

#ifdef __cplusplus
}
#endif

#endif // BZ_RECOMP_HOOKS_H
