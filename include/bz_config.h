#ifndef __BZ_CONFIG_H__
#define __BZ_CONFIG_H__

#include <cstdint>

#include "librecomp/config.hpp"

// Game-specific options, shown on the General tab (src/main/game_config.cpp).
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

    Accessory get_accessory(int port);
    bool get_local_multiplayer();
}

#endif
