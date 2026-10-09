#ifndef BZ_PATCHES_ULTRA64_H
#define BZ_PATCHES_ULTRA64_H

// The few N64 types and libultra functions the MIPS patch code uses. Written
// for this project in place of the original SDK headers: only what
// patches/*.c needs, with libultra's names and calling conventions.

typedef signed char s8;
typedef unsigned char u8;
typedef signed short s16;
typedef unsigned short u16;
typedef signed long s32;
typedef unsigned long u32;
typedef signed long long s64;
typedef unsigned long long u64;
typedef float f32;
typedef double f64;

#ifndef NULL
#define NULL 0
#endif

// Video interface (provided by the runtime; see patches.h's renames).
void osViBlack(u8 active);
void osViSwapBuffer(void* frameBufPtr);

#endif // BZ_PATCHES_ULTRA64_H
