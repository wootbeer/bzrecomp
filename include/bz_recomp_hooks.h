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

// src/game/terrain_precision.cpp: undo the ground grid's per-frame truncation
// of the camera position (ovl1 func_8012F864, func_8012EAA8, func_8012F2C0).
void bz_terrain_begin(uint8_t* rdram, int32_t gfx_state, float x, float y, float z);
void bz_terrain_begin_reg(uint8_t* rdram, uint64_t* head_reg, float x, float y, float z);
void bz_terrain_end(uint8_t* rdram, int32_t gfx_state);
void bz_terrain_end_reg(uint8_t* rdram, uint64_t* head_reg);

// src/game/widescreen_hud.cpp: anchor HUD pieces to the screen edges in
// Expand (see that file for where each hook sits).
void bz_hud_radar_begin(uint8_t* rdram);
void bz_hud_piece_end(uint8_t* rdram);
void bz_hud_element_begin(uint8_t* rdram, int32_t element);
void bz_hud_text_begin(uint8_t* rdram);
void bz_hud_text_end(uint8_t* rdram);
void bz_hud_text_line(uint8_t* rdram, int32_t entry, uint64_t* head_reg);
void bz_hud_text_done(uint8_t* rdram, uint64_t* head_reg);

#ifdef __cplusplus
}
#endif

#endif // BZ_RECOMP_HOOKS_H
