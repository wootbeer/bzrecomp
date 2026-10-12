#ifndef BZ_WIDESCREEN_H
#define BZ_WIDESCREEN_H

namespace bz {
    // How much wider than 4:3 RT64 is drawing (1 outside Expand). Set by the
    // frontend each frame, read by src/game/widescreen_hud.cpp.
    void set_widescreen_scale(float scale);
    float get_widescreen_scale();
}

#endif
