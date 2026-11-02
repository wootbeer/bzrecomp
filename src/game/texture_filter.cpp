// Texture Filtering and HUD Filtering: Sharp (Graphics tab, after the
// Sin and Punishment port). Each graphics task's display lists are walked
// just before the game hands the task to the RSP (the osSpTaskLoad calls in
// func_8002A818 and func_8002A934). A render mode command that sets the
// texture filter (othermode bits 12 - 13) belongs to the HUD if the first
// thing drawn after it is a rectangle (text, icons, panels), else to the
// world; when that one's option is Sharp it's set to point sampling. What it
// was is kept by address, so lists the game keeps get it back when the
// option is turned off (lists it rebuilds each frame are simply rewritten).
//
// The lists also hold RT64's extended commands written by the widescreen
// code (16 and 24 bytes long); they're stepped over whole, so no word inside
// one is taken for a command.
#include <atomic>
#include <cstdint>
#include <unordered_map>

#include "recomp.h"

#include "bz_recomp_hooks.h"

namespace bz {
    void set_texture_filters(bool sharp_world, bool sharp_hud);
}

namespace {
    constexpr uint32_t kTextureFilterBits = 0x3000;
    constexpr int kMaxDepth = 12;
    constexpr uint32_t kMaxCommands = 1 << 20;

    std::atomic<bool> sharp_world_wanted{ false };
    std::atomic<bool> sharp_hud_wanted{ false };
    bool sharp_world = false;
    bool sharp_hud = false;

    struct Kept {
        uint32_t w0, w1;              // the game's
        uint32_t sharp_w0, sharp_w1;  // as set here
    };
    std::unordered_map<uint32_t, Kept> kept;

    uint32_t segments[16];
    uint32_t pending = 0;  // the latest filter command, until something is drawn
    uint32_t commands = 0;

    gpr kseg0(uint32_t physical) {
        return (gpr)(int32_t)(0x80000000u | (physical & 0x1FFFFFFF));
    }

    // A segmented address (or a KSEG0 one) to KSEG0.
    uint32_t resolve(uint32_t addr) {
        if ((addr & 0xE0000000u) == 0x80000000u) {
            return addr;
        }
        return (uint32_t)kseg0(segments[(addr >> 24) & 0xF] + (addr & 0x00FFFFFF));
    }

    bool sets_texture_filter(uint32_t w0) {
        if ((w0 >> 24) == 0xEF) {  // G_RDPSETOTHERMODE: othermode_H in w0
            return true;
        }
        if ((w0 >> 24) != 0xE3) {
            return false;
        }
        // G_SETOTHERMODE_H (F3DEX2): 32 - shift - length, and length - 1.
        uint32_t length = (w0 & 0xFF) + 1;
        uint32_t shift = 32 - ((w0 >> 8) & 0xFF) - length;
        uint32_t mask = (length >= 32 ? 0xFFFFFFFFu : ((1u << length) - 1)) << shift;
        return (mask & kTextureFilterBits) != 0;
    }

    void set_command(uint8_t* rdram, uint32_t addr, uint32_t w0, uint32_t w1) {
        MEM_W(0, (gpr)(int32_t)addr) = (int32_t)w0;
        MEM_W(4, (gpr)(int32_t)addr) = (int32_t)w1;
    }

    void set_filter(uint8_t* rdram, uint32_t addr, bool sharp) {
        const uint32_t w0 = (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
        const uint32_t w1 = (uint32_t)MEM_W(4, (gpr)(int32_t)addr);
        auto it = kept.find(addr);
        if (it != kept.end()) {
            if (it->second.sharp_w0 != w0 || it->second.sharp_w1 != w1) {
                kept.erase(it);  // the game wrote something else here
            }
            else {
                if (!sharp) {
                    set_command(rdram, addr, it->second.w0, it->second.w1);
                    kept.erase(it);
                }
                return;
            }
        }
        if (!sharp) {
            return;
        }
        uint32_t sharp_w0 = w0, sharp_w1 = w1;
        if ((w0 >> 24) == 0xEF) {
            sharp_w0 = w0 & ~kTextureFilterBits;
        }
        else {
            sharp_w1 = w1 & ~kTextureFilterBits;
        }
        if (sharp_w0 != w0 || sharp_w1 != w1) {
            kept[addr] = { w0, w1, sharp_w0, sharp_w1 };
            set_command(rdram, addr, sharp_w0, sharp_w1);
        }
    }

    // How many 8-byte words an RT64 extended command takes.
    uint32_t extended_length(uint32_t w0) {
        switch (w0 & 0xFFFFFF) {
        case 0x02: case 0x08:  // texture rectangle, scissor align
            return 3;
        case 0x03: case 0x04: case 0x05: case 0x06: case 0x07: case 0x0C:
        case 0x13: case 0x14: case 0x30: case 0x31:
            return 2;
        default:
            return 1;
        }
    }

    void walk(uint8_t* rdram, uint32_t addr, int depth) {
        while (commands++ < kMaxCommands) {
            const uint32_t w0 = (uint32_t)MEM_W(0, (gpr)(int32_t)addr);
            const uint32_t w1 = (uint32_t)MEM_W(4, (gpr)(int32_t)addr);
            const uint32_t op = w0 >> 24;
            bool rect = op == 0xE4 || op == 0xE5;
            uint32_t length = 1;
            if (op == 0x64) {
                length = extended_length(w0);
                rect = (w0 & 0xFFFFFF) == 0x02 || (w0 & 0xFFFFFF) == 0x03;
            }
            if (sets_texture_filter(w0)) {
                pending = addr;
            }
            else if (pending != 0 && (rect || op == 0x05 || op == 0x06 || op == 0x07)) {
                set_filter(rdram, pending, rect ? sharp_hud : sharp_world);
                pending = 0;
            }
            switch (op) {
            case 0xDB:  // G_MOVEWORD
                if (((w0 >> 16) & 0xFF) == 0x06) {  // G_MW_SEGMENT
                    segments[((w0 & 0xFFFF) >> 2) & 0xF] = w1 & 0x1FFFFFFF;
                }
                break;
            case 0xDE: {  // G_DL
                const bool branch = ((w0 >> 16) & 0xFF) != 0;
                if (depth < kMaxDepth) {
                    walk(rdram, resolve(w1), depth + 1);
                }
                if (branch) {
                    return;
                }
                break;
            }
            case 0xDF:  // G_ENDDL
                return;
            default:
                break;
            }
            addr += length * 8;
        }
    }
}

void bz::set_texture_filters(bool world, bool hud) {
    sharp_world_wanted.store(world, std::memory_order_relaxed);
    sharp_hud_wanted.store(hud, std::memory_order_relaxed);
}

// Before an osSpTaskLoad: the task (OSTask: type at 0, data_ptr at 0x30).
extern "C" void bz_texture_filter_task(uint8_t* rdram, int32_t task) {
    const bool world = sharp_world_wanted.load(std::memory_order_relaxed);
    const bool hud = sharp_hud_wanted.load(std::memory_order_relaxed);
    // Nothing to do while both are off and nothing was changed.
    if (!world && !hud && kept.empty()) {
        return;
    }
    if ((uint32_t)MEM_W(0, (gpr)task) != 1) {  // M_GFXTASK
        return;
    }
    sharp_world = world;
    sharp_hud = hud;
    for (uint32_t& segment : segments) {
        segment = 0;
    }
    pending = 0;
    commands = 0;
    const uint32_t data = (uint32_t)MEM_W(0x30, (gpr)task);
    if (data != 0) {
        walk(rdram, (uint32_t)kseg0(data), 0);
    }
}
