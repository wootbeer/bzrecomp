// The Cheats tab (the game's own codes as toggles) and the Hacks tab
// (GameShark codes), kept apart so it's clear which are part of the game.
// The game side is src/game/cheats.cpp.
#include "bz_cheats.h"

#include "recompui/config.h"

namespace {
    const std::string cheats_id = "cheats";
    const std::string hacks_id = "hacks";
}

void bz::cheats::add_cheats_tabs() {
    recompui::config::create_config_tab("Cheats", cheats_id, false);
    recompui::config::create_config_tab("Hacks", hacks_id, false);
    const std::vector<Cheat>& all = list();
    for (size_t i = 0; i < all.size(); i++) {
        const Cheat& cheat = all[i];
        // Looked up by id: the Config& create_config_tab() returns lives in a
        // container that moves when later tabs are made.
        recomp::config::Config& tab = recompui::config::get_config(cheat.section == Section::Stock ? cheats_id : hacks_id);
        tab.add_bool_option(cheat.id, cheat.name, cheat.description, false);

        // Also called when the saved settings load.
        tab.add_option_change_callback(cheat.id,
            [i](recomp::config::ConfigValueVariant value, recomp::config::ConfigValueVariant, recomp::config::OptionChangeContext) {
                if (const bool* on = std::get_if<bool>(&value)) {
                    set_enabled(i, *on);
                }
            });
    }
}
