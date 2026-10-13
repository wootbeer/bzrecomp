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
// With Draw Distance at Extended, something past the game's distance is drawn
// as long as it stands on that grid (with a margin), up to twice the distance,
// so nothing is drawn floating past the ground's edge; the far plane and the
// depth buckets get twice the distance so those objects and the ground under
// them show. The grid is the one the builder placed last (a frame behind for
// the reads before it). The stored distance is never changed, so the option
// can change at any time.
//
// These culls also measure the view's width with the 4:3 projection: the
// object cull's half-width slope (camera + 0x50) and the effect culls'
// half-width (camera + 0x1D8). In Expand both are widened by the widescreen
// factor, so nothing vanishes in the widened sides of the view.
#include <algorithm>
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
    constexpr float kMaxFactor = 2.0f;       // and within twice the game's own distance

    struct GroundBox {
        float min_x, max_x, min_z, max_z;    // world units, relative to the camera
    };
    std::unordered_map<uint32_t, GroundBox> ground_boxes;  // by camera

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
        return on_ground ? far * kMaxFactor : far;
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
    ground_boxes[camera] = { x0, x0 + float(std::max(columns - 1, 0)) * kCellWorld, z0, z0 + float(std::max(rows - 1, 0)) * kCellWorld };
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

// A read of a cull's view half-width (slope or screen units), just loaded.
extern "C" void bz_cull_half_width(float* half_width) {
    *half_width *= bz::get_widescreen_scale();
}
