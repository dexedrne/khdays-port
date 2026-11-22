#include "../runtime/core/clock.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
void khdays_hal_unimplemented(const char *reason)
{
    SDL_Log("%s", reason);
    abort();
}
/* The screen's lock (clock.h) where the host reports the refreshes (khdays_clock_screen_timing, as the
 * phone's display link does): off with no times, a window not shown, a 75 Hz screen, old times or the
 * time held; on at 60 and 120 Hz, the DS's time then within half a percent of the screen's, never
 * backwards, and pulled so that the VBlanks come about 6 ms before a refresh. */
static int vblanks(int count, double refresh_hz, int report)
{
    const u64 frame = KHDAYS_CYCLES_PER_FRAME;
    u64 next = khdays_clock_cycles() / frame * frame + frame, last = khdays_clock_cycles();
    const Uint64 period = (Uint64)(1e9 / refresh_hz);
    for (int i = 0; i < count; ++i) {
        khdays_clock_sleep_until(next);
        const u64 now = khdays_clock_cycles();
        if (now < last)
            return -1;
        last = now;
        if (report) {
            /* the latest refresh at or before now, on a grid from SDL's zero */
            const Uint64 ns = SDL_GetTicksNS();
            khdays_clock_screen_timing(period, ns / period * period);
        }
        khdays_clock_vblank(next);
        next += frame;
    }
    return khdays_clock_screen_refreshes();
}

static int screen_lock(void)
{
    const double ds_hz = (double)KHDAYS_CLOCK_HZ / KHDAYS_CYCLES_PER_FRAME;
    if (khdays_clock_rate() != 1.0 || khdays_clock_screen_refreshes() != 0)
        return 10;
    khdays_clock_lock_screen(1);
    if (vblanks(3, 60.0, 0) != 0 || khdays_clock_rate() != 1.0) /* no times reported: off */
        return 11;
    if (vblanks(20, 60.0, 1) != 1) /* 60 Hz: one refresh a VBlank */
        return 12;
    double rate = khdays_clock_rate();
    if (rate < 60.0 / ds_hz * 0.9949 || rate > 60.0 / ds_hz * 1.0051)
        return 13;
    {
        /* locked: the VBlanks' place in the screen's frame settles (the pull is small: within 3 ms here) */
        const Uint64 period = (Uint64)(1e9 / 60.0);
        double worst = 0.0;
        u64 next = khdays_clock_cycles() / KHDAYS_CYCLES_PER_FRAME * KHDAYS_CYCLES_PER_FRAME + KHDAYS_CYCLES_PER_FRAME;
        double first = -1.0;
        for (int i = 0; i < 90; ++i) {
            khdays_clock_sleep_until(next);
            const Uint64 ns = SDL_GetTicksNS();
            khdays_clock_screen_timing(period, ns / period * period);
            khdays_clock_vblank(next);
            next += KHDAYS_CYCLES_PER_FRAME;
            if (i >= 60) {
                const double phase = (double)(ns % period) / 1e6;
                double off;
                if (first < 0.0)
                    first = phase;
                off = phase - first;
                off = off > 8.0 ? off - 16.667 : off < -8.0 ? off + 16.667 : off;
                worst = off > worst ? off : -off > worst ? -off : worst;
            }
        }
        SDL_Log("clock: locked at 60 Hz, the time at %.4f, the VBlanks within %.2f ms of one place in the frame",
                khdays_clock_rate(), worst);
        if (khdays_clock_screen_refreshes() != 1 || worst > 3.0)
            return 14;
    }
    if (vblanks(5, 120.0, 1) != 2) /* 120 Hz: two */
        return 15;
    if (vblanks(5, 75.0, 1) != 0 || khdays_clock_rate() != 1.0) /* 75 Hz: none fits */
        return 16;
    if (vblanks(5, 60.0, 1) != 1)
        return 17;
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1); /* held: off */
    khdays_clock_vblank(khdays_clock_cycles());
    if (khdays_clock_screen_refreshes() != 0 || khdays_clock_rate() != 1.0)
        return 18;
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
    if (vblanks(5, 60.0, 1) != 1)
        return 19;
    SDL_Delay(600); /* the times grown old (the app in the background): off */
    khdays_clock_vblank(khdays_clock_cycles());
    if (khdays_clock_screen_refreshes() != 0 || khdays_clock_rate() != 1.0)
        return 20;
    if (vblanks(5, 60.0, 1) != 1)
        return 21;
    khdays_clock_lock_screen(0); /* no window shows the game: off */
    if (vblanks(3, 60.0, 1) != 0 || khdays_clock_rate() != 1.0)
        return 22;
    return 0;
}

int main(void)
{
    khdays_clock_init();
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 1);
    u64 before = khdays_clock_cycles();
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
    khdays_clock_hold(KHDAYS_HOLD_MOVIE, 0);
    khdays_clock_wake();
    khdays_clock_sleep_until_or_woken(before + KHDAYS_CLOCK_HZ);
    if (khdays_clock_cycles() != before || !khdays_clock_held(KHDAYS_HOLD_SETTINGS))
        return 1;
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
    khdays_clock_sleep_until(before + KHDAYS_CLOCK_HZ / 100);
    if (khdays_clock_cycles() < before + KHDAYS_CLOCK_HZ / 100)
        return 2;
    return screen_lock();
}
