#include "../runtime/core/clock.h"
#include <SDL3/SDL.h>
#include <stdlib.h>
void khdays_hal_unimplemented(const char *reason)
{
    SDL_Log("%s", reason);
    abort();
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
    return khdays_clock_cycles() < before + KHDAYS_CLOCK_HZ / 100;
}
