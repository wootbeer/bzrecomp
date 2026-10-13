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
void bz_terrain_begin_anchored(uint8_t* rdram, uint64_t* head_reg, float x, float y, float z,
    int32_t cell_x, int32_t cell_z, int32_t sp, uint64_t* y_int, uint64_t* z_int);
void bz_terrain_end(uint8_t* rdram, int32_t gfx_state);
void bz_terrain_end_reg(uint8_t* rdram, uint64_t* head_reg);

// src/game/interpolation_ids.cpp: matrix IDs for RT64's frame interpolation
// on each object func_8002DC40 draws (its model, and its shadow in the
// backward-built list at graphics state + 0x94).
void bz_obj_model_begin(uint8_t* rdram, int32_t item, uint64_t* head);
void bz_obj_model_end(uint8_t* rdram, uint64_t* head);
void bz_obj_shadow_top(uint8_t* rdram, uint64_t* head);
void bz_obj_shadow_bottom(uint8_t* rdram, uint64_t* head);
// ...and on each particle ovl1 func_80166810 draws.
void bz_particle_begin(uint8_t* rdram, int32_t particle, uint64_t* cursor, uint64_t* head);
void bz_particle_end(uint8_t* rdram, uint64_t* head);
// ...and on the ground shadow ovl1 func_80129CE0 draws under an object.
void bz_shadow_quad_top(uint8_t* rdram, uint64_t* head);
void bz_shadow_quad_object(int32_t object);
// ...and on the effects ovl1 func_80164B80 draws.
void bz_effect_begin(uint8_t* rdram, int32_t effect, uint64_t* head);
void bz_effect_end(uint8_t* rdram, uint64_t* head);
// ...and on the beams ovl1 func_801A3F60 draws.
void bz_beam_begin(uint8_t* rdram, int32_t beam, uint64_t* head);
void bz_beam_end(uint8_t* rdram, uint64_t* head);
void bz_shadow_quad_bottom(uint8_t* rdram, uint64_t* head);

// src/game/draw_distance.cpp: the far distance and view width used to cull.
void bz_ground_extent(uint8_t* rdram, int32_t columns, int32_t rows, float start_x, float start_z);
void bz_far_distance(float* far);
void bz_object_far(int32_t camera, float dx, float dz, float radius, float* far);
void bz_effect_check_point(int32_t point, uint32_t radius_bits);
void bz_effect_far(uint8_t* rdram, int32_t camera, float* far);
void bz_cull_half_width(float* half_width);

// src/game/widescreen_sprites.cpp: let effect sprites reach the widened
// view (ovl1 func_8016765C = drawer 0, func_80166BD0 = drawer 1).
void bz_sprites_begin(uint8_t* rdram, int drawer, uint64_t* head, uint64_t* tail);
void bz_sprites_end(uint8_t* rdram, int drawer, uint64_t* head);
void bz_sprites_cull_width(float* half_width);
void bz_sprites_rect(int drawer, int32_t cx, int32_t half_w, int32_t cy, int32_t half_h);
void bz_sprites_written(uint8_t* rdram, int drawer, int32_t end);

// src/game/widescreen_hud.cpp: anchor HUD pieces to the screen edges in
// Expand (see that file for where each hook sits).
void bz_hud_radar_begin(uint8_t* rdram);
void bz_hud_piece_end(uint8_t* rdram);
void bz_hud_element_begin(uint8_t* rdram, int32_t element);
void bz_hud_text_queued(uint8_t* rdram);
void bz_hud_text_begin(uint8_t* rdram);
void bz_hud_text_end(uint8_t* rdram);
void bz_hud_text_line(uint8_t* rdram, int32_t entry, uint64_t* head_reg);
void bz_hud_text_done(uint8_t* rdram, uint64_t* head_reg);

#ifdef __cplusplus
}
#endif

#endif // BZ_RECOMP_HOOKS_H
