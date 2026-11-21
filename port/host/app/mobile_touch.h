#ifndef KHDAYS_MOBILE_TOUCH_H
#define KHDAYS_MOBILE_TOUCH_H
#include "host.h"
#include <SDL3/SDL.h>
void khdays_mobile_view(SDL_Renderer *, SDL_Rect safe, int width, int height);
void khdays_mobile_stylus_rect(SDL_Renderer *, SDL_FRect);
void khdays_mobile_draw(SDL_Renderer *);
void khdays_mobile_finger(const SDL_Event *, SDL_Renderer *, bool controller);
void khdays_mobile_reset(void);
void khdays_mobile_snapshot(KhdaysInputSnapshot *);
#endif
