#include "../host/app/mobile_touch.h"
#include "../host/app/profile.h"
#include <stdlib.h>

static SDL_Renderer *renderer;
/* the Screen key's request for the lower-screen view (host/app/devices.c in the game) */
static int view_requests;
void khdays_devices_request_lower_view(void) { ++view_requests; }
static unsigned finger(SDL_EventType type, SDL_FingerID id, float x, float y, bool pad)
{
    SDL_Event e = {0};
    e.type = type;
    e.tfinger.fingerID = id;
    e.tfinger.x = x / 512;
    e.tfinger.y = y / 384;
    khdays_mobile_finger(&e, renderer, !pad);
    KhdaysInputSnapshot snapshot = {0};
    khdays_mobile_snapshot(&snapshot);
    return snapshot.ds_buttons;
}
int main(void)
{
    SDL_Window *window;
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_VIDEO) || !SDL_CreateWindowAndRenderer("test", 512, 384, 0, &window, &renderer))
        return 1;
    SDL_SetRenderLogicalPresentation(renderer, 256, 384, SDL_LOGICAL_PRESENTATION_STRETCH);
    khdays_mobile_stylus_rect(renderer, (SDL_FRect){0, 192, 256, 192});
    khdays_mobile_draw(renderer);
    if (!khdays_mobile_settings_at(window, 82, 192) || khdays_mobile_settings_at(window, 486, 302)) return 1;
    unsigned a = 1u << KHDAYS_ACT_A, right = 1u << KHDAYS_ACT_RIGHT;
    if (finger(SDL_EVENT_FINGER_DOWN, 1, 486, 302, false) != a ||
        finger(SDL_EVENT_FINGER_DOWN, 2, 35, 310, false) != a ||
        finger(SDL_EVENT_FINGER_MOTION, 2, 75, 310, false) != (a | right) ||
        finger(SDL_EVENT_FINGER_CANCELED, 1, 486, 302, false) != right)
        return 1;
    khdays_mobile_reset();
    /* the Screen key (sixth from the right in the bottom row): no DS button, one request for the view
     * at the press, none while held or at the lift */
    if (view_requests != 0 || finger(SDL_EVENT_FINGER_DOWN, 4, 210, 356, false) != 0 || view_requests != 1 ||
        finger(SDL_EVENT_FINGER_MOTION, 4, 212, 356, false) != 0 || finger(SDL_EVENT_FINGER_UP, 4, 212, 356, false) != 0 ||
        view_requests != 1)
        return 1;
    khdays_mobile_reset();
    if (finger(SDL_EVENT_FINGER_DOWN, 3, 120, 210, true))
        return 1;
    KhdaysInputSnapshot snapshot = {0};
    khdays_mobile_snapshot(&snapshot);
    if (!snapshot.touching || snapshot.touch_x != 60 || snapshot.touch_y != 18)
        return 1;
    khdays_mobile_reset();
    /* a turned phone (khdays_mobile_relaid): a held key stays held and the stick keeps its direction,
     * also when its finger next reports from a different place (the turned window's coordinates); a
     * finger that was the stylus is let go */
    if (finger(SDL_EVENT_FINGER_DOWN, 1, 486, 302, false) != a ||
        finger(SDL_EVENT_FINGER_DOWN, 2, 35, 310, false) != a ||
        finger(SDL_EVENT_FINGER_MOTION, 2, 75, 310, false) != (a | right) ||
        finger(SDL_EVENT_FINGER_DOWN, 3, 120, 210, false) != (a | right))
        return 1;
    SDL_zero(snapshot);
    khdays_mobile_snapshot(&snapshot);
    if (!snapshot.touching)
        return 1;
    khdays_mobile_relaid();
    khdays_mobile_relaid(); /* (a turn comes as several events) */
    SDL_zero(snapshot);
    khdays_mobile_snapshot(&snapshot);
    if (snapshot.ds_buttons != (a | right) || snapshot.touching)
        return 1;
    if (finger(SDL_EVENT_FINGER_MOTION, 2, 300, 40, false) != (a | right) ||
        finger(SDL_EVENT_FINGER_MOTION, 2, 300 - 40, 40, false) != a ||
        finger(SDL_EVENT_FINGER_UP, 2, 260, 40, false) != a || finger(SDL_EVENT_FINGER_UP, 1, 486, 302, false) != 0)
        return 1;
    khdays_mobile_reset();
    /* the top of what a finger plays with (khdays_mobile_keys_top): above it, across the whole width, a
     * finger is no key and not the stick (it is the stylus where the lower screen is); below it there
     * are keys */
    {
        const float keys_top = khdays_mobile_keys_top(window);
        SDL_Rect safe;
        int pressed_below = 0;
        if (!khdays_mobile_safe(window, &safe) || keys_top <= safe.y || keys_top >= safe.y + safe.h)
            return 1;
        khdays_mobile_stylus_rect(renderer, (SDL_FRect){0, 0, 256, 384}); /* the whole window is the stylus's */
        for (float x = 1; x < 512; x += 3) {
            KhdaysInputSnapshot above = {0};
            if (finger(SDL_EVENT_FINGER_DOWN, 7, x, keys_top - 1, false) != 0)
                return 1;
            khdays_mobile_snapshot(&above);
            if (!above.touching)
                return 1;
            finger(SDL_EVENT_FINGER_UP, 7, x, keys_top - 1, false);
            for (float y = keys_top + 3; y < safe.y + safe.h; y += 9) {
                KhdaysInputSnapshot below = {0};
                pressed_below |= finger(SDL_EVENT_FINGER_DOWN, 8, x, y, false) != 0;
                khdays_mobile_snapshot(&below);
                finger(SDL_EVENT_FINGER_UP, 8, x, y, false);
            }
        }
        if (!pressed_below)
            return 1;
        khdays_mobile_stylus_rect(renderer, (SDL_FRect){0, 192, 256, 192});
    }
    khdays_mobile_reset();
    const int screens[][2] = {{2853, 2007}, {2007, 2853}, {393, 852}, {1320, 2868}, {2868, 1320}};
    for (size_t i = 0; i < SDL_arraysize(screens); ++i) {
        int w = screens[i][0], h = screens[i][1];
        if (!SDL_SetWindowSize(window, w, h))
            return 1;
        SDL_PumpEvents();
        SDL_Rect safe = {43, 25, w - 55, h - 50};
        for (int side = 0; side < 2; ++side) {
            int lw = side ? 512 : 256, lh = side ? 192 : 384;
            SDL_SetRenderLogicalPresentation(renderer, lw, lh, SDL_LOGICAL_PRESENTATION_LETTERBOX);
            khdays_mobile_view(renderer, safe, lw, lh);
            float left, top, right_edge, bottom;
            SDL_RenderCoordinatesToWindow(renderer, 0, 0, &left, &top);
            SDL_RenderCoordinatesToWindow(renderer, lw, lh, &right_edge, &bottom);
            float pixel = SDL_max((right_edge - left) / lw, (bottom - top) / lh);
            if (left < safe.x - pixel || top < safe.y - pixel || right_edge > safe.x + safe.w + pixel ||
                bottom > safe.y + safe.h + pixel || right_edge <= left || bottom <= top)
                return 1;
            /* upright (khdays_mobile_view_at, `top`): at the safe area's top, never above it, and either
             * as wide as the safe area or as tall */
            khdays_mobile_view_at(renderer, safe, lw, lh, true);
            SDL_RenderCoordinatesToWindow(renderer, 0, 0, &left, &top);
            SDL_RenderCoordinatesToWindow(renderer, lw, lh, &right_edge, &bottom);
            pixel = SDL_max((right_edge - left) / lw, (bottom - top) / lh);
            if (top < safe.y - 0.01f || top > safe.y + pixel || left < safe.x - pixel ||
                right_edge > safe.x + safe.w + pixel || bottom > safe.y + safe.h + 0.01f ||
                (right_edge - left < safe.w - 2 * pixel && bottom - top < safe.h - 2 * pixel))
                return 1;
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
