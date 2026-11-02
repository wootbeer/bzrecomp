// What the Cheats and Hacks tabs do in the game (include/bz_cheats.h).
//
// Stock codes are the game's own button codes. The menus check them against
// a table at 0x8012D000 (in the menu module): a flag address and four button
// combinations each, all with Z held. Each flag is a byte, read where the
// cheat acts:
//   0x8006A1C0  build costs are 0 for the player (ovl1 func_8017BF18/80)
//   0x8006A1C1  the player takes no damage (func_8016F94C)
//   0x8006A1C2  the player's ammo isn't spent (func_8016F9C4/FA08)
//   0x8006A1C3  satellite view without a satellite (func_80160AA8)
//   0x8006A1C4  every unit is visible to the player (func_80174778)
//   0x8006A1C5  game speed 1.5x, 0x8006A1C6 2x (the frame timer's scale,
//               set in func_80123970)
//   0x8006A1F5  every mission can be chosen (the menus' mission select)
// 0x8006A1C7 (Z + R + C-Right) has a code but nothing reads it.
// While a toggle is on its flag is kept set; turning it off clears it once,
// so entering the code in the game still works with the toggle off.
//
// GameShark codes write the game's memory, as the cheat device did: 80 a
// byte, 81 a halfword (as two bytes). They're applied on each pass of the
// controller thread (func_80029C48, after osContGetReadData), which runs in
// the menus and in play. Most addresses are in the in-game module's memory,
// which the menu module also loads into, so those are only written while the
// in-game module is loaded.
#include <array>
#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

#include "bz_cheats.h"
#include "recomp.h"

extern "C" int bz_loaded_module();

namespace {
    using bz::cheats::Cheat;
    using bz::cheats::Section;

    constexpr int kInGameModule = 1;
    // The module slot and the in-game module's memory (tools/bzlayout.py).
    constexpr uint32_t kModuleSlot = 0x8010FFE0;
    constexpr uint32_t kModuleEnd = 0x802D96B0;

    struct Entry {
        Cheat cheat;
        uint32_t flag = 0;                 // stock: the game's flag byte
        std::vector<const char*> codes {}; // GameShark: address/value pairs
    };

    Entry stock(const char* id, const char* name, const char* code, const char* text, uint32_t flag) {
        std::string description = std::string(text) + "<br/><br/>Code: <b>" + code + "</b>";
        return { { id, name, description, Section::Stock }, flag };
    }

    Entry gs(const char* id, const char* name, const char* text, std::vector<const char*> codes) {
        std::string description = std::string(text) + (text[0] ? " " : "") +
            "A GameShark code: not part of the game, so expect glitches, and turn it off if something goes wrong."
            "<br/><br/>GameShark code: <b>";
        for (size_t i = 0; i < codes.size(); i++) {
            description += (i ? (i % 2 ? " " : "<br/>") : "") + std::string(codes[i]);
        }
        return { { id, name, description + "</b>", Section::GameShark }, 0, codes };
    }

    const std::vector<Entry>& entries() {
        static const std::vector<Entry> list = {
            stock("free_buildings", "Free Buildings", "Hold Z, press L, R, L, R",
                "Building units and structures costs nothing.", 0x8006A1C0),
            stock("infinite_armor", "Infinite Armor", "Hold Z, press Up, Right, Down, Left",
                "You take no damage.", 0x8006A1C1),
            stock("infinite_ammo", "Infinite Ammo", "Hold Z, press B, C-Left, C-Down, A",
                "Your weapons never run out.", 0x8006A1C2),
            stock("free_satellite", "Free Satellite", "Hold Z, press A, Start, A, Start",
                "Satellite view without building a satellite.", 0x8006A1C3),
            stock("reveal_units", "Reveal All Units", "Hold Z and R, press C-Down",
                "Every unit on the map is visible to you.", 0x8006A1C4),
            stock("speed_fast", "Game Speed 1.5x", "Hold Z and R, press C-Up",
                "Everything moves half again as fast.", 0x8006A1C5),
            stock("speed_faster", "Game Speed 2x", "Hold Z and R, press C-Left",
                "Everything moves twice as fast (if Game Speed 1.5x is also on, it wins).", 0x8006A1C6),
            stock("level_select", "Level Select", "Hold Z, press A, B, A, B",
                "Every mission can be picked in mission select.", 0x8006A1F5),

            gs("gs_commander_us", "Commander: All American Missions", "Opens and completes them. Saved with your progress.",
                { "8006A1EC", "0011" }),
            gs("gs_commander_su", "Commander: All Soviet Missions", "Opens and completes them. Saved with your progress.",
                { "8006A1ED", "0008" }),
            gs("gs_commander_bd", "Commander: All Black Dog Missions", "Opens and completes them. Saved with your progress.",
                { "8006A1EF", "000A" }),
            gs("gs_pilot_us", "Pilot: All American Missions", "Opens and completes them. Saved with your progress.",
                { "8006A1F0", "0011" }),
            gs("gs_pilot_su", "Pilot: All Soviet Missions", "Opens and completes them. Saved with your progress.",
                { "8006A1F1", "0008" }),
            gs("gs_pilot_bd", "Pilot: All Black Dog Missions", "Opens and completes them. Saved with your progress.",
                { "8006A1F3", "000A" }),
            gs("gs_infinite_scrap", "Infinite Scrap", "", { "811FBCC6", "FFFF" }),
            gs("gs_max_scrap", "Max Scrap Limit", "", { "811FBCCA", "FFFF" }),
            gs("gs_infinite_pilots", "Infinite Pilots", "", { "811FBCCE", "FFFF" }),
            gs("gs_max_pilots", "Max Pilot Limit", "", { "811FBCD2", "FFFF" }),
            gs("gs_infinite_weapons", "Infinite Weapons", "In a vehicle.", { "811DF6EA", "04B0" }),
            gs("gs_infinite_health", "Infinite Vehicle Health", "", { "811DF6DE", "0BA9" }),
            gs("gs_sniper_shots", "Infinite Sniper Rifle Shots", "On foot.", { "811DA682", "0003" }),
        };
        return list;
    }

    // Set by the menu, read on the game thread.
    std::array<std::atomic<bool>, 64>& wanted() {
        static std::array<std::atomic<bool>, 64> flags{};
        return flags;
    }

    void write8(uint8_t* rdram, uint32_t addr, uint8_t value) {
        if (addr >= kModuleSlot && addr < kModuleEnd && bz_loaded_module() != kInGameModule) {
            return;
        }
        MEM_B(0, (gpr)(int32_t)addr) = (int8_t)value;
    }

    void run_codes(uint8_t* rdram, const std::vector<const char*>& codes) {
        for (size_t i = 0; i + 1 < codes.size(); i += 2) {
            const uint32_t word = (uint32_t)std::strtoul(codes[i], nullptr, 16);
            const uint32_t addr = 0x80000000u | (word & 0xFFFFFF);
            const uint16_t value = (uint16_t)std::strtoul(codes[i + 1], nullptr, 16);
            switch (word >> 24) {
            case 0x80:
                write8(rdram, addr, (uint8_t)value);
                break;
            case 0x81:
                write8(rdram, addr, (uint8_t)(value >> 8));
                write8(rdram, addr + 1, (uint8_t)value);
                break;
            default:
                break;
            }
        }
    }
}

const std::vector<Cheat>& bz::cheats::list() {
    static const std::vector<Cheat> cheats = [] {
        std::vector<Cheat> ret;
        for (const Entry& e : entries()) {
            ret.push_back(e.cheat);
        }
        return ret;
    }();
    return cheats;
}

void bz::cheats::set_enabled(size_t index, bool enabled) {
    if (index < wanted().size()) {
        wanted()[index].store(enabled);
    }
}

// func_80029C48, after the controller read (battlezone.us.rev0.toml).
extern "C" void bz_cheats_frame(uint8_t* rdram) {
    static std::array<bool, 64> was_on{};
    const std::vector<Entry>& all = entries();
    for (size_t i = 0; i < all.size() && i < was_on.size(); i++) {
        const Entry& e = all[i];
        const bool on = wanted()[i].load();
        if (e.flag != 0 && (on || was_on[i])) {
            write8(rdram, e.flag, on ? 1 : 0);
        }
        if (on) {
            run_codes(rdram, e.codes);
        }
        was_on[i] = on;
    }
}
