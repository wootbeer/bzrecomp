#ifndef BZ_PATCHES_PATCHES_H
#define BZ_PATCHES_PATCHES_H

// Section attributes N64Recomp looks for when it processes patches.elf
// (patches.ld routes each of these into its own linker section):
//   .recomp_patch  -- fully replaces an existing function, matched by name
//                      against func_reference_syms_file.
//   .recomp_export -- a genuinely new function, exported for other code
//                      (or future mods) to call by name.
#define RECOMP_PATCH __attribute__((section(".recomp_patch")))
#define RECOMP_EXPORT __attribute__((section(".recomp_export")))

// Renames so patch code can call the stock-runtime functions under their
// real libultra names; syms.ld maps the _recomp names to the dummy
// 0x8F0000xx addresses N64Recomp recognizes as calls into native code.
#define osViBlack osViBlack_recomp
#define osViSwapBuffer osViSwapBuffer_recomp

#include "ultra64.h"

// The MIPS patch build is plain freestanding C (-nostdinc, no <stdbool.h>),
// but recompui_event_structs.h (shared verbatim with the host-side C++
// build, where bool is native) uses bool. Host C++ doesn't hit this path.
#ifdef MIPS
#ifndef __cplusplus
typedef int bool;
#define true 1
#define false 0
#endif
#endif

#endif // BZ_PATCHES_PATCHES_H
