#include "patches.h"
#include "ui_funcs.h"

// recompui's per-frame UI callback pump (recomp_run_ui_callbacks) runs the
// callbacks of UI that mods build with recompui. Nothing calls it yet: the
// config menu doesn't need it in this port, and no mod UI exists. If that
// changes, call it once per frame from a patch of the game's per-VI swap
// routine.
//
// In the BattleTanx: Global Assault port this was a RECOMP_PATCH that
// replaced that game's VI-swap-throttle routine (reached once per real VI
// tick from its VI-message dispatch, and the only caller of osViSwapBuffer)
// with a faithful reimplementation plus a recomp_run_ui_callbacks() call at
// the top. A RECOMP_PATCH replaces the whole function at a real call
// boundary; a [[patches.hook]] mid-body splice was tried there first and
// caused hangs, since it shares ctx with the surrounding function's
// register-resident locals.
//
// For Battlezone: locate the function that is reached once per VI and calls
// osViSwapBuffer (search BattlezoneSyms/battlezone.us.rev0.syms.toml /
// the disassembly for osViSwapBuffer callers), reimplement it here under its
// symbol name as
//
//     RECOMP_PATCH void func_XXXXXXXX(...) {
//         bz_run_ui_callbacks_per_vi();
//         ... original logic, verified against the disassembly ...
//     }
//
// and declare any original game functions it calls as plain externs (they
// resolve by name through patches.toml's func_reference_syms_file). The
// helper below also gives the patch pipeline a real function to build.
void bz_run_ui_callbacks_per_vi(void) {
    recomp_run_ui_callbacks();
}
