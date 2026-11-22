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
/* Whether the window's safe area is taller than wide: the phone held upright. The window is laid out
 * for it anew whenever its size or safe area changes (a turned phone): nothing restarts. */
bool khdays_mobile_portrait(SDL_Window *);
#ifdef KHDAYS_MOBILE_TEST
/* The unsafe margins from now on, instead of KHDAYS_MOBILE_INSETS (a test's turned phone). */
void khdays_mobile_test_insets(int left, int top, int right, int bottom);
#endif
/* Fits the logical picture `width` x `height` into the safe area: in its middle, or (`top`, portrait)
 * at its top, the free space below it. */
void khdays_mobile_view(SDL_Renderer *, SDL_Rect safe, int width, int height);
void khdays_mobile_view_at(SDL_Renderer *, SDL_Rect safe, int width, int height, bool top);
const char *khdays_mobile_view_note(void);
void khdays_mobile_stylus_rect(SDL_Renderer *, SDL_FRect);
/* Where a finger is the stylus now, in the window's coordinates (empty: nowhere). */
void khdays_mobile_stylus_window(SDL_FRect *);
void khdays_mobile_draw(SDL_Renderer *);
/* The top of everything a finger plays with when no controller does -- the keys and the stick's zone --
 * as a y of the window (they reach from there to the safe area's foot); negative with no window to lay
 * them out in. What is to be touched as the DS's lower screen is kept above it (the lower-screen view
 * below the picture, host/render/gpu_present.c). */
float khdays_mobile_keys_top(SDL_Window *);
void khdays_mobile_finger(const SDL_Event *, SDL_Renderer *, bool controller);
void khdays_mobile_reset(void);
/* The window was laid out anew (turned, folded): held keys stay held and the stick keeps its
 * direction; a finger that was the stylus is let go. */
void khdays_mobile_relaid(void);
void khdays_mobile_snapshot(KhdaysInputSnapshot *);
#endif
