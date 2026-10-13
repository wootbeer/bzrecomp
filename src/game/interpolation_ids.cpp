// Matrix IDs for RT64's frame interpolation (Refresh Rate above the game's
// own frame rate, and at Original too, since the game runs below 60), as in
// the BattleTanx ports (btgarecomp and btrecomp, interpolation_ids.cpp there).
//
// Without IDs RT64 pairs each matrix with one from the last frame by itself,
// comparing draw calls and positions. In this game every object's matrix is
// relative to the camera and comes from a pool rebuilt each frame, so there's
// nothing stable for it to go on, and it guesses wrong: a new bullet paired
// with an old one streaked out of the player's back, shadows paired with
// other shadows swelled and snapped back, and now and then some object paired
// with one across the map swept over the screen.
//
// Every object goes through func_8002DC40, called per render item by the
// depth-sorted bucket walk in func_8002FFFC. It builds the object's Mtx from
// the item's world position (item + 0x18) and orientation, points segment 9
// at it and calls the model's display list (which loads 0x09000000, plus
// 0x09000040 on for models with parts) in the main list; then the shadow (its
// own Mtx, loaded directly) and an optional effect through segment 15, in a
// second list at graphics state + 0x94 that's built backwards from its end.
// Hooks there write a gEXMatrixGroup with the render item's address as the ID
// (shadow and effect: the same ID + 1), with every component always
// interpolated, and set RT64's default group back afterwards so nothing else
// inherits it. A model's several matrices share its ID; RT64 pairs repeats
// in order (G_EX_ORDER_LINEAR).
//
// Render items are reused (a bullet's slot by the next bullet), and an ID
// seen last frame is always paired, so a reused slot would be interpolated
// from where its old owner was. Each item's world position is checked against
// where its last movement predicts it; one that breaks away (a new owner, a
// respawn) gets a new generation in its ID, which last frame doesn't have, so
// RT64 draws it where it is. World positions don't move when the camera does,
// so camera jolts never trip this.
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "recomp.h"

#include "bz_matrix_group.h"
#include "bz_recomp_hooks.h"

namespace {
    constexpr uint32_t kGfxStatePtr = 0x800A4768;
    constexpr uint32_t kPoolBaseField = 0x58;    // this frame's Mtx pool, alternates between two buffers

    // A break from the predicted position larger than this (world units) or
    // than half the last frame's movement starts a new generation. The first
    // movement after an item appears is accepted unless it's a teleport.
    constexpr float kBreakMin = 2.0f;
    constexpr float kTeleport = 50.0f;

    // Decomposed; position, rotation, scale, skew and perspective always
    // interpolated; vertices and texcoords skipped; tiles and look-at
    // automatic; repeats of an ID paired in order.
    constexpr uint32_t kObjectGroupParams =
        (1u << 2) | (1u << 3) | (1u << 5) | (1u << 7) | (1u << 9) | (1u << 11) | (2u << 15) | (0u << 17) | (2u << 24);

    struct Track {
        uint32_t frame;
        float pos[3];
        float vel[3];
        bool has_vel;
        uint32_t generation;
    };
    std::unordered_map<uint32_t, Track> tracks;

    uint32_t frame = 0;
    uint32_t last_pool_base = 0;
    bool main_enabled = false;   // gEXEnable written to the main list this frame
    uint32_t current_id = bz::kIdAuto;

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    float read_float(uint8_t* rdram, gpr address, int offset) {
        const uint32_t bits = (uint32_t)MEM_W(offset, address);
        float value;
        static_assert(sizeof(value) == sizeof(bits));
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    // A new frame starts whenever the Mtx pool switches buffers.
    void update_frame(uint8_t* rdram) {
        const gpr state = (gpr)MEM_W(0, kseg0(kGfxStatePtr));
        const uint32_t pool_base = (uint32_t)MEM_W(kPoolBaseField, state);
        if (pool_base != last_pool_base) {
            last_pool_base = pool_base;
            frame++;
            main_enabled = false;
            if ((frame & 0xFF) == 0) {
                for (auto it = tracks.begin(); it != tracks.end();) {
                    it = (frame - it->second.frame > 120) ? tracks.erase(it) : std::next(it);
                }
            }
        }
    }

    // `item` identifies the thing drawn; its world position is at `position`.
    uint32_t id_for_item(uint8_t* rdram, uint32_t item, uint32_t position) {
        const gpr p = kseg0(position);
        const float pos[3] = { read_float(rdram, p, 0x0), read_float(rdram, p, 0x4), read_float(rdram, p, 0x8) };
        auto [it, inserted] = tracks.try_emplace(item, Track{ frame, { pos[0], pos[1], pos[2] }, {}, false, 0 });
        Track& t = it->second;
        if (!inserted && t.frame != frame) {
            if (t.frame + 1 != frame) {
                // Not drawn last frame, so RT64 has nothing to pair it with.
                t.has_vel = false;
            }
            else {
                float err = 0.0f, speed = 0.0f;
                for (int i = 0; i < 3; i++) {
                    const float predicted = t.pos[i] + (t.has_vel ? t.vel[i] : 0.0f);
                    err += (pos[i] - predicted) * (pos[i] - predicted);
                    speed += t.vel[i] * t.vel[i];
                }
                err = std::sqrt(err);
                speed = std::sqrt(speed);
                const bool broke = t.has_vel ? (err > std::fmax(kBreakMin, 0.5f * speed)) : (err > kTeleport);
                if (broke) {
                    t.generation++;
                    t.has_vel = false;
                }
                else {
                    for (int i = 0; i < 3; i++) {
                        t.vel[i] = pos[i] - t.pos[i];
                    }
                    t.has_vel = true;
                }
            }
            for (int i = 0; i < 3; i++) {
                t.pos[i] = pos[i];
            }
            t.frame = frame;
        }
        // Item addresses are KSEG0 and word aligned: the low two bits tell a
        // model (0) from its shadow (1), the top byte holds the generation.
        return (item & 0x00FFFFFCu) | ((t.generation & 0x7Fu) << 24);
    }
}

// Before the object's gSPSegment(9, Mtx) in the main list.
extern "C" void bz_obj_model_begin(uint8_t* rdram, int32_t item, uint64_t* head) {
    update_frame(rdram);
    current_id = id_for_item(rdram, (uint32_t)item, (uint32_t)item + 0x18);
    uint32_t h = (uint32_t)*head;
    if (!main_enabled) {
        bz::write_gbi_enable(rdram, kseg0(h));
        h += bz::kEnableBytes;
        main_enabled = true;
    }
    bz::write_matrix_group(rdram, kseg0(h), current_id, kObjectGroupParams);
    h += bz::kGroupBytes;
    *head = (uint64_t)(int64_t)(int32_t)h;
}

// After the model's gSPDisplayList: back to RT64's own pairing.
extern "C" void bz_obj_model_end(uint8_t* rdram, uint64_t* head) {
    const uint32_t h = (uint32_t)*head;
    bz::write_matrix_group(rdram, kseg0(h), bz::kIdAuto, bz::kDefaultGroupParams);
    *head = (uint64_t)(int64_t)(int32_t)(h + bz::kGroupBytes);
}

// The shadow list grows downwards and runs upwards, so what's written first
// runs last: the reset to RT64's own pairing goes in first...
extern "C" void bz_obj_shadow_top(uint8_t* rdram, uint64_t* head) {
    const uint32_t h = (uint32_t)*head - bz::kGroupBytes;
    bz::write_matrix_group(rdram, kseg0(h), bz::kIdAuto, bz::kDefaultGroupParams);
    *head = (uint64_t)(int64_t)(int32_t)h;
}

// ...and the object's group last, so it runs before the shadow's matrix.
extern "C" void bz_obj_shadow_bottom(uint8_t* rdram, uint64_t* head) {
    const uint32_t h = (uint32_t)*head - bz::kEnableBytes - bz::kGroupBytes;
    bz::write_gbi_enable(rdram, kseg0(h));
    bz::write_matrix_group(rdram, kseg0(h + bz::kEnableBytes), current_id | 1u, kObjectGroupParams);
    *head = (uint64_t)(int64_t)(int32_t)h;
}

// Particles (ovl1 func_80166810: smoke and debris): one billboard Mtx each,
// written to the main list through $s0 = head - 4, with the head itself in
// $s2. The particle's position (x, y, z) is at $s6; that address is its ID.
extern "C" void bz_particle_begin(uint8_t* rdram, int32_t particle, uint64_t* cursor, uint64_t* head) {
    update_frame(rdram);
    const uint32_t id = id_for_item(rdram, (uint32_t)particle, (uint32_t)particle);
    uint32_t h = (uint32_t)*head;
    uint32_t added = bz::kGroupBytes;
    if (!main_enabled) {
        bz::write_gbi_enable(rdram, kseg0(h));
        h += bz::kEnableBytes;
        added += bz::kEnableBytes;
        main_enabled = true;
    }
    bz::write_matrix_group(rdram, kseg0(h), id, kObjectGroupParams);
    *head = (uint64_t)(int64_t)(int32_t)((uint32_t)*head + added);
    *cursor = (uint64_t)(int64_t)(int32_t)((uint32_t)*cursor + added);
}

// After the system's last particle, before the head is stored.
extern "C" void bz_particle_end(uint8_t* rdram, uint64_t* head) {
    bz_obj_model_end(rdram, head);
}

// Ground shadows (ovl1 func_80129CE0, object in $s1): a quad fitted to the
// terrain under the object, with its own Mtx, added to the backward-built
// list at graphics state + 0x94 through $v0. Same ID as the object's model,
// low bit set. The reset goes in first (it runs after the quad)...
extern "C" void bz_shadow_quad_top(uint8_t* rdram, uint64_t* head) {
    bz_obj_shadow_top(rdram, head);
}

// The object, before $s1 is reused for the quad's Mtx.
static uint32_t shadow_object = 0;
extern "C" void bz_shadow_quad_object(int32_t object) {
    shadow_object = (uint32_t)object;
}

// ...the object's group last, below the quad's gSPMatrix.
extern "C" void bz_shadow_quad_bottom(uint8_t* rdram, uint64_t* head) {
    update_frame(rdram);
    const uint32_t id = id_for_item(rdram, shadow_object, shadow_object + 0x18) | 1u;
    const uint32_t h = (uint32_t)*head - bz::kEnableBytes - bz::kGroupBytes;
    bz::write_gbi_enable(rdram, kseg0(h));
    bz::write_matrix_group(rdram, kseg0(h + bz::kEnableBytes), id, kObjectGroupParams);
    *head = (uint64_t)(int64_t)(int32_t)h;
}
