// Green UI theme for the launcher and config menus.
//
// recompui's default palette (lib/RecompFrontend/recompui/src/elements/
// ui_theme.cpp) is blue-tinted. Every blue-tinted colour is re-applied here
// with its green and blue channels swapped, which keeps each colour's
// brightness and contrast but moves its hue from blue to green (e.g. the
// background 2,7,18 -> 2,18,7 and the primary/title colour 29,93,226 ->
// 29,226,93). Neutral greys/whites and the semantic colours (warning,
// danger, success, player colours) are left as they are. Must run before
// the UI builds its stylesheet, i.e. before recomp::start.
#include "elements/ui_theme.h"

namespace {
    using recompui::theme::color;

    void set_green(color c) {
        recompui::Color v = recompui::theme::get_theme_color(c);
        recompui::theme::set_theme_color(c, recompui::Color{ v.r, v.b, v.g, v.a });
    }
}

namespace bz {
    void apply_theme() {
        for (color c : {
            color::Background1, color::Background2, color::Background3,
            color::BGOverlay, color::ModalOverlay, color::BGShadow2,
            color::Primary, color::PrimaryL, color::PrimaryD,
            color::PrimaryA5, color::PrimaryA20, color::PrimaryA30, color::PrimaryA50, color::PrimaryA80,
            color::Elevated, color::ElevatedSoft, color::ElevatedBorder, color::ElevatedBorderHard,
            color::A, color::AL, color::AD, color::AA5, color::AA20, color::AA30, color::AA50, color::AA80,
        }) {
            set_green(c);
        }
    }
}
