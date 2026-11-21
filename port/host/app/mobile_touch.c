#include "mobile_touch.h"
#include "profile.h"

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

bool khdays_mobile_safe(SDL_Window *w, SDL_Rect *safe)
{
    if (!SDL_GetWindowSafeArea(w, safe))
        return false;
#ifdef KHDAYS_MOBILE_TEST
    const char *insets = SDL_getenv("KHDAYS_MOBILE_INSETS");
    int l, t, r, b;
    if (insets != NULL && SDL_sscanf(insets, "%d,%d,%d,%d", &l, &t, &r, &b) == 4) {
        safe->x += l;
        safe->y += t;
        safe->w -= l + r;
        safe->h -= t + b;
    }
#endif
    return true;
}

#ifdef KHDAYS_MOBILE_TEST
int khdays_ios_layout(void)
{
    const char *v = SDL_getenv("KHDAYS_MOBILE_LAYOUT");
    return v != NULL ? SDL_atoi(v) : 2; /* as the phone: single screen unless chosen otherwise */
}
#endif

/* Fit the logical picture inside both its letterbox and the unobscured window. */
static char view_note[160] = "safe-area fit not run";
const char *khdays_mobile_view_note(void) { return view_note; }

void khdays_mobile_view(SDL_Renderer *renderer, SDL_Rect safe, int w, int h)
{
    float x0, x1, y0, y1;
    SDL_snprintf(view_note, sizeof(view_note), "safe area %d,%d %dx%d: no fit needed", safe.x, safe.y, safe.w, safe.h);
    SDL_SetRenderViewport(renderer, NULL);
    SDL_SetRenderScale(renderer, 1, 1);
    if (!SDL_RenderCoordinatesFromWindow(renderer, safe.x, safe.y, &x0, &y0) ||
        !SDL_RenderCoordinatesFromWindow(renderer, safe.x + safe.w, safe.y + safe.h, &x1, &y1))
        return;
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

static struct Button {
    const char *label;
    int action;
    float x, y;
} const buttons[] = {{"A", KHDAYS_ACT_A, 0, 1},         {"B", KHDAYS_ACT_B, 1, 0},          {"X", KHDAYS_ACT_X, 1, 2},
                     {"Y", KHDAYS_ACT_Y, 2, 1},         {"R", KHDAYS_ACT_R, 0, 3},          {"L", KHDAYS_ACT_L, -1, 3},
                     {"Start", KHDAYS_ACT_START, 3, 0}, {"Select", KHDAYS_ACT_SELECT, 4, 0}};
enum { STICK = -2, STYLUS = -1 };
static struct Finger {
    bool held;
    SDL_FingerID id;
    int control;
    float x, y, home_x, home_y;
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
        if (fingers[i].held && fingers[i].control == STICK) {
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
            if (control == STYLUS && p.x >= safe.x && p.x < safe.x + 2.8f * size &&
                p.y >= safe.y + safe.h - 2.8f * size && p.y < safe.y + safe.h)
                control = STICK;
        }
        *f = (struct Finger){true, e->tfinger.fingerID, control, p.x, p.y, p.x, p.y};
    } else if (!f->held || f->id != e->tfinger.fingerID)
        return;
    if (e->type == SDL_EVENT_FINGER_UP || e->type == SDL_EVENT_FINGER_CANCELED) {
        f->held = false;
        return;
    }
    f->x = p.x;
    f->y = p.y;
    if (f->control == STYLUS && !stylus_point(p.x, p.y, &f->x, &f->y))
        f->held = false;
}

void khdays_mobile_snapshot(KhdaysInputSnapshot *out)
{
    unsigned bits = 0;
    for (int i = 0; i < 10; ++i) {
        const struct Finger *f = &fingers[i];
        if (!f->held)
            continue;
        if (f->control >= 0)
            bits |= 1u << buttons[f->control].action;
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
