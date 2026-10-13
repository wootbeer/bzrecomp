#ifndef BZ_WIDESCREEN_H
#define BZ_WIDESCREEN_H

#include <cstdint>

namespace bz {
    // How much wider than 4:3 RT64 is drawing (1 outside Expand). Set by the
    // frontend each frame, read by src/game/widescreen_hud.cpp.
    void set_widescreen_scale(float scale);
    float get_widescreen_scale();

    // src/game/widescreen_markers.cpp: the target markers' HUD element,
    // drawn into the overlay list between these (anchoring code calls them).
    void markers_begin(uint8_t* rdram);
    void markers_end(uint8_t* rdram);
    bool markers_drawing();
}

#endif
