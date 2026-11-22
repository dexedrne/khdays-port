/* The phone's picture rate: the rule of picture_pace.h. */
#include "picture_pace.h"

#include <stdio.h>
#include <string.h>

static const char *const thermal_names[] = {"nominal", "fair", "serious", "critical"};

const char *khdays_thermal_name(int state)
{
    return state >= 0 && state <= KHDAYS_THERMAL_CRITICAL ? thermal_names[state] : "unknown";
}

const char *khdays_pace_name(int level, int refreshes)
{
    if (level == KHDAYS_PACE_GAME) {
        return "the game's own (its 3D at 30 or 60 a second)";
    }
    if (level == KHDAYS_PACE_VBLANK || refreshes < 2) {
        return "60 a second";
    }
    return refreshes == 2 ? "120 a second" : refreshes == 4 ? "240 a second" : "the screen's refreshes";
}

static void clear_second(KhdaysPace *p, uint64_t now)
{
    p->second_at = now;
    p->pictures = p->missed = p->late = 0;
    p->busy_total = p->busy_most = 0;
}

void khdays_pace_init(KhdaysPace *p, int level, int highest, uint64_t now)
{
    memset(p, 0, sizeof(*p));
    p->highest = highest < KHDAYS_PACE_GAME ? KHDAYS_PACE_GAME : highest >= KHDAYS_PACE_LEVELS ? KHDAYS_PACE_SCREEN : highest;
    p->level = level > p->highest ? p->highest : level < KHDAYS_PACE_GAME ? KHDAYS_PACE_GAME : level;
    p->refreshes = 1;
    p->vblank_ns = 16715000; /* the DS's 59.83 Hz until told */
    p->hold_ns = KHDAYS_PACE_HOLD_NS;
    p->changed_at = p->calm_since = p->thermal_at = now;
    clear_second(p, now);
    snprintf(p->why, sizeof(p->why), "the start");
}

void khdays_pace_screen(KhdaysPace *p, int refreshes, uint64_t vblank_ns)
{
    p->refreshes = refreshes < 1 ? 1 : refreshes;
    if (vblank_ns > 0) {
        p->vblank_ns = vblank_ns;
    }
}

void khdays_pace_set_highest(KhdaysPace *p, int highest)
{
    p->highest = highest < KHDAYS_PACE_GAME ? KHDAYS_PACE_GAME : highest >= KHDAYS_PACE_LEVELS ? KHDAYS_PACE_SCREEN : highest;
}

void khdays_pace_picture(KhdaysPace *p, uint64_t busy_ns, uint64_t budget_ns)
{
    ++p->pictures;
    p->busy_total += busy_ns;
    p->busy_most = busy_ns > p->busy_most ? busy_ns : p->busy_most;
    p->budget = budget_ns;
    if (budget_ns > 0 && busy_ns * 10 > budget_ns * 9) {
        ++p->missed;
    }
}

void khdays_pace_late(KhdaysPace *p, unsigned n)
{
    p->late += n;
}

void khdays_pace_thermal(KhdaysPace *p, int state)
{
    p->thermal = state;
}

/* The most the thermal state allows. */
static int thermal_cap(const KhdaysPace *p)
{
    return p->thermal >= KHDAYS_THERMAL_CRITICAL  ? KHDAYS_PACE_GAME
           : p->thermal >= KHDAYS_THERMAL_SERIOUS ? KHDAYS_PACE_VBLANK
                                                  : KHDAYS_PACE_SCREEN;
}

/* The time a picture has at `level`. */
static uint64_t budget_at(const KhdaysPace *p, int level)
{
    return level == KHDAYS_PACE_SCREEN && p->refreshes > 1 ? p->vblank_ns / (uint64_t)p->refreshes : p->vblank_ns;
}

/* `missed`: down for pictures that missed their time (not for the heat or a lower most) */
static void set_level(KhdaysPace *p, int level, uint64_t now, int missed)
{
    if (level > p->level) {
        p->raised_at = now;
    } else if (missed && p->raised_at != 0 && now - p->raised_at < 15 * KHDAYS_PACE_SECOND_NS) {
        /* up and straight down again, the pictures too slow for it: wait longer before the next try */
        p->hold_ns = p->hold_ns * 2 > KHDAYS_PACE_HOLD_MOST_NS ? KHDAYS_PACE_HOLD_MOST_NS : p->hold_ns * 2;
        p->raised_at = 0;
    }
    if (level < p->level) {
        p->calm_since = now;
    }
    p->level = level;
    p->changed_at = now;
    p->good_seconds = 0;
}

int khdays_pace_tick(KhdaysPace *p, uint64_t now)
{
    const int was = p->level;
    int cap = thermal_cap(p);
    if (p->thermal != p->thermal_seen) {
        p->thermal_seen = p->thermal;
        p->thermal_at = now; /* (cooler: up only after the hold from here too) */
    }
    cap = cap > p->highest ? p->highest : cap;
    if (p->level > cap) {
        /* at once, not at the second's end */
        set_level(p, cap, now, 0);
        if (cap < p->highest && cap == thermal_cap(p)) {
            snprintf(p->why, sizeof(p->why), "the thermal state is %s: at most %s", khdays_thermal_name(p->thermal),
                     khdays_pace_name(cap, p->refreshes));
        } else {
            snprintf(p->why, sizeof(p->why), "%s is the most allowed now", khdays_pace_name(cap, p->refreshes));
        }
        clear_second(p, now);
        return 1;
    }
    if (now - p->second_at < KHDAYS_PACE_SECOND_NS) {
        return 0;
    }
    {
        const unsigned due = p->pictures + p->late, bad = p->missed + p->late;
        const double busy_ms = p->pictures ? (double)p->busy_total / p->pictures / 1e6 : 0.0;
        p->last_pictures = p->pictures;
        p->last_missed = p->missed;
        p->last_late = p->late;
        p->last_busy_ms = busy_ms;
        p->last_most_ms = (double)p->busy_most / 1e6;
        if (bad >= 3 && bad * 10 >= due && p->level > KHDAYS_PACE_GAME) {
            set_level(p, p->level - 1, now, 1);
            snprintf(p->why, sizeof(p->why),
                     "%u of %u pictures in the last second missed their time (%u took over 90%% of %.2f ms, %u came "
                     "late or never; %.2f ms each on average, %.2f at most)",
                     bad, due, p->missed, (double)p->budget / 1e6, p->late, busy_ms, p->last_most_ms);
        } else if (p->level < cap && bad == 0 && p->pictures > 0) {
            const uint64_t next = budget_at(p, p->level + 1);
            if (p->busy_total < (uint64_t)p->pictures * next * 6 / 10) {
                ++p->good_seconds;
            } else {
                p->good_seconds = 0;
            }
            if (p->good_seconds >= 2 && now - p->changed_at >= p->hold_ns && now - p->thermal_at >= p->hold_ns) {
                set_level(p, p->level + 1, now, 0);
                snprintf(p->why, sizeof(p->why),
                         "2 seconds with no picture missed, each made in %.2f ms on average (under 60%% of the %.2f "
                         "ms a picture has at %s), the thermal state %s",
                         busy_ms, (double)next / 1e6, khdays_pace_name(p->level, p->refreshes),
                         khdays_thermal_name(p->thermal));
            }
        } else {
            p->good_seconds = 0;
        }
        if (now - p->calm_since >= 60 * KHDAYS_PACE_SECOND_NS) {
            p->hold_ns = KHDAYS_PACE_HOLD_NS; /* a minute without a step down: quick to try again */
        }
        clear_second(p, now);
    }
    return p->level != was;
}
