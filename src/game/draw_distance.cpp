// Draw distance, and culling for the widescreen view.
//
// A camera's far distance (camera + 0x30, 200 world units in game) is both
// its projection's far plane (func_8002B8FC) and how far objects are drawn:
// func_80030824 culls an object whose depth beyond its radius passes it and
// files the rest in one depth bucket per world unit (func_80030624 makes
// trunc(far) of them), and func_80030410 / func_800304F4 drop effects, beams
// and particles past it. The ground, though, is a grid of a fixed size
// (ovl1 func_8012F864, about 38 x 34 cells of 10 units) placed around the
// view, off-centre: it reaches about 300 units straight ahead but less than
// 200 towards the sides of a widescreen view, and the far plane cut it off at
// 200.
//
// With Draw Distance at Extended, the ground's reach across the (4:3) view is
// worked out each frame from the grid the builder has just placed (as a
// factor of the game's distance, 1 to 1.5, smoothed). Something past the
// game's distance is drawn as long as it stands on that grid (with a margin)
// and within that reach, so nothing is drawn floating past the ground's edge;
// the far plane and the depth buckets get 1.5 times the distance so those
// objects and the ground under them show.
//
// The fog (func_8002B12C) runs from 128 units to the far distance, mapped
// to depth with camera + 0x1E4 / 0x1E8, which func_8002B8FC works out from
// the far distance too. That read is extended like the others so the fog
// lines up with the far plane, and the fog's start and end move out to the
// ground's reach, so the ground still fades out before its edge instead of
// ending in plain view. The game sets the fog every frame, so it follows. The farther
// plane squeezes that range into less depth, so the fog multiplier (16-bit in
// the command) would pass 32767 and wrap round to full fog: it's held at
// 32767 instead, which starts the fog a unit or two sooner. The grid is the one the builder placed last (a frame behind for
// the reads before it). The stored distance is never changed, so the option
// can change at any time.
//
// These culls also measure the view's width with the 4:3 projection: the
// object cull's half-width slope (camera + 0x50) and the effect culls'
// half-width (camera + 0x1D8). In Expand both are widened by the widescreen
// factor, so nothing vanishes in the widened sides of the view.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <unordered_map>

#include "recomp.h"

#include "bz_config.h"
#include "bz_recomp_hooks.h"
#include "bz_widescreen.h"

namespace {
    constexpr uint32_t kCurrentCameraPtr = 0x800A47A8;
    constexpr float kCellWorld = 10.0f;      // a ground cell (100 vertex units)
    constexpr float kEdgeMargin = 10.0f;     // stay this far inside the ground's edge
    constexpr float kMaxFactor = 1.5f;       // and within 1.5 times the game's own distance

    struct GroundBox {
        float min_x, max_x, min_z, max_z;    // world units, relative to the camera
    };
    std::unordered_map<uint32_t, GroundBox> ground_boxes;  // by camera

    // How far past the game's own distance the ground reaches across the
    // view, as a factor of it (1 to kMaxFactor), smoothed so it doesn't jump
    // when the grid moves a cell.
    std::unordered_map<uint32_t, float> reach_factors;     // by camera
    constexpr float kReachSmoothing = 0.1f;

    gpr kseg0(uint32_t address) {
        return (gpr)(int32_t)address;
    }

    float read_float(uint8_t* rdram, gpr address, int offset) {
        const uint32_t bits = (uint32_t)MEM_W(offset, address);
        float value;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    bool extended() {
        return bz::config::get_draw_distance() == bz::config::DrawDistance::Extended;
    }

    float reach_factor(uint32_t camera) {
        auto it = reach_factors.find(camera);
        return it != reach_factors.end() ? it->second : 1.0f;
    }

    // Depth along a ray of the view (per unit of depth: dx, dz) at which it
    // leaves the box.
    float exit_depth(const GroundBox& box, float dx, float dz) {
        float t = 1e30f;
        if (dx > 1e-6f) t = std::min(t, box.max_x / dx);
        if (dx < -1e-6f) t = std::min(t, box.min_x / dx);
        if (dz > 1e-6f) t = std::min(t, box.max_z / dz);
        if (dz < -1e-6f) t = std::min(t, box.min_z / dz);
        return t;
    }

    // Where the ground ends across the game's own (4:3) view: the culls'
    // depth is x * c34 + z * c38 and their side distance x * c38 - z * c34,
    // visible while under c50 * depth, which gives the view's rays per unit
    // of depth. The nearest edge along them is where the fog has to be thick.
    float ground_reach(uint8_t* rdram, uint32_t camera, const GroundBox& box) {
        const gpr cam = kseg0(camera);
        const float c34 = read_float(rdram, cam, 0x34);
        const float c38 = read_float(rdram, cam, 0x38);
        const float k2 = c34 * c34 + c38 * c38;
        if (k2 < 1e-6f) {
            return 0.0f;
        }
        const float half_width = read_float(rdram, cam, 0x50);
        float reach = 1e30f;
        for (float side : { -1.0f, -0.5f, 0.0f, 0.5f, 1.0f }) {
            const float l = side * half_width;
            reach = std::min(reach, exit_depth(box, (c34 + c38 * l) / k2, (c38 - c34 * l) / k2));
        }
        return reach;
    }

    // The far distance to use for something at (dx, dz) from the camera
    // (world units) with the given radius.
    float far_for(uint32_t camera, float far, float dx, float dz, float radius) {
        if (!extended()) {
            return far;
        }
        auto it = ground_boxes.find(camera);
        if (it == ground_boxes.end()) {
            return far;
        }
        const GroundBox& box = it->second;
        const float m = kEdgeMargin + std::max(radius, 0.0f);
        const bool on_ground = (dx >= box.min_x + m) && (dx <= box.max_x - m) && (dz >= box.min_z + m) && (dz <= box.max_z - m);
        return on_ground ? far * reach_factor(camera) : far;
    }

    // The point the effect checks were called for (their $a1, a world
    // position), and the radius of the second one ($a2).
    uint32_t check_point = 0;
    float check_radius = 0.0f;
}

// Where the ground grid starts (vertex units, relative to the camera) and its
// size in vertices, as the main builder sets it up.
extern "C" void bz_ground_extent(uint8_t* rdram, int32_t columns, int32_t rows, float start_x, float start_z) {
    const uint32_t camera = (uint32_t)MEM_W(0, kseg0(kCurrentCameraPtr));
    const float x0 = start_x / 10.0f;
    const float z0 = start_z / 10.0f;
    const GroundBox box = { x0, x0 + float(std::max(columns - 1, 0)) * kCellWorld, z0, z0 + float(std::max(rows - 1, 0)) * kCellWorld };
    ground_boxes[camera] = box;

    const float far = read_float(rdram, kseg0(camera), 0x30);
    if (far > 0.0f) {
        const float target = std::clamp((ground_reach(rdram, camera, box) - kEdgeMargin) / far, 1.0f, kMaxFactor);
        auto [it, inserted] = reach_factors.try_emplace(camera, target);
        if (!inserted) {
            it->second += (target - it->second) * kReachSmoothing;
        }
    }
}

// The far distance as the projection's far plane and for the depth buckets.
extern "C" void bz_far_distance(float* far) {
    if (extended()) {
        *far *= kMaxFactor;
    }
}

// The object cull: the object at (dx, dz) from the camera, with its radius.
extern "C" void bz_object_far(int32_t camera, float dx, float dz, float radius, float* far) {
    *far = far_for((uint32_t)camera, *far, dx, dz, radius);
}

// The effect checks: on entry, the point (and radius, as float bits) they're
// given...
extern "C" void bz_effect_check_point(int32_t point, uint32_t radius_bits) {
    check_point = (uint32_t)point;
    std::memcpy(&check_radius, &radius_bits, sizeof(check_radius));
}

// ...and their read of the far distance.
extern "C" void bz_effect_far(uint8_t* rdram, int32_t camera, float* far) {
    if (check_point == 0) {
        return;
    }
    const gpr cam = kseg0((uint32_t)camera);
    const gpr p = kseg0(check_point);
    const float dx = read_float(rdram, p, 0x0) - read_float(rdram, cam, 0x0);
    const float dz = read_float(rdram, p, 0x8) - read_float(rdram, cam, 0x8);
    *far = far_for((uint32_t)camera, *far, dx, dz, check_radius);
}

// The fog's start and end (world units) for the camera, before they're mapped
// to depth. Only the game's own fade to its far distance moves: other scenes
// (the menus' 3D) set fog of their own.
extern "C" void bz_fog_range(uint8_t* rdram, int32_t camera, float* start, float* end) {
    if (!extended()) {
        return;
    }
    const float far = read_float(rdram, kseg0((uint32_t)camera), 0x30);
    if ((*end > 0.0f) && (std::fabs(*end - far) < 1.0f)) {
        const float factor = reach_factor((uint32_t)camera);
        *start *= factor;
        *end *= factor;
    }
}

// The fog multiplier, just before it's truncated to 16 bits.
extern "C" void bz_fog_multiplier(float* multiplier) {
    *multiplier = std::clamp(*multiplier, -32768.0f, 32767.0f);
}

// A read of a cull's view half-width (slope or screen units), just loaded.
extern "C" void bz_cull_half_width(float* half_width) {
    *half_width *= bz::get_widescreen_scale();
}
