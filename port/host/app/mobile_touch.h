#ifndef KHDAYS_MOBILE_TOUCH_H
#define KHDAYS_MOBILE_TOUCH_H
#include "host.h"
#include <SDL3/SDL.h>
/* KHDAYS_MOBILE: the phone's layouts, safe areas and touch controls. iOS has them; a Linux test build
 * (CMake's KHDAYS_MOBILE_TEST) has them too, to draw the phone's picture on a hidden video driver:
 * KHDAYS_MOBILE_WINDOW=2853x2007, KHDAYS_MOBILE_LAYOUT=0|1|2 (stacked, side by side, single) and
 * KHDAYS_MOBILE_INSETS=left,top,right,bottom (the unsafe margins, in window units). */
#if (defined(KHDAYS_IOS) || defined(KHDAYS_MOBILE_TEST)) && !defined(KHDAYS_MOBILE)
#define KHDAYS_MOBILE 1
#endif
int khdays_ios_layout(void);
bool khdays_mobile_safe(SDL_Window *, SDL_Rect *safe);
void khdays_mobile_view(SDL_Renderer *, SDL_Rect safe, int width, int height);
const char *khdays_mobile_view_note(void);
void khdays_mobile_stylus_rect(SDL_Renderer *, SDL_FRect);
void khdays_mobile_draw(SDL_Renderer *);
void khdays_mobile_finger(const SDL_Event *, SDL_Renderer *, bool controller);
void khdays_mobile_reset(void);
void khdays_mobile_snapshot(KhdaysInputSnapshot *);
#endif
