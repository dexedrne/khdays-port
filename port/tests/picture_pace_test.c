/* The phone's picture rate (host/render/picture_pace.h) on a made-up 120 Hz screen: it holds 120 while the
 * pictures keep their time, steps down to 60 when they miss it, back up after its hold when they are quick
 * again, waits longer after a step up that failed at once, follows the thermal state down at once (serious:
 * 60, critical: the game's own) and up only after its hold, and never goes below the game's own pictures. */
#include "../host/render/picture_pace.h"

#include <stdio.h>

#define MS 1000000ull
static const uint64_t vblank = 16715000; /* the DS's 59.83 Hz */
static uint64_t now;
static int failures;

static void check(int ok, const char *what, const KhdaysPace *p)
{
    printf("%s: %s (level %d at %.1f s; %s)\n", ok ? "passed" : "FAILED", what, p->level, (double)now / 1e9, p->why);
    failures += !ok;
}

/* `seconds` of play: as many pictures as the level draws, each taking `busy` ms; `late` pictures a second
 * due and not drawn. */
static int changes;
static void play(KhdaysPace *p, double seconds, double busy, unsigned late)
{
    const uint64_t end = now + (uint64_t)(seconds * 1e9);
    while (now < end) {
        const uint64_t budget = p->level == KHDAYS_PACE_SCREEN ? vblank / 2 : vblank;
        khdays_pace_picture(p, (uint64_t)(busy * 1e6), budget);
        now += budget;
        if (late != 0 && (now / budget) % (uint64_t)(p->level == KHDAYS_PACE_SCREEN ? 120 / late : 60 / late) == 0) {
            khdays_pace_late(p, 1);
        }
        if (khdays_pace_tick(p, now)) {
            ++changes;
            printf("  %.2f s: level %d: %s\n", (double)now / 1e9, p->level, p->why);
        }
    }
}

int main(void)
{
    KhdaysPace p;
    now = 1000 * MS;
    khdays_pace_init(&p, KHDAYS_PACE_SCREEN, KHDAYS_PACE_SCREEN, now);
    khdays_pace_screen(&p, 2, vblank);

    play(&p, 10, 3.0, 0);
    check(p.level == KHDAYS_PACE_SCREEN && changes == 0, "quick pictures keep 120", &p);

    play(&p, 1.5, 8.0, 0); /* over 90% of 8.36 ms */
    check(p.level == KHDAYS_PACE_VBLANK, "slow pictures drop to 60 within a second and a half", &p);
    {
        const uint64_t dropped = now;
        changes = 0;
        play(&p, 4.0, 3.0, 0);
        check(p.level == KHDAYS_PACE_VBLANK, "no step up before the 5 s hold", &p);
        play(&p, 4.0, 3.0, 0);
        check(p.level == KHDAYS_PACE_SCREEN && now - dropped >= KHDAYS_PACE_HOLD_NS, "back to 120 after the hold", &p);
    }
    play(&p, 1.5, 8.0, 0); /* straight down again: the hold doubles */
    check(p.level == KHDAYS_PACE_VBLANK && p.hold_ns == 2 * KHDAYS_PACE_HOLD_NS,
          "down again at once after a step up: the hold doubled to 10 s", &p);
    play(&p, 8.0, 3.0, 0);
    check(p.level == KHDAYS_PACE_VBLANK, "not up again before 10 s", &p);
    play(&p, 4.0, 3.0, 0);
    check(p.level == KHDAYS_PACE_SCREEN, "up after the doubled hold", &p);

    play(&p, 3.0, 5.5, 0); /* holds 120 (under 90%), no step */
    check(p.level == KHDAYS_PACE_SCREEN, "pictures at 65% of their time keep 120", &p);
    play(&p, 1.5, 3.0, 15); /* over a tenth of the due pictures never drawn */
    check(p.level == KHDAYS_PACE_VBLANK, "pictures due and not drawn (15 a second) drop to 60", &p);
    play(&p, 30.0, 5.5, 0); /* holds 60 but would not have 40% to spare at 120 */
    check(p.level == KHDAYS_PACE_VBLANK, "5.5 ms pictures stay at 60 (over 60% of 120's 8.36 ms)", &p);
    play(&p, 70.0, 3.0, 0);
    check(p.level == KHDAYS_PACE_SCREEN, "quick again: 120", &p);
    play(&p, 61.0, 3.0, 0);
    check(p.hold_ns == KHDAYS_PACE_HOLD_NS, "a minute without a step down: the hold back to 5 s", &p);

    khdays_pace_thermal(&p, KHDAYS_THERMAL_FAIR);
    play(&p, 3.0, 3.0, 0);
    check(p.level == KHDAYS_PACE_SCREEN, "thermal state fair: 120 kept", &p);
    khdays_pace_thermal(&p, KHDAYS_THERMAL_SERIOUS);
    play(&p, 0.02, 3.0, 0);
    check(p.level == KHDAYS_PACE_VBLANK, "thermal state serious: 60 at once", &p);
    play(&p, 20.0, 3.0, 0);
    check(p.level == KHDAYS_PACE_VBLANK, "no step up while serious", &p);
    khdays_pace_thermal(&p, KHDAYS_THERMAL_CRITICAL);
    play(&p, 0.05, 3.0, 0);
    check(p.level == KHDAYS_PACE_GAME, "critical: the game's own pictures", &p);
    play(&p, 5.0, 40.0, 0);
    check(p.level == KHDAYS_PACE_GAME, "never below the game's own, however slow", &p);
    khdays_pace_thermal(&p, KHDAYS_THERMAL_NOMINAL);
    {
        const uint64_t cooled = now;
        play(&p, 3.0, 3.0, 0);
        check(p.level == KHDAYS_PACE_GAME, "cooled down: still held", &p);
        play(&p, 20.0, 3.0, 0);
        check(p.level == KHDAYS_PACE_SCREEN, "cooled down: up step by step to 120", &p);
        printf("  (back at 120 %.1f s after the thermal state was nominal again)\n", (double)(now - cooled) / 1e9);
    }

    play(&p, 1.5, 20.0, 0);
    play(&p, 1.5, 20.0, 0);
    check(p.level == KHDAYS_PACE_GAME, "pictures over a VBlank: down to the game's own, one step a second", &p);

    khdays_pace_screen(&p, 1, vblank); /* a 60 Hz screen */
    khdays_pace_set_highest(&p, KHDAYS_PACE_VBLANK);
    play(&p, 40.0, 3.0, 0);
    check(p.level == KHDAYS_PACE_VBLANK, "a 60 Hz screen: 60 the most", &p);
    khdays_pace_screen(&p, 2, vblank);
    khdays_pace_set_highest(&p, KHDAYS_PACE_SCREEN);
    play(&p, 40.0, 3.0, 0);
    check(p.level == KHDAYS_PACE_SCREEN, "120 again once the screen allows it", &p);
    {
        const uint64_t hold = p.hold_ns;
        khdays_pace_set_highest(&p, KHDAYS_PACE_VBLANK);
        play(&p, 0.02, 3.0, 0);
        check(p.level == KHDAYS_PACE_VBLANK, "a lower most: down at once", &p);
        khdays_pace_set_highest(&p, KHDAYS_PACE_SCREEN);
        play(&p, 30.0, 3.0, 0);
        khdays_pace_thermal(&p, KHDAYS_THERMAL_SERIOUS); /* within 15 s of the step up */
        play(&p, 0.5, 3.0, 0);
        check(p.level == KHDAYS_PACE_VBLANK && p.hold_ns == hold,
              "down for the heat soon after a step up: the hold not doubled (only missed pictures double it)", &p);
    }

    printf(failures ? "%d FAILED\n" : "all passed\n", failures);
    return failures != 0;
}
