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

    // The glow around bright colours (RT64::Bloom, lib-patches/rt64/0011).
    enum class Bloom : uint32_t {
        Off,
        Low,
        High,
    };

    // Adds Screen Filter and Bloom to the Graphics tab. Call between
    // recompui::config::create_graphics_tab() and recompui::config::finalize().
    void add_graphics_options(recomp::config::Config& graphics);
    ScreenFilter get_screen_filter();
    bool get_screen_filter_menus_only();
    Bloom get_bloom();
    // Texture Filtering (the world) and HUD Filtering: Sharp draws them with
    // point sampling (src/game/texture_filter.cpp).
    bool get_sharp_textures();
    bool get_sharp_hud();

    Accessory get_accessory(int port);
    bool get_local_multiplayer();
}

#endif
