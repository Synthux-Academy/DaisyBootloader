// Host test for the Tap (SW2) hold decision at power-on (../sw2_hold.h).
// Run: make -C audrey/tests

#include <stdio.h>

#include "sw2_hold.h"

static int failures = 0;
#define CHECK(cond)                                                     \
    do                                                                  \
    {                                                                   \
        if(!(cond))                                                     \
        {                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            failures++;                                                 \
        }                                                               \
    } while(0)

// SW2 as a function of time in ms: down while t < down_ms, except for bounces
typedef struct
{
    double down_ms;     // let go at this time (a huge value: never)
    double bounce_from; // a run of up readings during the hold, [from, to)
    double bounce_to;
    double chatter_ms;  // after the release, down again every other ms for this long
} Press;

static int PressDown(const Press* p, double t)
{
    if(t < p->down_ms)
        return !(t >= p->bounce_from && t < p->bounce_to);
    if(t < p->down_ms + p->chatter_ms)
        return ((int)(t - p->down_ms) & 1) == 1;
    return 0;
}

// Runs the step function the way the boot flow's loop does, one reading every
// step_ticks, from the first read (t = 0, SW2 down). Returns the decision and when
// it came, in ms.
static Sw2Hold Run(const Press* p, uint32_t ticks_per_ms, uint32_t step_ticks, double* at_ms)
{
    Sw2Timer t = { 0, 0, 0 };
    for(uint32_t now = 0;; now += step_ticks)
    {
        const double  ms = (double)now / ticks_per_ms;
        const Sw2Hold h  = Sw2Step(&t, now == 0 ? 1 : PressDown(p, ms), now, ticks_per_ms);
        if(h != kSw2Pending)
        {
            *at_ms = ms;
            return h;
        }
        if(ms > 60000.0)
        {
            *at_ms = ms;
            return kSw2Pending; // never decided: a bug
        }
    }
}

static Press Clean(double down_ms)
{
    Press p = { down_ms, -1.0, -1.0, 0.0 };
    return p;
}

// The same answers at a 1 ms reading rate, at 64 MHz with a reading every 50
// cycles (about the real loop's rate), and at 64 MHz every 6400 cycles (0.1 ms)
static void Expect(const Press* p, Sw2Hold want, double at_lo, double at_hi, int line)
{
    static const uint32_t rates[][2] = { { 1u, 1u }, { 64000u, 6400u }, { 64000u, 50u } };
    for(size_t i = 0; i < sizeof rates / sizeof rates[0]; i++)
    {
        if(rates[i][1] == 50u && p->down_ms > 6000.0 && want != kSw2Flip)
            continue; // slow at this rate, and covered by the others
        double        at = 0.0;
        const Sw2Hold h  = Run(p, rates[i][0], rates[i][1], &at);
        if(h != want || at < at_lo || at > at_hi)
        {
            printf("FAIL line %d (rate %u/%u): got %d at %.3f ms, want %d in [%.1f, %.1f]\n",
                   line, (unsigned)rates[i][0], (unsigned)rates[i][1], (int)h, at,
                   (int)want, at_lo, at_hi);
            failures++;
        }
    }
}

int main(void)
{
    const double hold = AUDREY_TAP_HOLD_MS, rel = SW2_RELEASE_MS, min = SW2_MIN_TAP_MS;
    CHECK(AUDREY_TAP_HOLD_MS == 5000u);
    CHECK(SW2_RELEASE_MS == 20u && SW2_MIN_TAP_MS == 50u);

    // Down at the first read only, or for less than 50 ms: a glitch, normal start
    Press p = Clean(0.0);
    Expect(&p, kSw2Glitch, rel, rel + 1.0, __LINE__);
    p = Clean(10.0);
    Expect(&p, kSw2Glitch, 10.0 + rel, 10.0 + rel + 1.0, __LINE__);
    p = Clean(min - 1.0);
    Expect(&p, kSw2Glitch, min - 1.0 + rel, min + rel, __LINE__);

    // Down 50 ms up to just under 5 s, then let go: bootloader, 20 ms after the release
    p = Clean(min + 1.0);
    Expect(&p, kSw2Bootloader, min + 1.0 + rel, min + rel + 2.0, __LINE__);
    p = Clean(1000.0);
    Expect(&p, kSw2Bootloader, 1000.0 + rel, 1000.0 + rel + 1.0, __LINE__);
    p = Clean(4000.0);
    Expect(&p, kSw2Bootloader, 4000.0 + rel, 4000.0 + rel + 1.0, __LINE__);
    p = Clean(hold - 1.0);
    Expect(&p, kSw2Bootloader, hold - 1.0 + rel, hold + rel, __LINE__);

    // Still down at 5 s: flip, decided right then, with SW2 still held
    p = Clean(1e9);
    Expect(&p, kSw2Flip, hold, hold + 0.1, __LINE__);
    p = Clean(hold + 0.5);
    Expect(&p, kSw2Flip, hold, hold + 0.1, __LINE__);

    // Contact bounce during the hold (up for less than 20 ms) isn't a release
    p             = Clean(1e9);
    p.bounce_from = 1000.0;
    p.bounce_to   = 1000.0 + rel - 2.0;
    Expect(&p, kSw2Flip, hold, hold + 0.1, __LINE__);
    p.down_ms = 3000.0;
    Expect(&p, kSw2Bootloader, 3000.0 + rel, 3000.0 + rel + 1.0, __LINE__);
    // ... but up for 20 ms is, and a hold shorter than 50 ms before it is a glitch
    p             = Clean(1e9);
    p.bounce_from = 30.0;
    p.bounce_to   = 30.0 + rel + 5.0;
    Expect(&p, kSw2Glitch, 30.0 + rel, 30.0 + rel + 1.0, __LINE__);
    p.bounce_from = 2000.0;
    p.bounce_to   = 2000.0 + rel + 5.0;
    Expect(&p, kSw2Bootloader, 2000.0 + rel, 2000.0 + rel + 1.0, __LINE__);

    // Chatter at the release: decided 20 ms after the last down reading
    p            = Clean(2500.0);
    p.chatter_ms = 9.0;
    Expect(&p, kSw2Bootloader, 2500.0 + 8.0 + rel, 2500.0 + 9.0 + rel + 1.0, __LINE__);

    // Let go just before 5 s and pressed again before 20 ms are up: the hold goes on
    // and the press at or after 5 s flips
    p             = Clean(1e9);
    p.bounce_from = hold - 5.0;
    p.bounce_to   = hold + 5.0;
    Expect(&p, kSw2Flip, hold + 5.0, hold + 5.1, __LINE__);

    // The release window and the hold at the real clock fit in 32 bits
    CHECK((AUDREY_TAP_HOLD_MS + SW2_RELEASE_MS) * 64000ull < 0xFFFFFFFFull);

    printf("%s (Tap hold %u ms, release %u ms, minimum %u ms): %d failure(s)\n",
           failures ? "FAILED" : "passed", (unsigned)AUDREY_TAP_HOLD_MS,
           (unsigned)SW2_RELEASE_MS, (unsigned)SW2_MIN_TAP_MS, failures);
    return failures ? 1 : 0;
}
