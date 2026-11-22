#include "mobile_touch.h"
#include "profile.h"
#include "devices.h"
#include <stdio.h>

static SDL_FRect window_stylus;
static bool stylus_point(float x, float y, float *sx, float *sy)
{
    const SDL_FPoint p = {x, y};
    if (!SDL_PointInRectFloat(&p, &window_stylus))
        return false;
    *sx = (x - window_stylus.x) * 256 / window_stylus.w;
    *sy = (y - window_stylus.y) * 192 / window_stylus.h;
    return true;
}

#ifdef KHDAYS_MOBILE_TEST
static int test_insets[4], test_insets_set;
#endif

bool khdays_mobile_safe(SDL_Window *w, SDL_Rect *safe)
{
    if (!SDL_GetWindowSafeArea(w, safe))
        return false;
#ifdef KHDAYS_MOBILE_TEST
    const char *insets = SDL_getenv("KHDAYS_MOBILE_INSETS");
    int l, t, r, b;
    if (test_insets_set) {
        /* the margins a test's script set while the game runs (a turned phone's: pad_script.h "safe=") */
        safe->x += test_insets[0];
        safe->y += test_insets[1];
        safe->w -= test_insets[0] + test_insets[2];
        safe->h -= test_insets[1] + test_insets[3];
    } else if (insets != NULL && SDL_sscanf(insets, "%d,%d,%d,%d", &l, &t, &r, &b) == 4) {
        safe->x += l;
        safe->y += t;
        safe->w -= l + r;
        safe->h -= t + b;
    }
#endif
    return true;
}

bool khdays_mobile_visible(SDL_Window *w, SDL_FRect *px, float *corner)
{
    static float corner_pt = -1;
    SDL_Rect safe;
    int ww = 0, wh = 0, pw = 0, ph = 0;
    if (corner_pt < 0) {
        const char *v = SDL_getenv("KHDAYS_HUD_CORNER");
        corner_pt = v != NULL && *v ? (float)SDL_atof(v) : 12.0f;
        corner_pt = corner_pt < 0 ? 0 : corner_pt > 80 ? 80 : corner_pt;
    }
    if (w == NULL || !khdays_mobile_safe(w, &safe) || safe.w <= 0 || safe.h <= 0 || !SDL_GetWindowSize(w, &ww, &wh) ||
        !SDL_GetWindowSizeInPixels(w, &pw, &ph) || ww <= 0 || wh <= 0 || pw <= 0 || ph <= 0)
        return false;
    {
        const float dx = (float)pw / (float)ww, dy = (float)ph / (float)wh, c = corner_pt * dx;
        const float x0 = SDL_max((float)safe.x * dx, c), y0 = SDL_max((float)safe.y * dy, c);
        const float x1 = SDL_min((float)(safe.x + safe.w) * dx, (float)pw - c);
        const float y1 = SDL_min((float)(safe.y + safe.h) * dy, (float)ph - c);
        if (x1 <= x0 || y1 <= y0)
            return false;
        *px = (SDL_FRect){x0, y0, x1 - x0, y1 - y0};
        if (corner != NULL)
            *corner = c;
    }
    return true;
}

SDL_FRect khdays_mobile_visible_output(SDL_Renderer *renderer)
{
    SDL_FRect vis = {0, 0, 0, 0};
    int w = 0, h = 0;
    SDL_GetCurrentRenderOutputSize(renderer, &w, &h);
    if (!khdays_mobile_visible(SDL_GetRenderWindow(renderer), &vis, NULL) || vis.x + vis.w > (float)w + 0.5f ||
        vis.y + vis.h > (float)h + 0.5f)
        vis = (SDL_FRect){0, 0, (float)w, (float)h};
    return vis;
}

#define OVERLAYS 24
static struct {
    char what[48];
    SDL_FRect px;
} overlays[OVERLAYS];
static int overlay_count;

void khdays_mobile_overlay_clear(void)
{
    overlay_count = 0;
}

void khdays_mobile_overlay_box(const char *what, SDL_FRect px)
{
    if (overlay_count < OVERLAYS && px.w > 0 && px.h > 0) {
        SDL_strlcpy(overlays[overlay_count].what, what, sizeof(overlays[0].what));
        overlays[overlay_count++].px = px;
    }
}

int khdays_mobile_overlay_count(void)
{
    return overlay_count;
}

const char *khdays_mobile_overlay(int i, SDL_FRect *px)
{
    if (i < 0 || i >= overlay_count)
        return NULL;
    *px = overlays[i].px;
    return overlays[i].what;
}

#ifdef KHDAYS_MOBILE_TEST
void khdays_mobile_test_insets(int left, int top, int right, int bottom)
{
    test_insets[0] = left;
    test_insets[1] = top;
    test_insets[2] = right;
    test_insets[3] = bottom;
    test_insets_set = 1;
}
#endif

bool khdays_mobile_portrait(SDL_Window *w)
{
    SDL_Rect safe;
    return khdays_mobile_safe(w, &safe) && safe.w > 0 && safe.h > safe.w;
}

#ifdef KHDAYS_MOBILE_TEST
int khdays_ios_layout(void)
{
    const char *v = SDL_getenv("KHDAYS_MOBILE_LAYOUT");
    return v != NULL ? SDL_atoi(v) : 2; /* as the phone: single screen unless chosen otherwise */
}

/* The picture rate's phone (mobile_touch.h): a screen of KHDAYS_TEST_SCREEN_HZ, else the one KHDAYS_SCREEN_LOCK=HZ
 * pretends (runtime/core/clock_sdl.c), else not known; the thermal state of KHDAYS_TEST_THERMAL until the pad
 * script says another; the display's rate only said. */
static int test_thermal = -1, test_slow_ms;

int khdays_ios_screen_most_hz(void)
{
    const char *v = SDL_getenv("KHDAYS_TEST_SCREEN_HZ");
    if (v == NULL) {
        v = SDL_getenv("KHDAYS_SCREEN_LOCK");
    }
    return v != NULL ? SDL_atoi(v) : 0;
}

int khdays_ios_thermal_state(void)
{
    if (test_thermal < 0) {
        const char *v = SDL_getenv("KHDAYS_TEST_THERMAL");
        test_thermal = v != NULL ? SDL_clamp(SDL_atoi(v), 0, 3) : 0;
    }
    return test_thermal;
}

void khdays_ios_picture_rate(int hz)
{
    fprintf(stderr, "test: the display asked for %d pictures a second\n", hz);
}

void khdays_mobile_test_thermal(int state)
{
    test_thermal = SDL_clamp(state, 0, 3);
}

void khdays_mobile_test_slow(int ms)
{
    test_slow_ms = ms < 0 ? 0 : ms;
}

int khdays_mobile_test_slow_ms(void)
{
    return test_slow_ms;
}
#endif

/* Fit the logical picture inside both its letterbox and the unobscured window. */
static char view_note[160] = "safe-area fit not run";
const char *khdays_mobile_view_note(void) { return view_note; }

/* Portrait: the picture at the top of the safe area, at the largest size that fits it (as wide as the
 * safe area when it is no taller than it), in the middle of its width. The free space is below it. */
static void view_top(SDL_Renderer *renderer, SDL_Rect safe, int w, int h, float x0, float y0, float x1, float y1)
{
    /* A viewport is in whole units (the picture's own dots at this size), so its place is right to
     * within one of them: its top is put at the first whole unit inside the safe area -- never under the
     * notch -- and its height fitted with that unit to spare, so its foot is inside too. */
    const float fit = SDL_min((x1 - x0) / w, (y1 - y0 - 1) / h);
    float left, top;
    if (fit <= 0)
        return;
    SDL_Rect viewport = {(int)SDL_roundf(((x0 + x1) / fit - w) / 2), (int)SDL_ceilf(y0 / fit - 0.01f), w, h};
    SDL_SetRenderScale(renderer, fit, fit);
    SDL_SetRenderViewport(renderer, &viewport);
    /* never left of or above the window itself (the rounding, with no margin there) */
    if (SDL_RenderCoordinatesToWindow(renderer, 0, 0, &left, &top) && (left < 0 || top < 0)) {
        viewport.x += left < 0;
        viewport.y += top < 0;
        SDL_SetRenderViewport(renderer, &viewport);
    }
    SDL_snprintf(view_note, sizeof(view_note), "safe area %d,%d %dx%d: at its top, fit %.3f, viewport %d,%d %dx%d",
                 safe.x, safe.y, safe.w, safe.h, (double)fit, viewport.x, viewport.y, viewport.w, viewport.h);
}

void khdays_mobile_view(SDL_Renderer *renderer, SDL_Rect safe, int w, int h)
{
    khdays_mobile_view_at(renderer, safe, w, h, false);
}

void khdays_mobile_view_at(SDL_Renderer *renderer, SDL_Rect safe, int w, int h, bool top)
{
    float x0, x1, y0, y1;
    SDL_snprintf(view_note, sizeof(view_note), "safe area %d,%d %dx%d: no fit needed", safe.x, safe.y, safe.w, safe.h);
    SDL_SetRenderViewport(renderer, NULL);
    SDL_SetRenderScale(renderer, 1, 1);
    if (!SDL_RenderCoordinatesFromWindow(renderer, safe.x, safe.y, &x0, &y0) ||
        !SDL_RenderCoordinatesFromWindow(renderer, safe.x + safe.w, safe.y + safe.h, &x1, &y1))
        return;
    if (top) {
        view_top(renderer, safe, w, h, x0, y0, x1, y1);
        return;
    }
    x0 = SDL_max(0, x0);
    y0 = SDL_max(0, y0);
    x1 = SDL_min(w, x1);
    y1 = SDL_min(h, y1);
    float fit = SDL_min((x1 - x0) / w, (y1 - y0) / h);
    if (fit <= 0 || fit >= 1)
        return;
    SDL_Rect viewport = {(int)SDL_roundf(((x0 + x1) / fit - w) / 2), (int)SDL_roundf(((y0 + y1) / fit - h) / 2), w, h};
    SDL_SetRenderScale(renderer, fit, fit);
    SDL_SetRenderViewport(renderer, &viewport);
    SDL_snprintf(view_note, sizeof(view_note), "safe area %d,%d %dx%d: fit %.3f, viewport %d,%d %dx%d", safe.x, safe.y,
                 safe.w, safe.h, (double)fit, viewport.x, viewport.y, viewport.w, viewport.h);
}

void khdays_mobile_stylus_window(SDL_FRect *at)
{
    *at = window_stylus;
}

void khdays_mobile_stylus_rect(SDL_Renderer *renderer, SDL_FRect stylus_rect)
{
    SDL_zero(window_stylus);
    if (stylus_rect.w > 0 && stylus_rect.h > 0) {
        float right, bottom;
        SDL_RenderCoordinatesToWindow(renderer, stylus_rect.x, stylus_rect.y, &window_stylus.x, &window_stylus.y);
        SDL_RenderCoordinatesToWindow(renderer, stylus_rect.x + stylus_rect.w, stylus_rect.y + stylus_rect.h, &right,
                                      &bottom);
        window_stylus.w = right - window_stylus.x;
        window_stylus.h = bottom - window_stylus.y;
    }
}

enum { VIEW_KEY = -1 };
static struct Button {
    const char *label;
    int action;
    float x, y;
} const buttons[] = {{"A", KHDAYS_ACT_A, 0, 1},         {"B", KHDAYS_ACT_B, 1, 0},          {"X", KHDAYS_ACT_X, 1, 2},
                     {"Y", KHDAYS_ACT_Y, 2, 1},         {"R", KHDAYS_ACT_R, 0, 3},          {"L", KHDAYS_ACT_L, -1, 3},
                     {"Start", KHDAYS_ACT_START, 3, 0}, {"Select", KHDAYS_ACT_SELECT, 4, 0},
                     /* the lower-screen view of one screen, shown and put away (no DS button) */
                     {"Screen", VIEW_KEY, 5, 0}};
enum { STICK = -2, STYLUS = -1 };
static struct Finger {
    bool held;
    SDL_FingerID id;
    int control;
    float x, y, home_x, home_y;
    bool rehome; /* the stick's: the window was laid out anew, its home follows the finger's next place */
} fingers[10];
static float size;

static SDL_FRect button_rect(const SDL_Rect *safe, int i)
{
    float x = buttons[i].x < 0 ? safe->x : safe->x + safe->w - (buttons[i].x + 1) * size;
    return (SDL_FRect){x + 2, safe->y + safe->h - (buttons[i].y + 1) * size + 2, size - 4, size - 4};
}

static bool geometry(SDL_Window *w, SDL_Rect *safe)
{
    if (!khdays_mobile_safe(w, safe) || safe->w <= 0 || safe->h <= 0)
        return false;
    size = SDL_min(55, SDL_min(safe->w / 8.f, safe->h / 7.f));
    return true;
}

/* The stick's zone: where a finger that comes down on no key is the stick. */
static SDL_FRect stick_zone(const SDL_Rect *safe)
{
    return (SDL_FRect){(float)safe->x, safe->y + safe->h - 2.8f * size, 2.8f * size, 2.8f * size};
}

float khdays_mobile_keys_top(SDL_Window *w)
{
    SDL_Rect safe;
    float top;
    if (!geometry(w, &safe))
        return -1;
    top = stick_zone(&safe).y;
    for (int i = 0; i < (int)SDL_arraysize(buttons); ++i)
        top = SDL_min(top, safe.y + safe.h - (buttons[i].y + 1) * size); /* (the key's whole cell) */
    return top;
}

static void draw_key(SDL_Renderer *r, SDL_FRect rect, const char *label)
{
    /* in the window's pixels (the logical presentation is off): a third of a point each on the phone,
     * so the outline and the label are scaled to the key, not left a pixel wide and 8 pixels high */
    SDL_FRect at;
    float end_x, end_y;
    SDL_RenderCoordinatesFromWindow(r, rect.x, rect.y, &at.x, &at.y);
    SDL_RenderCoordinatesFromWindow(r, rect.x + rect.w, rect.y + rect.h, &end_x, &end_y);
    at.w = end_x - at.x;
    at.h = end_y - at.y;
    const float length = (float)SDL_strlen(label);
    const float text = SDL_max(1.0f, SDL_floorf(SDL_min(at.h / 22.0f, at.w / (8.0f * length + 4.0f))));
    const int line = (int)SDL_max(1.0f, SDL_roundf(at.h / 40.0f));
    SDL_SetRenderDrawColor(r, 255, 255, 255, 34);
    SDL_RenderFillRect(r, &at);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 170);
    for (int i = 0; i < line; ++i) {
        const SDL_FRect edge = {at.x + (float)i, at.y + (float)i, at.w - 2.0f * (float)i, at.h - 2.0f * (float)i};
        SDL_RenderRect(r, &edge);
    }
    SDL_SetRenderDrawColor(r, 255, 255, 255, 220);
    SDL_SetRenderScale(r, text, text);
    SDL_RenderDebugText(r, (at.x + (at.w - 8.0f * length * text) / 2.0f) / text,
                        (at.y + (at.h - 8.0f * text) / 2.0f) / text, label);
    SDL_SetRenderScale(r, 1.0f, 1.0f);
}

void khdays_mobile_draw(SDL_Renderer *r)
{
    SDL_Rect safe;
    if (!geometry(SDL_GetRenderWindow(r), &safe))
        return;
    int lw, lh;
    SDL_RendererLogicalPresentation presentation;
    SDL_GetRenderLogicalPresentation(r, &lw, &lh, &presentation);
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_BlendMode blend;
    Uint8 red, green, blue, alpha;
    SDL_GetRenderDrawColor(r, &red, &green, &blue, &alpha);
    SDL_GetRenderDrawBlendMode(r, &blend);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < (int)SDL_arraysize(buttons); ++i)
        draw_key(r, button_rect(&safe, i), buttons[i].label);
    SDL_FRect stick = {safe.x + size / 2, safe.y + safe.h - 2 * size, size * 1.5f, size * 1.5f};
    for (int i = 0; i < 10; ++i) {
        if (fingers[i].held && fingers[i].control == STICK && !fingers[i].rehome) {
            stick.x = fingers[i].home_x - stick.w / 2;
            stick.y = fingers[i].home_y - stick.h / 2;
        }
    }
    draw_key(r, stick, "Move");
    SDL_SetRenderDrawBlendMode(r, blend);
    SDL_SetRenderDrawColor(r, red, green, blue, alpha);
    SDL_SetRenderLogicalPresentation(r, lw, lh, presentation);
}

void khdays_mobile_reset(void)
{
    SDL_zero(fingers);
}

void khdays_mobile_relaid(void)
{
    for (int i = 0; i < 10; ++i) {
        struct Finger *f = &fingers[i];
        if (!f->held)
            continue;
        if (f->control == STYLUS) {
            f->held = false; /* what was under it has moved */
        } else if (f->control == STICK && !f->rehome) {
            /* the direction it holds stays held: kept as the finger's offset from a home that is found
             * again at the finger's next place, which comes in the new window's coordinates */
            f->x -= f->home_x;
            f->y -= f->home_y;
            f->home_x = f->home_y = 0;
            f->rehome = true;
        }
    }
}

void khdays_mobile_finger(const SDL_Event *e, SDL_Renderer *r, bool controller)
{
    int w, h;
    SDL_Rect safe;
    SDL_Window *window = SDL_GetRenderWindow(r);
    if (!SDL_GetWindowSize(window, &w, &h) || !geometry(window, &safe))
        return;
    SDL_FPoint p = {e->tfinger.x * w, e->tfinger.y * h};
    struct Finger *f = NULL;
    for (int i = 0; i < 10; ++i) {
        if (fingers[i].held && fingers[i].id == e->tfinger.fingerID) {
            f = &fingers[i];
            break;
        }
        if (!f && !fingers[i].held)
            f = &fingers[i];
    }
    if (!f)
        return;
    if (e->type == SDL_EVENT_FINGER_DOWN) {
        int control = STYLUS;
        if (!controller) {
            for (int i = 0; i < (int)SDL_arraysize(buttons); ++i) {
                SDL_FRect rect = button_rect(&safe, i);
                if (SDL_PointInRectFloat(&p, &rect)) {
                    control = i;
                    break;
                }
            }
            const SDL_FRect zone = stick_zone(&safe);
            if (control == STYLUS && SDL_PointInRectFloat(&p, &zone))
                control = STICK;
        }
        *f = (struct Finger){true, e->tfinger.fingerID, control, p.x, p.y, p.x, p.y};
        if (control >= 0 && buttons[control].action == VIEW_KEY) {
            khdays_devices_request_lower_view();
        }
    } else if (!f->held || f->id != e->tfinger.fingerID)
        return;
    if (e->type == SDL_EVENT_FINGER_UP || e->type == SDL_EVENT_FINGER_CANCELED) {
        f->held = false;
        return;
    }
    if (f->rehome) {
        f->home_x = p.x - f->x;
        f->home_y = p.y - f->y;
        f->rehome = false;
    }
    f->x = p.x;
    f->y = p.y;
    if (f->control == STYLUS && !stylus_point(p.x, p.y, &f->x, &f->y))
        f->held = false;
    if (f->control == STYLUS && e->type == SDL_EVENT_FINGER_DOWN) {
        /* where a finger lands on the DS's lower screen (the first 40): the phone's console and the Linux test
         * check the stylus mapping through every layout, the filled picture's crop included */
        static int said;
        if (said < 40) {
            ++said;
            if (f->held) {
                SDL_Log("touch: a finger at %.0f,%.0f (window points) is the stylus at DS %.1f,%.1f (lower screen at "
                        "%.0f,%.0f %.0fx%.0f)",
                        (double)p.x, (double)p.y, (double)f->x, (double)f->y, (double)window_stylus.x,
                        (double)window_stylus.y, (double)window_stylus.w, (double)window_stylus.h);
            } else {
                SDL_Log("touch: a finger at %.0f,%.0f (window points) is off the lower screen", (double)p.x, (double)p.y);
            }
        }
    }
}

void khdays_mobile_snapshot(KhdaysInputSnapshot *out)
{
    unsigned bits = 0;
    for (int i = 0; i < 10; ++i) {
        const struct Finger *f = &fingers[i];
        if (!f->held)
            continue;
        if (f->control >= 0) {
            if (buttons[f->control].action >= 0) bits |= 1u << buttons[f->control].action;
        }
        else if (f->control == STYLUS) {
            out->touching = 1;
            out->touch_x = (uint16_t)f->x;
            out->touch_y = (uint16_t)f->y;
        } else {
            float dx = f->x - f->home_x, dy = f->y - f->home_y;
            if (SDL_fabsf(dx) > size * .18f)
                bits |= 1u << (dx > 0 ? KHDAYS_ACT_RIGHT : KHDAYS_ACT_LEFT);
            if (SDL_fabsf(dy) > size * .18f)
                bits |= 1u << (dy > 0 ? KHDAYS_ACT_DOWN : KHDAYS_ACT_UP);
        }
    }
    out->ds_buttons |= bits;
    out->ds_buttons_field |= bits;
}
