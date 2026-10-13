#ifndef __BZ_CONFIG_H__
#define __BZ_CONFIG_H__

#include <cstdint>

#include "librecomp/config.hpp"

// Game-specific options, shown on the General and Graphics tabs
// (src/main/game_config.cpp).
namespace bz::config {
    // What's plugged into a controller's accessory slot. Like on the N64, one
    // controller holds one pak at a time.
    enum class Accessory : uint32_t {
        ControllerPak, // saves -- src/game/controller_pak_hle.cpp
        RumblePak,
        None,
    };

    // Adds this game's options to the General tab. Call between
    // recompui::config::create_general_tab() and recompui::config::finalize().
    void add_general_options(recomp::config::Config& general);

    // The filter over the final picture (RT64::ScreenFilter, lib-patches/rt64/0010).
    enum class ScreenFilter : uint32_t {
        None,
        CRT,
        Scanlines,
        LCD,
    };

    // How far objects (and the ground already built around the view) are
    // drawn (src/game/draw_distance.cpp).
    enum class DrawDistance : uint32_t {
        Original,
        Extended,
    };

    // Adds Screen Filter and Draw Distance to the Graphics tab. Call between
    // recompui::config::create_graphics_tab() and recompui::config::finalize().
    void add_graphics_options(recomp::config::Config& graphics);
    ScreenFilter get_screen_filter();
    DrawDistance get_draw_distance();

    Accessory get_accessory(int port);
    bool get_local_multiplayer();
}

#endif
