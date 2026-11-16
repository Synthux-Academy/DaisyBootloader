#pragma once

// Tap (SW2) held at power-on: what the hold means, decided one reading at a time.
// The pure part of the boot flow's Sw2TimeHold (startup.c), so a host test can
// check it (tests/sw2_hold_test.c). See ../specs/audrey-bootloader.md, "Boot flow".

#include <stdint.h>

#include "boot_layout.h"

// Up for this long counts as let go (contact bounce is shorter)
#define SW2_RELEASE_MS 20u
// Down for at least this long counts as a press; less is a glitch at the first
// read, and a normal start
#define SW2_MIN_TAP_MS 50u

typedef enum
{
    kSw2Pending,    // still timing
    kSw2Glitch,     // down at the first read only: a normal start
    kSw2Bootloader, // let go before AUDREY_TAP_HOLD_MS: bootloader mode
    kSw2Flip,       // still down at AUDREY_TAP_HOLD_MS: start the app at once (SW2
                    // still down) with the tap flag, so it flips its auto-sleep setting
} Sw2Hold;

// Zero it before the first reading
typedef struct
{
    uint32_t last_down; // time of the last reading with SW2 down
    uint32_t up_since;  // start of the current run of up readings
    int      up;        // the last reading was up
} Sw2Timer;

// One reading of SW2 (down or not) at time now, counted from the first read in
// ticks of ticks_per_ms per millisecond; the first read itself was down. Returns
// kSw2Pending until the hold is decided. AUDREY_TAP_HOLD_MS x ticks_per_ms must
// fit in 32 bits (5000 ms at the 64 MHz reset clock: 3.2e8).
// Always inlined: the boot flow runs from flash before the code in RAM is there,
// so this must not become a call into .text.
static inline __attribute__((always_inline)) Sw2Hold
Sw2Step(Sw2Timer* t, int down, uint32_t now, uint32_t ticks_per_ms)
{
    if(down)
    {
        t->up        = 0;
        t->last_down = now;
        return now >= AUDREY_TAP_HOLD_MS * ticks_per_ms ? kSw2Flip : kSw2Pending;
    }
    if(!t->up)
    {
        t->up       = 1;
        t->up_since = now;
    }
    if(now - t->up_since < SW2_RELEASE_MS * ticks_per_ms)
        return kSw2Pending;
    return t->last_down >= SW2_MIN_TAP_MS * ticks_per_ms ? kSw2Bootloader : kSw2Glitch;
}
