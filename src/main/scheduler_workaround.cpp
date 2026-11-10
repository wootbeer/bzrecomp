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
// The busy-wait hooks tools/scan_spin_loops.py generates into
// battlezone.us.rev0.toml call `bz_yield_via_priority_drop(rdram);` from
// inside each polling loop (declared in include/bz_recomp_hooks.h, which is
// force-included into RecompiledFuncs/).

#include <chrono>
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
//
// Power use (from the BattleTanx port, its GitHub issue 6): the loops call
// this back to back, so a game thread would spin a whole CPU core while it
// waits (on the N64 the CPU spun there too). What the loops wait for comes
// from another game thread, which only becomes ready when an external message
// (VI, AI, ...) is delivered, and the hand-off below already lets any thread
// that was ready run. So once back-to-back calls (under 1 ms apart) have gone
// on for 2 ms -- the loop is really waiting -- each further call first sleeps
// until the next external message arrives, at most 1 ms, instead of
// spinning. Shorter runs never sleep, so a loop that calls this a few times
// in a normal pass, or a flag the hand-off clears, isn't slowed.
extern "C" void bz_yield_via_priority_drop(uint8_t* rdram) {
    // Calls less than 1 ms apart are one wait; once a wait has gone on for
    // 2 ms, each further call first sleeps until the next external message.
    static thread_local std::chrono::steady_clock::time_point last_call{}, wait_start{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_call > std::chrono::milliseconds(1)) {
        wait_start = now;
    }
    else if (now - wait_start >= std::chrono::milliseconds(2)) {
        ultramodern::wait_for_external_message_timed(rdram, 1);
    }

    dequeue_external_messages(rdram);

    int32_t saved_pri = osGetThreadPri(rdram, 0);
    osSetThreadPri(rdram, 0, 0);
    osSetThreadPri(rdram, 0, saved_pri);
    last_call = std::chrono::steady_clock::now();
}
