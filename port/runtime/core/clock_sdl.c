/* The DS's time from SDL's nanosecond counter (clock.h), less the time spent held, at the rate the
 * screen's lock sets: clock.c's logic where there is no Windows. */
#include "clock.h"
#include <SDL3/SDL.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static SDL_Mutex *lock;
static SDL_Condition *changed;
static Uint64 origin, paused_at, paused_total;
static unsigned reasons;
static bool wake_pending;
/* The rate (under lock): the DS's time runs `rate` times the counter's from `rate_ns` (the counter less
 * the holds, from the start), where it was `rate_cycles`; 1 from the start. */
static double rate = 1.0;
static Uint64 rate_ns;
static u64 rate_cycles;
/* The screen's refreshes as the host reports them (khdays_clock_screen_timing; under lock): how long one
 * lasts, when one was (SDL's counter) and when that was said. None reported: period_ns 0. */
static Uint64 period_ns, refresh_ns, reported_ns;

void khdays_clock_init(void)
{
    if (!lock)
        lock = SDL_CreateMutex();
    if (!changed)
        changed = SDL_CreateCondition();
    if (!lock || !changed)
        khdays_hal_unimplemented("cannot initialize game clock");
    origin = SDL_GetTicksNS();
}

/* The cycles at `ns` of the counter (less the holds, from the start), under lock. */
static u64 cycles_at(Uint64 ns)
{
    if (rate == 1.0 && rate_ns == 0)
        return ns / 1000000000 * KHDAYS_CLOCK_HZ + ns % 1000000000 * KHDAYS_CLOCK_HZ / 1000000000;
    /* since the rate was last set: a VBlank ago while the lock holds it */
    return rate_cycles + (u64)((double)(Sint64)(ns - rate_ns) / 1e9 * (double)KHDAYS_CLOCK_HZ * rate);
}

static u64 cycles_locked(void)
{
    return cycles_at((reasons ? paused_at : SDL_GetTicksNS()) - origin - paused_total);
}

u64 khdays_clock_cycles(void)
{
    SDL_LockMutex(lock);
    u64 now = cycles_locked();
    SDL_UnlockMutex(lock);
    return now;
}

double khdays_clock_rate(void)
{
    if (!lock)
        return 1.0;
    SDL_LockMutex(lock);
    double r = rate;
    SDL_UnlockMutex(lock);
    return r;
}

/* Sets the rate from now on, the time going on from where it is; under lock. */
static void set_rate_locked(double r)
{
    if (r != rate) {
        const Uint64 ns = (reasons ? paused_at : SDL_GetTicksNS()) - origin - paused_total;
        rate_cycles = cycles_at(ns);
        rate_ns = ns;
        rate = r;
        SDL_BroadcastCondition(changed); /* a sleeper counts its wait again */
    }
}

/* The counter's value when the DS's time reaches `cycle` at the rate now (the inverse of cycles_at);
 * under lock. */
static double counter_at(u64 cycle)
{
    double ns;
    if (rate == 1.0 && rate_ns == 0)
        ns = (double)(cycle / KHDAYS_CLOCK_HZ) * 1e9 + (double)(cycle % KHDAYS_CLOCK_HZ) * 1e9 / KHDAYS_CLOCK_HZ;
    else
        ns = (double)rate_ns + (double)(Sint64)(cycle - rate_cycles) / (double)KHDAYS_CLOCK_HZ / rate * 1e9;
    return ns + (double)origin + (double)paused_total;
}

/* The screen's lock (clock.h). */
static SDL_AtomicInt lock_screen;    /* the host's: a window shows the game */
static SDL_AtomicInt lock_refreshes; /* the screen's refreshes a VBlank while the lock holds, else 0 */
static int lock_trace = -1;          /* KHDAYS_TRACE_LOCK: the phase every 300 VBlanks */
static double lock_test_hz = -1.0;   /* KHDAYS_SCREEN_LOCK=HZ: a screen at that rate, whatever this one's */
static int lock_off;                 /* KHDAYS_SCREEN_LOCK=off */

int khdays_clock_screen_refreshes(void)
{
    return SDL_GetAtomicInt(&lock_refreshes);
}

void khdays_clock_lock_screen(int on)
{
    SDL_SetAtomicInt(&lock_screen, on != 0);
}

void khdays_clock_screen_timing(unsigned long long period, unsigned long long refresh_at)
{
    if (!lock)
        return;
    SDL_LockMutex(lock);
    period_ns = period;
    refresh_ns = refresh_at;
    reported_ns = SDL_GetTicksNS();
    SDL_UnlockMutex(lock);
}

void khdays_clock_vblank(u64 vblank)
{
    /* where the VBlank should come: this long before the screen's refresh (the picture made, drawn and
     * presented meanwhile), and how hard each VBlank's error pulls the rate */
    static const double lead_ms = 6.0, pull = 0.05, reach = 0.005;
    /* refresh times older than this are no screen's (the app in the background, the display link stopped) */
    static const Uint64 fresh_ns = 500000000;
    static unsigned traced;
    static int told;
    const double ds_frame = (double)KHDAYS_CYCLES_PER_FRAME / KHDAYS_CLOCK_HZ * 1e9;
    double refresh, phase, error, wanted, period, last;
    int refreshes;
    if (lock_trace < 0) {
        const char *test = getenv("KHDAYS_SCREEN_LOCK");
        lock_trace = getenv("KHDAYS_TRACE_LOCK") != NULL;
        lock_test_hz = test != NULL ? atof(test) : 0.0;
        lock_off = test != NULL && lock_test_hz <= 0.0;
    }
    const int screen = SDL_GetAtomicInt(&lock_screen);
    SDL_LockMutex(lock);
    period = (double)period_ns;
    last = (double)refresh_ns;
    if (period_ns != 0 && SDL_GetTicksNS() - reported_ns > fresh_ns)
        period = 0.0;
    if (lock_test_hz > 0.0) {
        /* refreshes from the game's start on, at that rate */
        period = 1e9 / lock_test_hz;
        last = (double)origin;
    }
    if (screen && !lock_off && told != (period > 0.0 ? 1 : 2)) {
        /* while a window shows the game, each time it changes: whether there is a screen to lock to */
        told = period > 0.0 ? 1 : 2;
        fprintf(stderr, period > 0.0 ? "clock: the screen's lock has refresh times (%.2f ms a refresh)\n"
                                     : "clock: the screen's lock is off: no refresh times reported here\n",
                period / 1e6);
    }
    if (!screen || reasons || lock_off || period <= 0.0) {
        if (lock_trace && ++traced % 300 == 0)
            fprintf(stderr, "lock: off (screen %d, held %d, period %.0f ns)\n", screen, reasons != 0, period);
        SDL_SetAtomicInt(&lock_refreshes, 0);
        set_rate_locked(1.0);
        SDL_UnlockMutex(lock);
        return;
    }
    /* the screen's refreshes a VBlank: one at 60 Hz, two at 120 Hz; none that fits at 75 or 144 Hz */
    refreshes = (int)(ds_frame / period + 0.5);
    refresh = period * (refreshes > 0 ? refreshes : 1);
    if (refreshes < 1 || refresh / ds_frame < 0.99 || refresh / ds_frame > 1.01) {
        SDL_SetAtomicInt(&lock_refreshes, 0);
        set_rate_locked(1.0);
        SDL_UnlockMutex(lock);
        return;
    }
    SDL_SetAtomicInt(&lock_refreshes, refreshes);
    /* the VBlank's place in the screen's frame (when it was due: the game's thread takes it a little
     * later), and how far from where it should be (-half to half) */
    phase = fmod(counter_at(vblank) - last, refresh);
    phase = phase < 0.0 ? phase + refresh : phase;
    error = phase - (refresh - lead_ms * 1e6);
    error -= refresh * floor(error / refresh + 0.5);
    /* late in the frame: run a little faster, so the next VBlanks come sooner */
    wanted = error / refresh * pull;
    wanted = wanted > reach ? reach : wanted < -reach ? -reach : wanted;
    set_rate_locked(ds_frame / refresh * (1.0 + wanted));
    if (lock_trace && ++traced % 300 == 0)
        fprintf(stderr, "lock: the VBlank %.2f ms after the screen's refresh (%.2f ms a frame), the time at %.4f\n",
                phase / 1e6, refresh / 1e6, rate);
    SDL_UnlockMutex(lock);
}

void khdays_clock_hold(unsigned reason, int on)
{
    SDL_LockMutex(lock);
    if (on && reason) {
        if (!reasons)
            paused_at = SDL_GetTicksNS();
        reasons |= reason;
    } else if (reasons & reason) {
        reasons &= ~reason;
        if (!reasons)
            paused_total += SDL_GetTicksNS() - paused_at;
    }
    SDL_BroadcastCondition(changed);
    SDL_UnlockMutex(lock);
}

int khdays_clock_held(unsigned reason)
{
    SDL_LockMutex(lock);
    int held = (reasons & reason) != 0;
    SDL_UnlockMutex(lock);
    return held;
}

void khdays_clock_wake(void)
{
    SDL_LockMutex(lock);
    wake_pending = true;
    SDL_BroadcastCondition(changed);
    SDL_UnlockMutex(lock);
}

static void sleep_until(u64 cycle, bool wakeable)
{
    SDL_LockMutex(lock);
    for (;;) {
        u64 now = cycles_locked();
        if (now >= cycle)
            break;
        if (wakeable && wake_pending) {
            wake_pending = false;
            break;
        }
        if (reasons)
            SDL_WaitCondition(changed, lock);
        else {
            u64 left = cycle - now;
            Sint32 ms = left > (u64)INT_MAX * KHDAYS_CLOCK_HZ / 1000
                            ? INT_MAX
                            : (Sint32)((left * 1000 + KHDAYS_CLOCK_HZ - 1) / KHDAYS_CLOCK_HZ);
            SDL_WaitConditionTimeout(changed, lock, ms);
        }
    }
    SDL_UnlockMutex(lock);
}
void khdays_clock_sleep_until(u64 cycle)
{
    sleep_until(cycle, false);
}
void khdays_clock_sleep_until_or_woken(u64 cycle)
{
    sleep_until(cycle, true);
}
