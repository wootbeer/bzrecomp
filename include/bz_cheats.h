#ifndef BZ_CHEATS_H
#define BZ_CHEATS_H

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The Cheats and Hacks tabs (src/main/cheats_config.cpp) and what each cheat
// does in the game (src/game/cheats.cpp).
namespace bz::cheats {
    enum class Section {
        Stock,      // the game's own codes (Cheats tab)
        GameShark,  // cheat-device codes (Hacks tab)
    };

    struct Cheat {
        std::string id;          // saved in cheats.json / hacks.json
        std::string name;        // shown in the menu
        std::string description;
        Section section;
    };

    // Every cheat, in menu order.
    const std::vector<Cheat>& list();

    // Turns cheat `index` in list() on or off; applied on the game's next
    // controller read.
    void set_enabled(size_t index, bool enabled);

    // Adds the Cheats and Hacks tabs. Call between the other create_*_tab()
    // calls and recompui::config::finalize().
    void add_cheats_tabs();
}

#endif
