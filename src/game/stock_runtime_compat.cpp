// Stock-runtime compatibility shims for functions N64Recomp's own built-in
// `ignored_funcs` list (N64Recomp/src/symbol_lists.cpp) expects some
// runtime to provide under a `_recomp` suffix, but that stock
// N64ModernRuntime doesn't implement (confirmed by grepping its whole
// source). CMakeLists.txt's BZ_FORKED_RUNTIME check leaves this file out when
// building against a runtime fork that already has them.
//
// Each implementation is a reading of real libultra behavior. They were
// written and verified for the BattleTanx: Global Assault port.

#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"
#include "recomp.h"

#include <cstdio>

// --- COP0 Status register read -------------------------------------------
// __osGetSR() just does `mfc0 $v0, C0_SR` on real hardware. Nothing in this
// runtime emulates COP0 state, so there's no real value to return.
// librecomp's own equivalent for the sibling function __osSetFpcCsr_recomp
// (lib/N64ModernRuntime/librecomp/src/ultra_translation.cpp) does the same
// thing for the same reason.
extern "C" void __osGetSR_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = (gpr)(int32_t)ctx->status_reg;
}

// --- libultra thread scheduler internals ---------------------------------
// Real libultra prototypes (queue head in $a0, thread pointer in $a1): `__osEnqueueThread(OSThread **queue, OSThread *t)`,
// `__osDequeueThread(OSThread **queue, OSThread *t)`,
// `OSThread *__osPopThread(OSThread **queue)`, `void __osDispatchThread(void)`.
// ultramodern already implements the same priority-queue-of-OSThread
// operations these need (ultramodern/include/ultramodern/ultramodern.hpp)
// for its own osStartThread/osCreateThread/etc., operating directly on the
// guest OSThread queue linked list in rdram -- not ultramodern's own
// private state -- so calling straight through to those should be
// behaviorally equivalent regardless of which queue (a message queue's
// blocked-thread list, the run queue, etc.) is being manipulated.
extern "C" void __osEnqueueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_insert(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osDequeueThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ultramodern::thread_queue_remove(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4, (PTR(OSThread))(int32_t)ctx->r5);
}

extern "C" void __osPopThread_recomp(uint8_t* rdram, recomp_context* ctx) {
    ctx->r2 = ultramodern::thread_queue_pop(rdram, (PTR(PTR(OSThread)))(int32_t)ctx->r4);
}

// __osDispatchThread's real job (pick the next ready thread off the run
// queue and context-switch into it) is done very differently here than on
// real hardware: real hardware does it with a raw register/COP0 context
// restore, while ultramodern runs each N64 thread as a real host thread
// and switches via semaphore signaling. run_next_thread_and_wait is
// ultramodern's own equivalent entry point for "give up the CPU and let
// the scheduler pick who runs next."
extern "C" void __osDispatchThread_recomp(uint8_t* rdram, recomp_context*) {
    ultramodern::run_next_thread_and_wait(rdram);
}

// --- SI access serialization ----------------------------------------------
// libultra serializes SI bus use (controller reads, Controller Pak access)
// through a one-message queue. Everything that touches the SI here is the
// runtime's own (osContInit/osContStartReadData) or the pak HLE
// (controller_pak_hle.cpp), which don't use that queue, so creating, taking
// and releasing it are all no-ops.
extern "C" void __osSiCreateAccessQueue_recomp(uint8_t*, recomp_context*) {
}

extern "C" void __osSiGetAccess_recomp(uint8_t*, recomp_context*) {
}

extern "C" void __osSiRelAccess_recomp(uint8_t*, recomp_context*) {
}

// --- COP0 ------------------------------------------------------------------
// Cause register: no exceptions are ever pending under the runtime. Only the
// debug server (kdebugserver) reads it.
extern "C" void __osGetCause_recomp(uint8_t*, recomp_context* ctx) {
    ctx->r2 = 0;
}

// The game's main loop (func_800295D0) does __osSetSR(__osGetSR() | SR_FR) to
// switch the FPU to 32 64-bit registers. librecomp's cop0_status_write
// handles the FR bit (it repoints the odd float registers) and refuses any
// other change, so pass FR through and keep the rest of the status register.
extern "C" void cop0_status_write(recomp_context* ctx, gpr value);
extern "C" void __osSetSR_recomp(uint8_t*, recomp_context* ctx) {
    constexpr uint32_t SR_FR = 0x04000000;
    uint32_t sr = (ctx->status_reg & ~SR_FR) | ((uint32_t)ctx->r4 & SR_FR);
    cop0_status_write(ctx, (gpr)(int32_t)sr);
}

// --- Timer / VI internals --------------------------------------------------
// Both are called with no arguments from libultra's VI manager thread code,
// matching their real void-void libultra prototypes.
// ultramodern manages both VI framebuffer swaps (via the renderer context,
// tied to the real osViSwapBuffer/osViGetCurrentFramebuffer exports) and
// timer expiry (ultramodern::init_timers, backing the real
// osSetTimer/osStopTimer exports) through its own separate mechanism
// already, so these ROM-internal calls -- which just update the N64-side
// bookkeeping structures those subsystems would otherwise touch on real
// hardware -- are made no-ops here rather than risk them fighting with
// ultramodern's own tracking of the same state. The game runs correctly
// with them as no-ops; if VI timing or timer-driven gameplay logic ever
// misbehaves, check whether the ROM reads that bookkeeping directly.
extern "C" void __osTimerInterrupt_recomp(uint8_t*, recomp_context*) {
}

extern "C" void __osViSwapContext_recomp(uint8_t*, recomp_context*) {
}
