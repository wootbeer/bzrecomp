# patches/

C code compiled for the N64 (MIPS) and recompiled alongside the game, for
changes that replace a whole game function. A `RECOMP_PATCH` function here
takes the place of the original function of the same name.

- `recompui_patches.c`: will hold the patch of Battlezone's per-VI swap
  routine that adds the frontend's per-frame UI pump. That routine hasn't
  been identified yet (see the TODO in the file), so for now it only defines
  the not-yet-wired helper.

The build is automatic: `make` here produces `patches.elf` (through WSL on
Windows, since the Windows clang builds have no MIPS backend), then N64Recomp
recompiles it using `patches.toml` in the repo root. `syms.ld` gives native
runtime functions dummy addresses so the patches can call them.

Smaller fixes don't live here: instruction patches and hooks that run native
code at a point in a game function belong in `battlezone.us.rev0.toml`, with
the native code in `src/game/`.
