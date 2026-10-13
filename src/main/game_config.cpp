// Game-specific options on the General tab (which pak each controller holds,
// and local multiplayer) and the Graphics tab (Screen Filter, Draw Distance). See
// include/bz_config.h.
#include "bz_config.h"

#include "recompui/config.h"

namespace {
    using Accessory = bz::config::Accessory;

    const std::string p1_accessory_id = "p1_accessory";
    const std::string other_accessory_id = "other_accessory";
    const std::string local_multiplayer_id = "local_multiplayer";

    const std::vector<recomp::config::ConfigOptionEnumOption> accessory_options = {
        {Accessory::ControllerPak, "ControllerPak", "Controller Pak"},
        {Accessory::RumblePak, "RumblePak", "Rumble Pak"},
        {Accessory::None, "None", "None"},
    };

    const std::string screen_filter_id = "screen_filter";
    const std::vector<recomp::config::ConfigOptionEnumOption> screen_filter_options = {
        {bz::config::ScreenFilter::None, "None", "None"},
        {bz::config::ScreenFilter::CRT, "CRT", "CRT"},
        {bz::config::ScreenFilter::Scanlines, "Scanlines", "Scanlines"},
        {bz::config::ScreenFilter::LCD, "LCD", "LCD"},
    };

    const std::string draw_distance_id = "draw_distance";
    const std::vector<recomp::config::ConfigOptionEnumOption> draw_distance_options = {
        {bz::config::DrawDistance::Original, "Original", "Original"},
        {bz::config::DrawDistance::Extended, "Extended", "Extended"},
    };

    // Looked up by id on each read: the Config& create_general_tab() returns
    // lives in a container that moves when later tabs are created, so a
    // pointer kept from it dangles.
    bool options_added = false;
    bool graphics_options_added = false;
}

void bz::config::add_general_options(recomp::config::Config& general) {
    options_added = true;

    general.add_enum_option(
        p1_accessory_id,
        "Player 1 Accessory",
        "The pak plugged into player 1's controller. As on the N64, a controller holds one pak at a time: "
        "the <b>Controller Pak</b> stores saves (in the saves folder as a standard 32 KB .mpk file), "
        "the <b>Rumble Pak</b> vibrates your controller."
        "<br/><br/>"
        "Changing this is like swapping paks: the game notices the next time it checks.",
        accessory_options,
        Accessory::ControllerPak
    );

    general.add_enum_option(
        other_accessory_id,
        "Players 2-4 Accessory",
        "The pak plugged into the other players' controllers in local multiplayer. "
        "Each player's Controller Pak is a separate .mpk file.",
        accessory_options,
        Accessory::RumblePak
    );

    general.add_bool_option(
        local_multiplayer_id,
        "Local Multiplayer",
        "Lets up to 4 players use their own controllers."
        "<br/><br/>"
        "<b>Restart the game after changing this.</b> Then open the Controls tab and use "
        "<b>Assign players</b> to choose each player's controller or keyboard.",
        false
    );
}

bz::config::Accessory bz::config::get_accessory(int port) {
    if (!options_added) {
        return Accessory::None;
    }
    const std::string& id = port == 0 ? p1_accessory_id : other_accessory_id;
    return static_cast<Accessory>(std::get<uint32_t>(recompui::config::get_general_config().get_option_value(id)));
}

bool bz::config::get_local_multiplayer() {
    if (!options_added) {
        return false;
    }
    return std::get<bool>(recompui::config::get_general_config().get_option_value(local_multiplayer_id));
}

void bz::config::add_graphics_options(recomp::config::Config& graphics) {
    graphics_options_added = true;

    graphics.add_enum_option(
        screen_filter_id,
        "Screen Filter",
        "A filter over the final picture, lined up with the N64's own lines at any resolution."
        "<br/><br/>"
        "<b>CRT</b>: a TV's scanlines, glow and shadow mask (Timothy Lottes' CRT shader).<br/>"
        "<b>Scanlines</b>: dark gaps between the lines only.<br/>"
        "<b>LCD</b>: a handheld LCD's red, green and blue stripes (LCD3x).",
        screen_filter_options,
        ScreenFilter::None
    );

    graphics.add_enum_option(
        draw_distance_id,
        "Draw Distance",
        "How far away objects are drawn."
        "<br/><br/>"
        "<b>Original</b>: as on the N64, 200 units.<br/>"
        "<b>Extended</b>: out to the edge of the ground the game has already built around your view "
        "(up to twice as far, depending on where you're facing), and that ground shows too, "
        "so nothing appears floating past it.",
        draw_distance_options,
        DrawDistance::Extended
    );
}

bz::config::ScreenFilter bz::config::get_screen_filter() {
    if (!graphics_options_added) {
        return ScreenFilter::None;
    }
    return static_cast<ScreenFilter>(std::get<uint32_t>(recompui::config::get_graphics_config().get_option_value(screen_filter_id)));
}

bz::config::DrawDistance bz::config::get_draw_distance() {
    if (!graphics_options_added) {
        return DrawDistance::Original;
    }
    return static_cast<DrawDistance>(std::get<uint32_t>(recompui::config::get_graphics_config().get_option_value(draw_distance_id)));
}
