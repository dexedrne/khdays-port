#ifndef KHDAYS_MOBILE_TOUCH_H
#define KHDAYS_MOBILE_TOUCH_H
#include "host.h"
#include <SDL3/SDL.h>
#ifdef __cplusplus
extern "C" {
#endif
/* KHDAYS_MOBILE: the phone's layouts, safe areas and touch controls. iOS has them; a Linux test build
 * (CMake's KHDAYS_MOBILE_TEST) has them too, to draw the phone's picture on a hidden video driver:
 * KHDAYS_MOBILE_WINDOW=2853x2007, KHDAYS_MOBILE_LAYOUT=0|1|2 (stacked, side by side, single) and
 * KHDAYS_MOBILE_INSETS=left,top,right,bottom (the unsafe margins, in window units). */
#if (defined(KHDAYS_IOS) || defined(KHDAYS_MOBILE_TEST)) && !defined(KHDAYS_MOBILE)
#define KHDAYS_MOBILE 1
#endif
int khdays_ios_layout(void);
bool khdays_mobile_safe(SDL_Window *, SDL_Rect *safe);
/* The visible safe rectangle, in the window's pixels: the safe area (inside the notch, the hinge's
 * strip, the home bar), kept also KHDAYS_HUD_CORNER points (12 unless the environment says otherwise)
 * clear of every edge of the display, which keeps what is placed in a corner clear of its rounding where
 * the safe area has no margin there (the unfolded phone lying: none but the home bar's). The HUD, the
 * FPS and notice lines, the controller notice, the settings menu and the port's list screens are laid
 * out inside it over the filled picture. Its corner clearance in pixels in `corner` when not NULL. */
bool khdays_mobile_visible(SDL_Window *, SDL_FRect *pixels, float *corner);
/* What the window draws over the picture that keeps inside the visible safe area (the FPS line, the
 * controller notice, the settings menu, a list screen), each redraw, for the picture reports
 * (host/app/window.c): cleared at each redraw's start, then each box as it is drawn, in window pixels. */
void khdays_mobile_overlay_clear(void);
void khdays_mobile_overlay_box(const char *what, SDL_FRect pixels);
int khdays_mobile_overlay_count(void);
const char *khdays_mobile_overlay(int index, SDL_FRect *pixels);
/* The renderer's visible safe rectangle in its output's pixels (the window's: the logical presentation set
 * aside): khdays_mobile_visible for its window, else the whole output. */
SDL_FRect khdays_mobile_visible_output(SDL_Renderer *);
/* Whether the window's safe area is taller than wide: the phone held upright. The window is laid out
 * for it anew whenever its size or safe area changes (a turned phone): nothing restarts. */
bool khdays_mobile_portrait(SDL_Window *);
/* The phone's screen and heat, for the picture rate (host/render/picture_pace.h): the most pictures a second
 * its screen shows (UIScreen's maximumFramesPerSecond: 120 on a ProMotion screen; 0 not known), its thermal
 * state (NSProcessInfo's: 0 nominal, 1 fair, 2 serious, 3 critical), and the rate asked of the display from
 * now on (the display link's preferredFrameRateRange: 120, or 60 to let the screen slow down and cool).
 * tools/ios/ios_main.m; the Linux test build's in mobile_touch.c (KHDAYS_TEST_SCREEN_HZ, else the
 * KHDAYS_SCREEN_LOCK=HZ screen; KHDAYS_TEST_THERMAL or the pad script's "!thermal=N"). */
int khdays_ios_screen_most_hz(void);
int khdays_ios_thermal_state(void);
void khdays_ios_picture_rate(int hz);
#ifdef KHDAYS_MOBILE_TEST
/* The unsafe margins from now on, instead of KHDAYS_MOBILE_INSETS (a test's turned phone). */
void khdays_mobile_test_insets(int left, int top, int right, int bottom);
/* The thermal state the test's phone reports from now on (the pad script's "!thermal=N"), and the time
 * each picture is made to take longer (its "!slow=MS": a slow phone's pictures), in milliseconds. */
void khdays_mobile_test_thermal(int state);
void khdays_mobile_test_slow(int ms);
int khdays_mobile_test_slow_ms(void);
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
 * them out in. (Round 9: the DS's lower screen is only ever drawn over the picture, so no part of it is
 * kept clear of the keys; host/render/gpu_present.c.) */
float khdays_mobile_keys_top(SDL_Window *);
void khdays_mobile_finger(const SDL_Event *, SDL_Renderer *, bool keys_visible);
bool khdays_mobile_keys_visible(void);
bool khdays_mobile_settings_at(SDL_Window *, float x, float y);
void khdays_mobile_reset(void);
/* The window was laid out anew (turned, folded): held keys stay held and the stick keeps its
 * direction; a finger that was the stylus is let go. */
void khdays_mobile_relaid(void);
void khdays_mobile_snapshot(KhdaysInputSnapshot *);
#ifdef __cplusplus
}
#endif
#endif
