#include "clock.h"
#include <SDL3/SDL.h>
#include <limits.h>

static SDL_Mutex *lock;
static SDL_Condition *changed;
static Uint64 origin, paused_at, paused_total;
static unsigned reasons;
static bool wake_pending;

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

static u64 cycles_locked(void)
{
    Uint64 ns = (reasons ? paused_at : SDL_GetTicksNS()) - origin - paused_total;
    return ns / 1000000000 * KHDAYS_CLOCK_HZ + ns % 1000000000 * KHDAYS_CLOCK_HZ / 1000000000;
}

u64 khdays_clock_cycles(void)
{
    SDL_LockMutex(lock);
    u64 now = cycles_locked();
    SDL_UnlockMutex(lock);
    return now;
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
