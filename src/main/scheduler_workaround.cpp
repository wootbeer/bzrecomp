// Workaround for a gap in N64Recomp/ultramodern's cooperative thread
// scheduler, carried over from the BattleTanx: Global Assault port where it
// was needed. Busy-wait polling loops in original N64 code can rely on a
// real hardware interrupt unconditionally preempting whatever was running so
// the thread clearing the poll flag gets a turn. Ultramodern only switches
// threads at specific recognized library calls, and only to a *strictly*
// higher-priority ready thread (check_running_queue in
// ultramodern/src/scheduling.cpp) -- real libultra's osYieldThread, which
// would round-robin to an equal-priority thread too, is unimplemented in the
// runtime (librecomp/src/ultra_translation.cpp just asserts false). A bare
// polling loop with no OS call inside it never gives the scheduler a chance,
// and even a plain yield can't hand off to a same-or-lower-priority thread.
//
// A priority drop alone isn't enough when the thread that needs to run is
// blocked in osRecvMesg on a hardware event (VI/AI/etc) rather than merely
// lower-priority-and-ready. Those events are delivered by a host thread via
// ultramodern::enqueue_external_message, which pushes onto a separate
// `external_messages` queue (ultramodern/src/mesgqueue.cpp) -- it is NOT
// written directly into the target's OSMesgQueue. That queue is only
// drained, and the message actually delivered (moving the blocked receiver
// into the ready queue), when some game thread drains it. So this drains
// external messages first and only then does the priority-drop trick, so any
// thread that just became ready from a drained message also gets an actual
// handoff despite not being strictly higher priority.
//
// Nothing calls this yet. To use it, add a [[patches.hook]] inside the
// offending polling loop in battlezone.us.rev0.toml whose text calls
// `bz_yield_via_priority_drop(rdram);` (declared in include/bz_recomp_hooks.h,
// which is force-included into RecompiledFuncs/).

#include <cstdint>

#include "ultramodern/ultramodern.hpp"

extern "C" int32_t osGetThreadPri(uint8_t* rdram, int32_t t);
extern "C" void osSetThreadPri(uint8_t* rdram, int32_t t, int32_t pri);

// Not extern "C" and not declared in any header, but has ordinary external
// C++ linkage (ultramodern/src/mesgqueue.cpp) -- drains every
// currently-pending message in one pass, unlike yield_self_1ms's
// wait_for_external_message_timed, which pops at most one.
void dequeue_external_messages(uint8_t* rdram);

// external_messages is one shared FIFO fed by every source (VI, AI, SP, DP,
// Timer, SI), so popping a single entry per call can starve a waiter: with
// other sources producing faster than the drain rate, the message it's
// waiting for can sit arbitrarily far back behind a growing backlog.
// Draining the whole queue in one pass can't starve that way.
extern "C" void bz_yield_via_priority_drop(uint8_t* rdram) {
    dequeue_external_messages(rdram);

    int32_t saved_pri = osGetThreadPri(rdram, 0);
    osSetThreadPri(rdram, 0, 0);
    osSetThreadPri(rdram, 0, saved_pri);
}
