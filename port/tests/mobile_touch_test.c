#include "../host/app/mobile_touch.h"
#include "../host/app/profile.h"
#include <stdlib.h>

static SDL_Renderer *renderer;
static unsigned finger(SDL_EventType type, SDL_FingerID id, float x, float y, bool pad)
{
    SDL_Event e = {0};
    e.type = type;
    e.tfinger.fingerID = id;
    e.tfinger.x = x / 512;
    e.tfinger.y = y / 384;
    khdays_mobile_finger(&e, renderer, pad);
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
    unsigned a = 1u << KHDAYS_ACT_A, right = 1u << KHDAYS_ACT_RIGHT;
    if (finger(SDL_EVENT_FINGER_DOWN, 1, 486, 302, false) != a ||
        finger(SDL_EVENT_FINGER_DOWN, 2, 35, 310, false) != a ||
        finger(SDL_EVENT_FINGER_MOTION, 2, 75, 310, false) != (a | right) ||
        finger(SDL_EVENT_FINGER_CANCELED, 1, 486, 302, false) != right)
        return 1;
    khdays_mobile_reset();
    if (finger(SDL_EVENT_FINGER_DOWN, 3, 120, 210, true))
        return 1;
    KhdaysInputSnapshot snapshot = {0};
    khdays_mobile_snapshot(&snapshot);
    if (!snapshot.touching || snapshot.touch_x != 60 || snapshot.touch_y != 18)
        return 1;
    const int screens[][2] = {{2853, 2007}, {2007, 2853}, {393, 852}};
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
        }
    }
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
