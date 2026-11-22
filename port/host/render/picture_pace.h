/* The phone's picture rate (picture_pace = smooth): how many pictures a second the window draws, chosen as the
 * game plays and changed when the pictures stop holding their time or the phone grows hot. The game itself
 * keeps the DS's rate whatever is chosen here (its time is the clock's, runtime/core/clock.h); only the
 * pictures drawn of it change.
 *
 *   KHDAYS_PACE_SCREEN  a picture at each of the screen's refreshes (120 a second on a 120 Hz screen): the
 *                       3D placed between the game's frames (gpu_present.c picture_list)
 *   KHDAYS_PACE_VBLANK  one a VBlank (60 a second), the 3D placed between the game's frames as at 60 Hz
 *   KHDAYS_PACE_GAME    the game's own pictures (as picture_pace = ds: each of its frames drawn as it comes,
 *                       the 3D at the game's 30 or 60 a second): never fewer
 *
 * The rule (khdays_pace_tick, once a second over the second gone by):
 *   - down one step when a tenth of the pictures (and at least 3) missed their time: a picture whose window
 *     thread's work (made, drawn, presented) took over 90% of the time it had (a refresh at SCREEN, a VBlank
 *     below), or one due that came late or never;
 *   - down to VBLANK at a thermal state "serious", to GAME at "critical" (iOS's NSProcessInfo.thermalState);
 *   - up one step only after a hold (5 s after the last change and after the thermal state last changed, doubled each time a step up is followed by a
 *     step down for missed pictures within 15 s, up to 120 s; back to 5 s after 60 s without a step down), while the thermal state
 *     allows it, and after 2 seconds in a row with no picture missed and the work done in under 60% of the
 *     time the step up would give a picture.
 * Pure arithmetic on the times given: host/render/gpu_present.c feeds it, port/tests/picture_pace_test.c
 * drives it. */
#ifndef KHDAYS_PICTURE_PACE_H
#define KHDAYS_PICTURE_PACE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { KHDAYS_PACE_GAME, KHDAYS_PACE_VBLANK, KHDAYS_PACE_SCREEN, KHDAYS_PACE_LEVELS };

/* iOS's thermal states (NSProcessInfoThermalState) */
enum { KHDAYS_THERMAL_NOMINAL, KHDAYS_THERMAL_FAIR, KHDAYS_THERMAL_SERIOUS, KHDAYS_THERMAL_CRITICAL };

#define KHDAYS_PACE_HOLD_NS 5000000000ull      /* the least wait before a step up */
#define KHDAYS_PACE_HOLD_MOST_NS 120000000000ull
#define KHDAYS_PACE_SECOND_NS 1000000000ull

typedef struct KhdaysPace {
    int level;   /* KHDAYS_PACE_* now */
    int highest; /* the most the screen and the settings allow */
    int thermal; /* KHDAYS_THERMAL_* last told */
    int thermal_seen;    /* the one the last tick saw, and since when */
    uint64_t thermal_at;
    /* the second being counted */
    uint64_t second_at;
    unsigned pictures, missed, late;
    uint64_t busy_total, busy_most, budget; /* (budget: the time a picture had at the level, the last told) */
    uint64_t vblank_ns;                     /* a VBlank, for the step up's budget */
    int refreshes;                          /* the screen's refreshes a VBlank (1 at 60 Hz, 2 at 120) */
    unsigned good_seconds;
    uint64_t changed_at, raised_at, hold_ns, calm_since;
    /* what the last second held, for the console */
    unsigned last_pictures, last_missed, last_late;
    double last_busy_ms, last_most_ms;
    char why[200];
} KhdaysPace;

/* Starts at `level`, at most `highest`. `now`: any steady nanosecond clock. */
void khdays_pace_init(KhdaysPace *p, int level, int highest, uint64_t now);

/* The screen now: its refreshes a VBlank and a VBlank's length (the step up's budget, and SCREEN's). */
void khdays_pace_screen(KhdaysPace *p, int refreshes, uint64_t vblank_ns);

/* The most allowed (the screen's most, a setting): the level comes down at once when above it. */
void khdays_pace_set_highest(KhdaysPace *p, int highest);

/* One picture drawn: the window thread's time for it and the time it had. */
void khdays_pace_picture(KhdaysPace *p, uint64_t busy_ns, uint64_t budget_ns);

/* Pictures due that came late or were never drawn. */
void khdays_pace_late(KhdaysPace *p, unsigned n);

/* The thermal state (KHDAYS_THERMAL_*). */
void khdays_pace_thermal(KhdaysPace *p, int state);

/* Called often (each picture is enough): 1 when the level changed, the reason in p->why. */
int khdays_pace_tick(KhdaysPace *p, uint64_t now);

/* The level's name for the console: "120", "60", "the game's own". `refreshes`: the screen's a VBlank. */
const char *khdays_pace_name(int level, int refreshes);

/* The thermal state's name. */
const char *khdays_thermal_name(int state);

#ifdef __cplusplus
}
#endif

#endif
