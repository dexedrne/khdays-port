/* The window, run by SDL3 on a thread of its own: the game's thread hands over frames and reads the
 * input through SDL's locks and atomics; gpu_present.c draws, devices.c reads the devices through the
 * player's profile, the settings menu opens on Esc or the pad's settings button, sound_output.c plays.
 * Closing the window ends the process (docs/PLATFORM.md, "The PC's side"). */
#include "host.h"
#include "mobile_touch.h"
#ifdef KHDAYS_MOBILE_TEST
#include "pad_script.h"
#endif
#include "devices.h"
#include "press_edges.h"
#include "../render/gpu_present.h"
#include "../render/png_io.h"
#include "../movies/movie_hd.h"
#include "profile.h"
#include "../../runtime/menus/newgame.h"
#include "../../runtime/menus/savelist.h"
#include "../../runtime/battle/stop_magic.h"
#include "../settings/settings_model.h"
#include "../settings/settings_sample.h"
#include "../settings/settings_ui.h"
#include "../screens/cine_screen.h"
#include "../../runtime/menus/cinelist.h"
#include "../screens/mod_screen.h"
#include "../screens/room_screen.h"
#include "../screens/select_screen.h"
#include "../screens/camp_screen.h"
#include "../screens/movie_pause.h"
#include "../screens/save_screen.h"
#include "sound_output.h"
#include "updates.h"
#include "../screens/ui_font.h"
#include "../screens/ui_text.h"
#include "../../runtime/core/clock.h"
#include "../../runtime/display/display.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/core/language.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/net/net_session.h"
#include "../../runtime/scripts/queue.h"
#include "../../runtime/scripts/view.h"
#include "../../runtime/core/runtime.h"
#include "../../runtime/core/events.h"
#include "../../runtime/core/session_log.h"
#include "../../ui/situation.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 256
#define H 192

/* The plain two-screen frame of khdays_host_present (when there are no frame
 * packages). */
static SDL_Mutex *frame_lock;
static uint32_t frame[2 * W * H];
static SDL_AtomicInt frame_new;     /* set by khdays_host_present, cleared by the window */
static SDL_AtomicInt frame_pending; /* a frame event is already on its way */
static Uint32 frame_event;          /* our SDL event: "a new frame is ready" */
static Uint64 last_redraw;          /* SDL_GetTicks of the last redraw */
#define MOVIE_REDRAW_MS 8           /* an HD video drawn at least this often (ms) */

/* The frame packages. There are three, so neither thread ever waits for the
 * other: the game thread fills one, the newest finished one waits in
 * `mailbox`, and the window shows the third. `mailbox` holds the waiting
 * package's index, with FRESH set until the window takes it. Only the window
 * clears FRESH, so a swap never loses a package either thread holds. */
enum { FRESH = 4 };
static KhdaysFramePackage *packages[3];
static int game_slot;         /* the game thread's */
static int host_slot = 1;     /* the host thread's */
static SDL_AtomicInt mailbox; /* = 2 at the start */
/* What the window asks the game thread to put in each package. */
static SDL_AtomicInt wants;   /* KHDAYS_WANT_* */
static SDL_AtomicInt columns; /* how wide the 3D should be, in DS pixels (256 is the DS's width) */
static SDL_AtomicInt scale;   /* the internal resolution the picture is composed at */

static SDL_Mutex *input_lock;
static KhdaysInputSnapshot input;
/* Presses and releases since the game's last read of the pad (press_edges.h), under input_lock. */
static KhdaysPressEdges edges, edges_field;

static SDL_Semaphore *ready; /* signalled when the window is up (or failed) */
static SDL_AtomicInt started;
static SDL_Semaphore *closed; /* signalled when the window and the sound are gone */
static Uint32 close_event;    /* our SDL event: khdays_host_close's request */
static Uint32 wake_event;     /* our SDL event: khdays_host_wake's, the loop looks again */

/* Stores the latest input for the game thread to read. */
static void publish(const KhdaysInputSnapshot *snapshot)
{
    SDL_LockMutex(input_lock);
    const uint32_t generation = input.generation + 1;
    khdays_press_edges_note(&edges, input.ds_buttons, snapshot->ds_buttons);
    khdays_press_edges_note(&edges_field, input.ds_buttons_field, snapshot->ds_buttons_field);
    input = *snapshot;
    input.generation = generation;
    SDL_UnlockMutex(input_lock);
}

/* Reports a problem with the settings file or a controller: in the log, and
 * on the settings menu's notice line. */
static void notice(const char *text)
{
    fprintf(stderr, "host: %s\n", text);
    khdays_settings_add_notice(text);
}

/* Tells the game thread what to put in the frame packages for the picture
 * the window shows now (it reads this at its next VBlank). */
static void tell_game(void)
{
    SDL_SetAtomicInt(&wants, khdays_present_wants());
    SDL_SetAtomicInt(&columns, khdays_present_columns());
    SDL_SetAtomicInt(&scale, khdays_present_scale());
}

/* The window as [video] display says (profile.h KhdaysDisplay): a window, one without borders
 * over its screen (SDL's full screen at the desktop's mode, NULL), or the screen the game's alone
 * at the desktop's mode. */
static SDL_Window *host_window;
static int last_full = KHDAYS_DISPLAY_BORDERLESS; /* the full screen Alt+Enter goes back to */

static void display_now(int display)
{
    static int shown = -1;
#ifdef KHDAYS_IOS
    /* the phone's window is always the whole screen: leaving full screen (the default setting is "a
     * window") would bring the status bar over the game and lay the view out again as it appears */
    (void)shown;
    (void)display;
    return;
#else
    if (host_window == NULL || display == shown) {
        return;
    }
    shown = display;
    if (display != KHDAYS_DISPLAY_WINDOW) {
        last_full = display;
    }
    if (display == KHDAYS_DISPLAY_WINDOW) {
        SDL_SetWindowFullscreen(host_window, false);
        SDL_SetWindowBordered(host_window, true);
    } else {
        const SDL_DisplayMode *desktop = SDL_GetDesktopDisplayMode(SDL_GetDisplayForWindow(host_window));
        SDL_SetWindowFullscreenMode(host_window, display == KHDAYS_DISPLAY_FULLSCREEN ? desktop : NULL);
        SDL_SetWindowFullscreen(host_window, true);
    }
    SDL_SyncWindow(host_window);
    khdays_log("window: %s", display == KHDAYS_DISPLAY_WINDOW       ? "a window"
                             : display == KHDAYS_DISPLAY_BORDERLESS ? "a borderless window over the screen"
                                                                    : "full screen");
#endif
}

/* Applies a change in the menu's Video section at once: the picture behind
 * the menu is drawn again from the last frame with the new settings. (The game
 * stays paused; if the last frame did not carry the 3D, the change shows from
 * the next frame the game draws.) */
static void video_now(void)
{
    static KhdaysVideo applied;
    const KhdaysVideo *v = &khdays_settings_profile()->video;
    if (memcmp(v, &applied, sizeof(applied)) == 0) {
        return;
    }
    applied = *v;
    display_now(v->display);
    khdays_present_set_video(v);
    tell_game();
    if (packages[0] != NULL) {
        khdays_present_package(packages[host_slot]);
    }
}

/* The text of the ping display ([network] show_ping), to SDL_free; NULL when
 * it is off or outside a session. It lists each member's round trip, joined
 * by " / " ("-" until measured, "<1" under half a millisecond). While the
 * menu is open its setting counts, so a change shows at once. */
static char *ping_line(void)
{
    const KhdaysProfile *p = khdays_settings_is_open() ? khdays_settings_profile() : khdays_devices_profile();
    unsigned int *aids;
    int *ms, count, shown = 0;
    size_t size;
    char *numbers, *line;
    if (!p->network.show_ping || !khdays_net_in_session()) {
        return NULL;
    }
    count = khdays_net_round_trips(NULL, NULL, 0);
    aids = (unsigned int *)SDL_malloc(sizeof(*aids) * (size_t)(count > 0 ? count : 1));
    ms = (int *)SDL_malloc(sizeof(*ms) * (size_t)(count > 0 ? count : 1));
    size = 16 * (size_t)(count > 0 ? count : 1) + 2; /* at most "-2147483648 / " per member */
    numbers = (char *)SDL_malloc(size);
    if (aids == NULL || ms == NULL || numbers == NULL) {
        SDL_free(aids);
        SDL_free(ms);
        SDL_free(numbers);
        return NULL;
    }
    numbers[0] = '\0';
    /* members who joined since the count wait for the next picture */
    count = SDL_min(count, khdays_net_round_trips(aids, ms, count));
    for (int i = 0; i < count; ++i) {
        char one[16];
        if (ms[i] < 0) {
            SDL_strlcpy(one, "-", sizeof(one));
        } else if (ms[i] == 0) {
            SDL_strlcpy(one, "<1", sizeof(one)); /* under half a millisecond: copies on the same PC */
        } else {
            SDL_snprintf(one, sizeof(one), "%d", ms[i]);
        }
        if (shown++ > 0) {
            SDL_strlcat(numbers, " / ", size);
        }
        SDL_strlcat(numbers, one, size);
    }
    SDL_asprintf(&line, khdays_text(TXT_PING_LINE), shown > 0 ? numbers : "-");
    SDL_free(aids);
    SDL_free(ms);
    SDL_free(numbers);
    return line;
}

/* The mods' notices shown now (runtime/scripts/view.h), each for five seconds; each also kept for the
 * settings menu's notice line. */
typedef struct ModNotice {
    char *line;
    Uint64 until;
} ModNotice;
static ModNotice *mod_notices;
static int mod_notice_count, mod_notice_capacity;

static void take_mod_notices(void)
{
    const Uint64 now = SDL_GetTicks();
    int kind, kept = 0;
    char *mod, *text;
    for (int i = 0; i < mod_notice_count; ++i) {
        if (mod_notices[i].until > now) {
            mod_notices[kept++] = mod_notices[i];
        } else {
            SDL_free(mod_notices[i].line);
        }
    }
    mod_notice_count = kept;
    while (khdays_scripts_take_notice(&kind, &mod, &text)) {
        static const int formats[] = {[KHDAYS_NOTICE_STOPPED] = TXT_MOD_STOPPED,
                                      [KHDAYS_NOTICE_PAUSED] = TXT_MOD_PAUSED,
                                      [KHDAYS_NOTICE_REFUSED] = TXT_MOD_REFUSED};
        char said[400], *line = NULL;
        if (kind == KHDAYS_NOTICE_SAID) {
            SDL_snprintf(said, sizeof(said), "%s", text);
        } else {
            SDL_snprintf(said, sizeof(said), khdays_text(formats[kind]), text);
        }
        if (SDL_asprintf(&line, "%s: %s", mod, said) >= 0) {
            khdays_settings_add_notice(line);
            if (mod_notice_count == mod_notice_capacity) {
                const int grown = mod_notice_capacity == 0 ? 4 : mod_notice_capacity * 2;
                ModNotice *more = SDL_realloc(mod_notices, (size_t)grown * sizeof(*more));
                if (more != NULL) {
                    mod_notices = more;
                    mod_notice_capacity = grown;
                }
            }
            if (mod_notice_count < mod_notice_capacity) {
                mod_notices[mod_notice_count].line = line;
                mod_notices[mod_notice_count].until = now + 5000;
                ++mod_notice_count;
            } else {
                SDL_free(line);
            }
        }
        free(mod);
        free(text);
    }
}

/* The picture report: what the window is about to show, read back from the renderer before it is
 * presented -- how much of it is lit and where, with the window's sizes and how the picture was drawn
 * (khdays_present_note). On the phone it tells a picture that was drawn but not shown (the window's
 * side) from one never drawn (the port's side); the Linux picture test reads it too. Reported for
 * the first pictures, a few later ones, after each change of the window's size, and every
 * KHDAYS_PICTURE_LOG pictures; saved as PNGs to KHDAYS_SHOT_WINDOW (on the phone: saves/logs). */
static unsigned picture_count;
static int picture_report_due;
#ifdef KHDAYS_MOBILE
static int pad_lost; /* (the wait for a gamepad, below) */
#endif
static int title_menu_shown; /* the newest frame's situation: the title's menu shows (the pad script's anchor) */

static void picture_report(SDL_Window *window, SDL_Renderer *renderer)
{
    static int every = -1;
    static const char *shot_dir;
    int ww = 0, wh = 0, pw = 0, ph = 0, lw = 0, lh = 0;
    SDL_Rect safe = {0, 0, 0, 0};
    SDL_RendererLogicalPresentation mode = SDL_LOGICAL_PRESENTATION_DISABLED;
    SDL_Surface *got, *rgba;
    const unsigned n = ++picture_count;
    if (every < 0) {
        const char *v = SDL_getenv("KHDAYS_PICTURE_LOG");
        every = v != NULL ? SDL_atoi(v) : 0;
        shot_dir = SDL_getenv("KHDAYS_SHOT_WINDOW");
#ifdef KHDAYS_IOS
        if (shot_dir == NULL) {
            shot_dir = "saves/logs";
        }
#endif
        if (shot_dir != NULL) {
            SDL_CreateDirectory(shot_dir);
        }
    }
    if (picture_report_due > 0) {
        if (--picture_report_due > 0) {
            return; /* the first picture after a change of size: SDL's backbuffer still has the old size */
        }
    } else if (!(n == 1 || n == 30 || n == 300 || n == 900 || n == 1800 || (every > 0 && n % (unsigned)every == 0))) {
        return;
    }
    SDL_GetWindowSize(window, &ww, &wh);
    SDL_GetWindowSizeInPixels(window, &pw, &ph);
    SDL_GetWindowSafeArea(window, &safe);
    fprintf(stderr,
            "picture %u (game frame %u, scene %u): window %dx%d, %dx%d pixels, density %.2f, safe area %d,%d %dx%d, "
            "flags %llx; %s\n",
            n, khdays_events_vblank_count(), khdays_runtime_scene(), ww, wh, pw, ph,
            (double)SDL_GetWindowPixelDensity(window), safe.x, safe.y, safe.w, safe.h,
            (unsigned long long)SDL_GetWindowFlags(window), khdays_present_note());
#ifdef KHDAYS_IOS
    {
        extern void khdays_ios_report(const char *when);
        char when[32];
        SDL_snprintf(when, sizeof(when), "picture %u", n);
        khdays_ios_report(when);
    }
#endif
#ifdef KHDAYS_MOBILE
    fprintf(stderr, "picture %u: touch keys %s (settings %s, a controller %s), their top at y %.0f (window pixels)\n", n,
            !khdays_settings_is_open() && !khdays_devices_gamepad() && !pad_lost ? "drawn" : "not drawn",
            khdays_settings_is_open() ? "open" : "closed",
            khdays_devices_gamepad() ? "connected" : pad_lost ? "not connected: the game waits for one" : "not connected",
            (double)(khdays_mobile_keys_top(window) * SDL_GetWindowPixelDensity(window)));
    fprintf(stderr, "picture %u: layout %s\n", n, khdays_present_layout_note());
#endif
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    got = SDL_RenderReadPixels(renderer, NULL);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
    rgba = got != NULL ? SDL_ConvertSurface(got, SDL_PIXELFORMAT_RGBA32) : NULL;
    SDL_DestroySurface(got);
    if (rgba == NULL) {
        fprintf(stderr, "picture %u: cannot be read back: %s\n", n, SDL_GetError());
    } else {
        int x0 = rgba->w, y0 = rgba->h, x1 = -1, y1 = -1;
        unsigned long long lit = 0, sum[3] = {0, 0, 0};
        const unsigned long long all = (unsigned long long)rgba->w * (unsigned long long)rgba->h;
        for (int y = 0; y < rgba->h; ++y) {
            const Uint8 *row = (const Uint8 *)rgba->pixels + (size_t)y * (size_t)rgba->pitch;
            for (int x = 0; x < rgba->w; ++x) {
                const Uint8 *c = row + 4 * x;
                sum[0] += c[0];
                sum[1] += c[1];
                sum[2] += c[2];
                if (c[0] > 16 || c[1] > 16 || c[2] > 16) {
                    ++lit;
                    x0 = x < x0 ? x : x0;
                    x1 = x > x1 ? x : x1;
                    y0 = y < y0 ? y : y0;
                    y1 = y > y1 ? y : y1;
                }
            }
        }
        if (lit == 0) {
            fprintf(stderr, "picture %u: %dx%d read back, ALL BLACK\n", n, rgba->w, rgba->h);
        } else {
            fprintf(stderr,
                    "picture %u: %dx%d read back, %.1f%% lit, within %d,%d to %d,%d, average colour %llu %llu %llu\n",
                    n, rgba->w, rgba->h, 100.0 * (double)lit / (double)all, x0, y0, x1, y1, sum[0] / all, sum[1] / all,
                    sum[2] / all);
        }
        if (shot_dir != NULL) {
            /* saved no wider than 1024: every k-th pixel */
            const int k = (rgba->w + 1023) / 1024, w = rgba->w / k, h = rgba->h / k;
            uint8_t *small = (uint8_t *)SDL_malloc((size_t)w * (size_t)h * 4);
            if (small != NULL) {
                char path[512];
                const char *error;
                for (int y = 0; y < h; ++y) {
                    const Uint8 *row = (const Uint8 *)rgba->pixels + (size_t)(y * k) * (size_t)rgba->pitch;
                    for (int x = 0; x < w; ++x) {
                        memcpy(small + ((size_t)y * (size_t)w + (size_t)x) * 4, row + 4 * (x * k), 3);
                        small[((size_t)y * (size_t)w + (size_t)x) * 4 + 3] = 255;
                    }
                }
                SDL_snprintf(path, sizeof(path), "%s/picture-%05u.png", shot_dir, n);
                error = khdays_png_write(path, small, w, h);
                fprintf(stderr, error != NULL ? "picture %u: cannot write %s: %s\n" : "picture %u: saved to %s%s\n", n,
                        path, error != NULL ? error : "");
                SDL_free(small);
            }
        }
        SDL_DestroySurface(rgba);
    }
    fflush(stderr);
}

/* Presents the picture; says so when the window would not take it (the first time, then every 600th). */
static void present_picture(SDL_Renderer *renderer)
{
    static unsigned refused;
    if (!SDL_RenderPresent(renderer) && refused++ % 600 == 0) {
        fprintf(stderr, "picture %u: the window did not take it (%u so far): %s\n", picture_count, refused,
                SDL_GetError());
        fflush(stderr);
    }
}

/* The lower-screen view of one screen (host/render/gpu_present.h): shown or put away by its button. */
static void toggle_lower_view(void)
{
    khdays_present_set_lower_view(!khdays_present_lower_view());
    fprintf(stderr, "lower view: %s at game frame %u (scene %u)\n", khdays_present_lower_view() ? "on" : "off",
            khdays_events_vblank_count(), khdays_runtime_scene());
    fflush(stderr);
    khdays_log("one screen: the lower-screen view %s", khdays_present_lower_view() ? "on" : "off");
    picture_report_due = 1; /* the next picture is reported: with the view, or without it again */
}

#ifdef KHDAYS_MOBILE
/* On the phone, the playing gamepad gone with no other connected (its battery, out of reach): the game
 * waits -- its time and sound stop -- until a gamepad connects or the screen is touched (the touch keys
 * play from then on). Not in a multiplayer session, whose other players' games go on. */
static int pad_lost;
static int pad_had; /* a gamepad was playing */
static int settings_stopped;

/* The phone's app in the background (the phone locked, another app in front: from "will enter
 * background" until "did enter foreground"): an HD video and a monologue's voice wait there, as under
 * the settings menu. iOS takes a hidden app's decoding session away, so a video left playing would
 * read into failures until the app is suspended (host/movies/movie_hd_apple.m), and its clock would
 * count the time away as played. They go on at the return unless something else still holds them: the
 * settings menu, the wait for a gamepad, the video's own pause. */
static int app_background;

static void app_background_set(int away)
{
    if (away == app_background) {
        return;
    }
    app_background = away;
    if (away) {
        khdays_hd_pause(1);
        khdays_hd_voice_pause(1);
    } else if (!settings_stopped && !pad_lost) {
        if (!khdays_movie_hd_course_paused()) {
            khdays_hd_pause(0);
        }
        khdays_hd_voice_pause(0);
    }
    fprintf(stderr, "window: the app is %s: an HD video and a voice %s\n", away ? "in the background" : "back in front",
            away                             ? "wait"
            : settings_stopped               ? "still wait under the settings menu"
            : pad_lost                       ? "still wait for a gamepad"
            : khdays_movie_hd_course_paused() ? "go on (a video under its own pause stays paused)"
                                              : "go on");
    fflush(stderr);
}

static void pad_lost_set(int lost)
{
    if (lost == pad_lost) {
        return;
    }
    pad_lost = lost;
    khdays_clock_hold(KHDAYS_HOLD_SETTINGS, lost);
    khdays_sound_output_pause(lost);
    if (lost || (!khdays_movie_hd_course_paused() && !app_background)) {
        khdays_hd_pause(lost); /* (not a video its course keeps paused, nor with the app in the background) */
    }
    khdays_hd_voice_pause(lost || app_background);
    fprintf(stderr, "host: %s at game frame %u\n",
            lost ? "the gamepad is gone: the game waits for one, or for a touch" : "the game goes on",
            khdays_events_vblank_count());
    fflush(stderr);
}

static void pad_lost_draw(SDL_Renderer *r)
{
    static const char *const lines[] = {"Controller disconnected", "Reconnect it, or touch the screen to play with touch"};
    int w = 0, h = 0, lw, lh;
    SDL_RendererLogicalPresentation presentation;
    SDL_GetRenderLogicalPresentation(r, &lw, &lh, &presentation);
    SDL_SetRenderLogicalPresentation(r, 0, 0, SDL_LOGICAL_PRESENTATION_DISABLED);
    SDL_GetCurrentRenderOutputSize(r, &w, &h);
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 150);
    SDL_RenderFillRect(r, NULL);
    SDL_SetRenderDrawColor(r, 255, 255, 255, 255);
    for (int i = 0; i < 2; ++i) {
        const float length = (float)SDL_strlen(lines[i]);
        const float k = SDL_max(1.0f, SDL_floorf((float)w / (8.0f * 60.0f))) * (i == 0 ? 1.5f : 1.0f);
        SDL_SetRenderScale(r, k, k);
        SDL_RenderDebugText(r, ((float)w - 8.0f * length * k) / 2.0f / k,
                            ((float)h / 2.0f + (i == 0 ? -14.0f : 6.0f) * SDL_max(1.0f, (float)w / 480.0f)) / k,
                            lines[i]);
    }
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_SetRenderLogicalPresentation(r, lw, lh, presentation);
}
#endif

/* Draws the last frame, with the FPS and ping displays and the settings menu
 * over it. Draws nothing while the graphics card is lost (the loop recreates
 * it). */
static void redraw(SDL_Window *window, SDL_Renderer *renderer)
{
    int pictures, frames, fps, corner_rows;
    char *ping;
    last_redraw = SDL_GetTicks();
    if (khdays_present_lost()) {
        return;
    }
    khdays_present_draw();
    khdays_save_screen_draw(renderer);   /* the port's save list over the game's (runtime/menus/savelist.h) */
    khdays_cine_screen_draw(renderer);   /* MODO CINE's list as the HD theater's (runtime/menus/cinelist.h) */
    khdays_room_screen_draw(renderer);   /* the port's list of rooms over the game's (runtime/net/roomlist.h) */
    khdays_select_screen_draw(renderer); /* Mission Mode's character select on one screen (charselect.h) */
    khdays_camp_screen_draw(renderer);   /* Mission Mode's camp menu on one screen (camp.h) */
    khdays_mod_screen_draw(renderer);    /* a mod's screen, the title's mods' prompt (runtime/scripts/screens.h) */
    fps = khdays_present_fps(&pictures, &frames);
    if (fps) {
        khdays_settings_fps_draw(renderer, pictures, frames);
    }
    ping = ping_line();
    corner_rows = (fps != 0) + (ping != NULL);
    if (ping != NULL) {
        khdays_settings_ping_draw(renderer, ping, fps);
        SDL_free(ping);
    }
    take_mod_notices();
    for (int i = 0; i < mod_notice_count; ++i) {
        khdays_settings_corner_draw(renderer, mod_notices[i].line, corner_rows + i);
    }
    khdays_settings_draw(renderer, window);
#ifdef KHDAYS_MOBILE
    if (pad_lost) {
        pad_lost_draw(renderer);
    } else if (!khdays_settings_is_open() && !khdays_devices_gamepad()) {
        khdays_mobile_draw(renderer);
    }
#endif
    picture_report(window, renderer);
    present_picture(renderer);
}

#ifdef KHDAYS_MOBILE
/* The phone turned, folded or unfolded (its window's size, pixel size, safe area or the display's
 * orientation changed): the picture is laid out anew at once, with nothing restarted -- the game, its
 * sound and what is held on a gamepad or the touch keys go on. Upright, the game's picture goes across
 * the safe area's width at its top and the lower-screen view below it; lying, it fills the height in
 * the middle (host/render/gpu_present.c). The game's 3D gets its new width with its next frame. Each
 * change is said on the console with the new layout; a picture report follows a few pictures later.
 *
 * `force`: laid out and drawn anew even with nothing changed since the last time -- the return to the
 * foreground. A turn or fold made while the app was hidden may have been taken here already with both
 * its pictures skipped (SDL presents nothing for a minimized window, and its backbuffer gets its new
 * size only in a present), so the first picture after the return would go stretched into the old
 * backbuffer and stay, the game standing still: two pictures again, whatever was noted. */
static void relaid(SDL_Window *window, SDL_Renderer *renderer, const char *why, int force)
{
    static int had[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    static unsigned turns;
    int now[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    SDL_Rect safe = {0, 0, 0, 0};
    SDL_GetWindowSize(window, &now[0], &now[1]);
    SDL_GetWindowSizeInPixels(window, &now[2], &now[3]);
    khdays_mobile_safe(window, &safe);
    now[4] = safe.x;
    now[5] = safe.y;
    now[6] = safe.w;
    now[7] = safe.h;
    if (!force && memcmp(now, had, sizeof(now)) == 0) {
        return; /* (a turn comes as several events: the size, the pixels, the safe area, the display) */
    }
    memcpy(had, now, sizeof(now));
    khdays_present_resized();
    tell_game();
    if (force) {
        picture_report_due = 2; /* the second of the two below: the one in the renewed backbuffer */
    }
    /* twice: the first picture after a change of size still goes to SDL's old backbuffer, and a game
     * that stands still (the settings menu, the wait for a gamepad) sends no frame to draw another */
    redraw(window, renderer);
    redraw(window, renderer);
    fprintf(stderr,
            "window: orientation %s after %s (change %u, picture %u, game frame %u): window %dx%d, %dx%d pixels, "
            "safe area %d,%d %dx%d; layout %s\n",
            khdays_mobile_portrait(window) ? "portrait" : "landscape", why, ++turns, picture_count,
            khdays_events_vblank_count(), now[0], now[1], now[2], now[3], safe.x, safe.y, safe.w, safe.h,
            khdays_present_layout_note());
    fflush(stderr);
    if (!force) {
        picture_report_due = 12; /* once the game's frames have the new width */
    }
}
#endif

/* Whether the open settings menu paused the game. In a multiplayer session
 * it does not, since the other players' games go on. */
static int settings_stopped;
#ifndef KHDAYS_MOBILE
enum { app_background = 0 }; /* (the phone's: app_background_set) */
#endif

/* Keeps the subtitles the new game's question chose (runtime/menus/newgame.h): in the profile, the
 * picture's settings and the settings file. */
static void keep_subtitles(void)
{
    KhdaysProfile p;
    char path[512];
    int on;
    if (!khdays_newgame_take_answer(&on) || khdays_devices_profile()->video.subtitles == on) {
        return;
    }
    p = *khdays_devices_profile();
    p.video.subtitles = on;
    khdays_devices_set_profile(&p);
    khdays_present_set_video(&p.video);
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(&p, path, notice);
}

/* Alt+Enter, as on most PC games: from a window to the full screen chosen last (the borderless
 * window if none), from either full screen to a window; kept in the profile and the settings file
 * as the menu keeps it. */
static void toggle_display(void)
{
    KhdaysProfile p = *khdays_devices_profile();
    char path[512];
    p.video.display = p.video.display == KHDAYS_DISPLAY_WINDOW ? last_full : KHDAYS_DISPLAY_WINDOW;
    khdays_devices_set_profile(&p);
    display_now(p.video.display);
    khdays_present_set_video(&p.video);
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(&p, path, notice);
}

static int is_alt_enter(const SDL_Event *e)
{
    return e->type == SDL_EVENT_KEY_DOWN && !e->key.repeat && (e->key.mod & SDL_KMOD_ALT) &&
           (e->key.scancode == SDL_SCANCODE_RETURN || e->key.scancode == SDL_SCANCODE_KP_ENTER);
}

/* Writes the settings file from the menu's profile as it is now. */
static void save_settings(void)
{
    char path[512];
    khdays_settings_path(path, sizeof(path));
    khdays_profile_save(khdays_settings_profile(), path, notice);
}

/* Applies each change in the open menu as it is made: each part of the profile goes where it would go
 * when the menu closes. (The game gets no input while the menu is open, so
 * new controls take effect from the first press after it closes; the menu
 * itself keeps the buttons it opened with.) */
static KhdaysSound heard; /* the sound settings applied last */

static void apply_changes(void)
{
    const unsigned changes = khdays_settings_take_changes();
    const KhdaysProfile *p = khdays_settings_profile();
    if (changes & KHDAYS_CHANGE_CONTROLS) {
        khdays_devices_set_profile(p);
    }
    if (changes & KHDAYS_CHANGE_VIDEO) {
        video_now(); /* the picture behind the menu follows */
    }
    if (changes & KHDAYS_CHANGE_SOUND) {
        khdays_sound_output_set(&p->sound);
        if (settings_stopped) {
            /* the game is paused, so play a sample of the sound whose volume changed */
            khdays_settings_sample(&heard, &p->sound);
        }
        heard = p->sound;
    }
    if (changes & KHDAYS_CHANGE_NETWORK) {
        save_settings();          /* the nickname is read back from the file */
        khdays_boot_owner_name(); /* for the next multiplayer session the game starts */
    }
    if (changes & KHDAYS_CHANGE_LANGUAGE) {
        /* the menu's text changes at once, the game's when the current scene
         * ends (runtime/core/language.c); KHDAYS_LANGUAGE still takes precedence */
        khdays_language_request(khdays_language_choose(p->language));
    }
    if (changes & KHDAYS_CHANGE_LOG) {
        khdays_log_want(p->session_log);
    }
}

/* While the settings menu shows, the game pauses: its time stops, it gets no input and its sound
 * is silent. In a multiplayer session it only stops getting input. */
static void stop_game_for_menu(void)
{
    khdays_devices_set_game_input(0);
    settings_stopped = !khdays_net_in_session();
    if (settings_stopped) {
        khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 1);
        khdays_sound_output_pause(1);
        khdays_hd_pause(1);       /* an HD video playing for a movie pauses too */
        khdays_hd_voice_pause(1); /* and a monologue's voice */
    }
}

/* Opens the menu with only the notice of a newer release (host/app/updates.h). */
static void open_update_notice(const char *tag)
{
    static const KhdaysUpdateHooks hooks = {khdays_updates_can_install, khdays_updates_download,
                                            khdays_updates_download_state, khdays_updates_download_cancel,
                                            khdays_updates_install};
    khdays_log("updates: telling the player of %s", tag);
    stop_game_for_menu();
    khdays_settings_offer_update(khdays_devices_profile(), khdays_updates_version(), tag, &hooks);
}

static void close_settings(void);

/* The menu has closed: acts on the update notice's answer, and closes the game once the update's
 * installer runs (it replaces the game's files). */
static void settings_closed(SDL_Window *window, SDL_Renderer *renderer)
{
    const int update = khdays_settings_take_update_answer();
    close_settings();
    if (update >= 0) {
        khdays_updates_answer(update); /* the notice's: the page opened, or not told again */
    }
    if (update == KHDAYS_UPDATE_INSTALL) {
        khdays_log("updates: the game closes for its installer");
        _Exit(0);
    }
    redraw(window, renderer);
}

/* Opens the settings menu (the game pauses, stop_game_for_menu). */
static void open_settings(void)
{
    char text[160];
#ifdef KHDAYS_MOBILE
    pad_lost_set(0); /* the menu's own pause from here */
#endif
    stop_game_for_menu();
    while (khdays_devices_take_notice(text, sizeof(text))) {
        khdays_settings_add_notice(text);
    }
    khdays_settings_set_situation(!settings_stopped, khdays_settings_sample_has_movies());
    khdays_settings_set_guest(khdays_net_own_aid() > 0);
    khdays_settings_open(khdays_devices_profile());
    heard = khdays_settings_profile()->sound;
    khdays_scripts_ask(KHDAYS_ASK_SETTINGS_OPENED, NULL, NULL, 0); /* the mods hear it */
}

/* Closes the settings menu: saves and applies what changed, and lets the game
 * run again. */
static void close_settings(void)
{
    apply_changes();
    khdays_scripts_ask(KHDAYS_ASK_SETTINGS_CLOSED, NULL, NULL, 0);
    khdays_settings_sample_stop();
    if (khdays_settings_changed()) {
        khdays_devices_set_profile(khdays_settings_profile()); /* as closing may have fixed it */
        save_settings();
        khdays_present_set_video(&khdays_settings_profile()->video);
        khdays_sound_output_set(&khdays_settings_profile()->sound);
        khdays_log_want(khdays_settings_profile()->session_log);
        khdays_savelist_set_settings(khdays_settings_profile()->save.all_files,
                                     khdays_settings_profile()->save.autosave);
        khdays_boot_owner_name(); /* the nickname, for the next multiplayer session */
        tell_game();
    }
    /* Whatever is still held (the button that closed the menu) only counts for
     * the game once it has been released, and the settings button that closed
     * the menu does not reopen it. */
    khdays_devices_set_game_input(1);
    (void)khdays_devices_take_settings_request();
    if (settings_stopped) {
        khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0);
        khdays_sound_output_pause(0);
        if (!khdays_movie_hd_course_paused() && !app_background) {
            khdays_hd_pause(0); /* not a video its course keeps paused (a pause over it), nor in the background */
        }
        khdays_hd_voice_pause(app_background);
        settings_stopped = 0;
    }
}

/* The mouse pointer is hidden over the game while its window has the focus (the touch screen is not
 * used) and shown over the settings menu or when another window has the focus. With the mouse camera
 * on, the window also holds the mouse while the game has the input (devices.c). */
static void mouse_follow(SDL_Window *window)
{
    const int menu = khdays_settings_is_open();
    const int focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    const int hold = !menu && khdays_devices_profile()->enh.mouse_camera && focused;
    const int shown = menu || !focused;
    if (hold != (int)SDL_GetWindowRelativeMouseMode(window)) {
        SDL_SetWindowRelativeMouseMode(window, hold != 0);
    }
    if (shown != (int)SDL_CursorVisible()) {
        if (shown) {
            SDL_ShowCursor();
        } else {
            SDL_HideCursor();
        }
    }
}

/* Whether this event opens the settings menu (which is closed now). */
static int opens_settings(const SDL_Event *event, SDL_Window *window)
{
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return 1;
    }
    (void)window;
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT &&
        khdays_present_settings_button() && khdays_present_settings_button_at(event->button.x, event->button.y)) {
        return 1; /* the picture's own settings key (the title's) */
    }
    return khdays_devices_take_settings_request();
}

/* Creates the window: the DS's two screens at 2x, or with one picture
 * the largest whole multiple of 342 x 192 (16:9) that fits in
 * three quarters of the desktop. */
static SDL_Window *open_window(const KhdaysProfile *profile)
{
    int w = 2 * W, h = 4 * H;
    SDL_Rect desk;
    if (profile->video.one_screen && SDL_GetDisplayUsableBounds(SDL_GetPrimaryDisplay(), &desk)) {
        const int kx = desk.w * 3 / 4 / 342, ky = desk.h * 3 / 4 / H;
        const int k = kx < ky ? kx : ky;
        w = 342 * (k > 1 ? k : 1);
        h = H * (k > 1 ? k : 1);
    }
#ifdef KHDAYS_IOS
    return SDL_CreateWindow("358/2 Days", w, h, SDL_WINDOW_FULLSCREEN | SDL_WINDOW_HIGH_PIXEL_DENSITY);
#endif
#ifdef KHDAYS_MOBILE_TEST
    {
        const char *size = SDL_getenv("KHDAYS_MOBILE_WINDOW");
        if (size != NULL && SDL_sscanf(size, "%dx%d", &w, &h) == 2) {
            return SDL_CreateWindow("358/2 Days", w, h, SDL_WINDOW_HIGH_PIXEL_DENSITY);
        }
    }
#endif
    return SDL_CreateWindow("Kingdom Hearts 358/2 Days (native)", w, h, SDL_WINDOW_RESIZABLE);
}

/* The window's thread: opens everything, then handles SDL's events until the
 * window closes. */
static int host_thread(void *unused)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    KhdaysProfile profile;
    (void)unused;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        fprintf(stderr, "host: SDL_Init failed: %s\n", SDL_GetError());
        SDL_SignalSemaphore(ready);
        return 1;
    }
    khdays_sound_output_open();
    {
        /* The player's settings (or the defaults, the first time). Read first,
         * so that the window speaks the player's language. */
        char path[512];
        khdays_settings_path(path, sizeof(path));
        khdays_profile_load(&profile, path, notice);
#ifdef KHDAYS_MOBILE
        {
            /* The phone's own defaults, for what the settings file does not say (there is none on
             * the first run; one written by hand may say only a part): the Kingdom Hearts II
             * layout -- attack and confirm on the bottom face button (Cross), jump and back on the
             * right one (Circle), the camera on the right stick -- instead of the DS's positions,
             * and the picture as wide as the phone's screen, folded (about 2.2:1) or unfolded
             * (1.42:1), not 16:9 with bars. Settings changes both, and the file then says. */
            char *text = (char *)SDL_LoadFile(path, NULL);
            const int pad_said = text != NULL && (SDL_strstr(text, "[gamepad") != NULL || SDL_strstr(text, "preset") != NULL);
            const int aspect_said = text != NULL && SDL_strstr(text, "aspect") != NULL;
            if (!pad_said && SDL_getenv("KHDAYS_MOBILE_PRESET_DS") == NULL) {
                khdays_profile_apply_preset(&profile, KHDAYS_PRESET_KH2, KHDAYS_AVAILABLE_ENHANCEMENTS);
                fprintf(stderr, "host: %s: the Kingdom Hearts II gamepad layout\n",
                        text == NULL ? "no settings file yet" : "the settings file names no gamepad layout");
            }
            if (!aspect_said) {
                profile.video.aspect = KHDAYS_ASPECT_WINDOW;
            }
            SDL_free(text);
        }
#endif
        khdays_ui_set_language(khdays_language_choose(profile.language));
        khdays_sound_output_set(&profile.sound);
        khdays_log_want(profile.session_log);
        khdays_savelist_set_settings(profile.save.all_files, profile.save.autosave);
        khdays_stop_set_enabled(profile.gameplay.stop_magic); /* for this run: a change waits for the next */
    }
    window = open_window(&profile);
    host_window = window;
    display_now(profile.video.display);
    khdays_present_set_video(&profile.video);
    renderer = window ? khdays_present_open(window, notice) : NULL;
    frame_event = SDL_RegisterEvents(3);
    close_event = frame_event + 1;
    wake_event = frame_event + 2;
    if (renderer == NULL || frame_event == 0) {
        fprintf(stderr, "host: cannot open the window: %s\n", SDL_GetError());
        SDL_SignalSemaphore(ready);
        return 1;
    }
    khdays_devices_init(&profile, publish);
#ifdef KHDAYS_MOBILE_TEST
    const int pad_script = khdays_pad_script_start();
#else
    const int pad_script = 0;
#endif
    khdays_settings_set_graphics_card(khdays_present_gpu());
    tell_game();
    khdays_updates_start(profile.network.check_updates); /* in the background; told of on the title */
    SDL_SetAtomicInt(&started, 1);
    SDL_SignalSemaphore(ready);

    for (;;) {
        SDL_Event event;
        /* While the settings menu shows, it keeps animating even if the paused
         * game sends no frames. */
        int animating;
        /* A newer release is told of on the front-end (the boot logos, the title: scene 1), never
         * in the middle of the game, and once a run. */
        if (!khdays_settings_is_open() && khdays_runtime_scene() == 1) {
            char tag[64];
            if (khdays_updates_offer(tag, sizeof(tag))) {
                open_update_notice(tag);
                redraw(window, renderer);
            }
        }
        animating = khdays_settings_is_open();
        khdays_devices_frame(); /* a wheel's notch let go */
#ifdef KHDAYS_MOBILE_TEST
        if (pad_script) {
            /* the script's clock is the game's VBlanks, and while the game waits for a gamepad (its
             * time stopped) the wall's clock at 60 a second, so that a script can bring one back */
            static Uint64 waited_ms, waiting_since;
            const int stands = pad_lost || settings_stopped; /* (so too under the settings menu's pause) */
            if (stands && waiting_since == 0) {
                waiting_since = SDL_GetTicks();
            } else if (!stands && waiting_since != 0) {
                waited_ms += SDL_GetTicks() - waiting_since;
                waiting_since = 0;
            }
            khdays_pad_script_frame(khdays_events_vblank_count() +
                                        (unsigned)((waited_ms + (waiting_since ? SDL_GetTicks() - waiting_since : 0)) *
                                                   60 / 1000),
                                    khdays_runtime_scene(), title_menu_shown);
            if (khdays_pad_script_take_report()) {
                /* the script's "!report": the next picture is reported -- drawn now, when the game stands
                 * still and sends none */
                picture_report_due = 1;
                if (pad_lost || settings_stopped) {
                    redraw(window, renderer);
                }
            }
        }
#endif
        mouse_follow(window);
        /* The 3D at the monitor's refresh rate: draw a picture
         * whenever VSync lets one through, between events. */
        const int smooth = khdays_present_smooth();
        /* An HD video playing for a movie is drawn at the display's
         * refresh rate whether or not the game sends frames (the game waits at
         * the movie's end). Here we watch for its end and pass on START, which
         * the waiting game thread cannot read. */
        const int movie = khdays_movie_hd_running();
        /* So is an animated backdrop (the title's, ui/composition.h): its pieces move while the
         * game's still menu sends no frames. */
        const int backdrop = khdays_present_backdrop_moving();
        /* So is a situation still settling on a still picture (a pause opened where the 3D was
         * on both screens): it is decided again as the game's VBlanks pass. */
        const int settling = khdays_present_settling();
        keep_subtitles();
        if (!khdays_settings_is_open()) {
            khdays_cine_screen_buttons(khdays_cinelist_seen()); /* MODO CINE's chapters, the port's list */
        }
        if (movie && !khdays_settings_is_open()) {
            khdays_movie_hd_poll();
            khdays_movie_hd_host_buttons(khdays_host_buttons());
            /* drawn on time even while events keep coming (a gamepad's stream of them): the game,
             * held for a scene's video, sends no frames that would draw it */
            if (SDL_GetTicks() - last_redraw >= MOVIE_REDRAW_MS) {
                redraw(window, renderer);
            }
        }
        if (khdays_present_lost()) {
            /* the port noticed it itself (SDL's renderer does not on D3D12):
             * handle it like SDL's event */
            SDL_zero(event);
            event.type = SDL_EVENT_RENDER_DEVICE_LOST;
        } else if (!(smooth || movie || backdrop || pad_script ? SDL_WaitEventTimeout(&event, 1)
                     : settling                  ? SDL_WaitEventTimeout(&event, 4) /* (a VBlank is ~17 ms) */
                     : animating                 ? SDL_WaitEventTimeout(&event, 50)
                                                 : SDL_WaitEvent(&event))) {
            if (animating) {
                khdays_settings_tick();
                if (khdays_settings_is_open()) {
                    apply_changes(); /* a held direction keeps repeating its change */
                } else {
                    settings_closed(window, renderer); /* by itself: the update's installer runs */
                    continue;
                }
            }
            if ((smooth && khdays_present_refresh()) || (backdrop && khdays_present_refresh_backdrop()) ||
                (settling && khdays_present_refresh_settling()) || animating || movie) {
                redraw(window, renderer);
            }
            continue;
        }
        mouse_follow(window);
        if (event.type == SDL_EVENT_QUIT) {
            if (khdays_settings_is_open() && khdays_settings_profile_changed()) {
                save_settings(); /* closed with the menu open: keep what was changed */
            }
            _Exit(0);
        }
        if (event.type == close_event) {
            if (khdays_settings_is_open()) {
                khdays_clock_hold(KHDAYS_HOLD_SETTINGS, 0); /* a reset while the menu was open */
                if (khdays_settings_profile_changed()) {
                    save_settings(); /* keep what was changed, as closing the menu would */
                }
            }
            khdays_sound_output_close();
            khdays_hd_voice_stop(); /* a monologue's voice still heard */
            khdays_ui_font_forget();
            khdays_settings_renderer_gone();
            khdays_save_screen_renderer_gone();
            khdays_cine_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_mod_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_camp_screen_renderer_gone();
            khdays_movie_pause_renderer_gone();
            khdays_present_close();
            SDL_DestroyWindow(window);
            SDL_Quit();
            SDL_SignalSemaphore(closed);
            return 0;
        }
        if (event.type == frame_event) {
            /* clear this first, so a frame published from now on sends its own event */
            SDL_SetAtomicInt(&frame_pending, 0);
            if (SDL_GetAtomicInt(&mailbox) & FRESH) {
                host_slot = SDL_SetAtomicInt(&mailbox, host_slot) & 3;
                title_menu_shown = packages[host_slot]->situation.taken && packages[host_slot]->situation.title_valid &&
                                   packages[host_slot]->situation.title_shown == KHDAYS_TITLE_SHOWN;
                khdays_present_package(packages[host_slot]);
            } else if (SDL_SetAtomicInt(&frame_new, 0)) {
                SDL_LockMutex(frame_lock);
                khdays_present_screens(frame);
                SDL_UnlockMutex(frame_lock);
            } else {
                continue; /* already taken with an earlier event */
            }
            tell_game(); /* (a scale the graphics card refused changes what the game should fill) */
            redraw(window, renderer);
            continue;
        }
        if (event.type >= SDL_EVENT_TERMINATING && event.type <= SDL_EVENT_DID_ENTER_FOREGROUND) {
            /* the phone's app going to the background and coming back. SDL's renderer draws nothing
             * while its window counts as minimized (from "will enter background" until "did enter
             * foreground"): say what the window's flags are, and draw again once it is back. */
            static const char *const names[] = {"terminating",           "low memory",
                                                "will enter background", "did enter background",
                                                "will enter foreground", "did enter foreground"};
            fprintf(stderr, "window: the app: %s (window flags %llx; 40 = minimized, 8 = hidden) after picture %u\n",
                    names[event.type - SDL_EVENT_TERMINATING], (unsigned long long)SDL_GetWindowFlags(window),
                    picture_count);
            fflush(stderr);
#ifdef KHDAYS_MOBILE
            if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND || event.type == SDL_EVENT_DID_ENTER_BACKGROUND) {
                app_background_set(1);
            } else if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
                app_background_set(0);
                relaid(window, renderer, "the return to the foreground", 1);
            }
#else
            if (event.type == SDL_EVENT_DID_ENTER_FOREGROUND) {
                picture_report_due = 2;
                redraw(window, renderer);
            }
#endif
            continue;
        }
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST &&
            event.type != SDL_EVENT_WINDOW_MOUSE_ENTER && event.type != SDL_EVENT_WINDOW_MOUSE_LEAVE &&
            event.type != SDL_EVENT_WINDOW_EXPOSED) {
            /* the window's own events, as they come: shown, resized, the safe area, focus, a turned phone */
            static int said;
            if (said < 200) {
                ++said;
                fprintf(stderr, "window: event %x (%d, %d) after picture %u\n", (unsigned)event.type,
                        (int)event.window.data1, (int)event.window.data2, picture_count);
                fflush(stderr);
            }
#ifndef KHDAYS_MOBILE /* (the phone's: after the picture is laid out anew, relaid) */
            if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED) {
                picture_report_due = 2;
            }
#endif
        }
#ifdef KHDAYS_MOBILE
        if (event.type == SDL_EVENT_DISPLAY_ORIENTATION) {
            static const char *const names[] = {"unknown", "landscape", "landscape flipped", "portrait",
                                                "portrait flipped"};
            fprintf(stderr, "window: the display's orientation: %s (%d) after picture %u\n",
                    event.display.data1 >= 0 && event.display.data1 < 5 ? names[event.display.data1] : "?",
                    (int)event.display.data1, picture_count);
            fflush(stderr);
        }
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED ||
            event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_DISPLAY_ORIENTATION) {
            relaid(window, renderer,
                   event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED  ? "a pixel-size change"
                   : event.type == SDL_EVENT_WINDOW_SAFE_AREA_CHANGED ? "a safe-area change"
                   : event.type == SDL_EVENT_WINDOW_RESIZED           ? "a size change"
                                                                      : "a display turn",
                   0);
        }
#else
        if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
            khdays_present_resized(); /* settings set to "window" follow the new size */
            tell_game();
        }
#endif
        if (event.type == SDL_EVENT_RENDER_DEVICE_RESET || event.type == SDL_EVENT_RENDER_DEVICE_LOST) {
            int renewed;
            khdays_ui_font_forget(); /* free its textures before their renderer goes */
            khdays_settings_renderer_gone();
            khdays_save_screen_renderer_gone();
            khdays_cine_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_mod_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_camp_screen_renderer_gone();
            khdays_movie_pause_renderer_gone();
            renderer = khdays_present_event(&event, &renewed);
            khdays_settings_set_graphics_card(khdays_present_gpu());
            tell_game();
            redraw(window, renderer);
            continue;
        }

        /* Alt+Enter: the window or the full screen; neither the game's Enter nor the menu's (the
         * menu has its own Display mode, and its profile would undo a change made under it) */
        if (is_alt_enter(&event)) {
            if (!khdays_settings_is_open()) {
                toggle_display();
                tell_game();
            }
            continue;
        }

        /* Input. The devices always see it (they track what is held). While the
         * settings menu is open, the menu gets the input and the game none. */
        if (khdays_settings_is_open()) {
            /* One picture for the events waiting, not one each: a gamepad's sticks and
             * triggers send their moves many times a frame (and SDL each twice, as a joystick's
             * and a gamepad's), and a picture each -- every one waiting for the display's
             * refresh -- left the menu behind the player's presses. At least a picture a frame
             * while they keep coming. */
            static Uint64 drawn_at;
            khdays_devices_handle(&event, renderer);
#ifdef KHDAYS_MOBILE
            if (khdays_devices_take_pad_change()) {
                pad_had = khdays_devices_gamepad() != NULL; /* under the menu's own pause: no wait for a gamepad */
                khdays_present_resized(); /* (the touch keys come or go: where the lower-screen view fits) */
                tell_game();
            }
#endif
            if (khdays_settings_handle(&event, window)) {
                settings_closed(window, renderer);
                continue;
            }
            khdays_settings_tick(); /* also while events keep coming (a gamepad's stream of them) */
            if (!khdays_settings_is_open()) {
                settings_closed(window, renderer);
                continue;
            }
            apply_changes();
            if (!SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST) || SDL_GetTicks() - drawn_at >= 16) {
                redraw(window, renderer);
                drawn_at = SDL_GetTicks();
            }
            continue;
        }
        if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F12) {
            khdays_log_mark();     /* F12 marks the session log: "the player saw something here" */
            khdays_display_mark(); /* and saves what the DS shows then (runtime/display/display.c) */
        }
        if (khdays_room_screen_event(&event)) {
            /* a field of the list of rooms takes what is typed (Escape there is its own); the
             * devices still track what is held */
            khdays_devices_handle(&event, renderer);
            continue;
        }
        if (opens_settings(&event, window)) {
            open_settings();
            redraw(window, renderer);
            continue;
        }
#ifdef KHDAYS_MOBILE
        if (pad_lost && event.type == SDL_EVENT_FINGER_DOWN) {
            /* played by touch from here. This touch only wakes the game: the keys were not drawn under
             * the notice, so it is no key's press (its later moves and lift find no finger of theirs) */
            pad_lost_set(0);
            redraw(window, renderer);
            continue;
        }
#endif
        khdays_devices_handle(&event, renderer); /* keys, mouse, gamepads, focus */
        if (khdays_devices_take_pad_change()) {
            /* the playing gamepad changed: the phone's touch keys show without one and hide with one */
            fprintf(stderr, "host: gamepads: %d connected, %s%s; touch keys %s (game frame %u)\n",
                    khdays_devices_pad_count(), khdays_devices_gamepad() != NULL ? "playing: " : "none plays",
                    khdays_devices_gamepad() != NULL ? khdays_devices_gamepad_name() : "",
#ifdef KHDAYS_MOBILE
                    khdays_devices_gamepad() != NULL ? "hidden" : "shown",
#else
                    "not in this build",
#endif
                    khdays_events_vblank_count());
            fflush(stderr);
#ifdef KHDAYS_MOBILE
            pad_lost_set(khdays_devices_gamepad() == NULL && pad_had && !khdays_net_in_session());
            pad_had = khdays_devices_gamepad() != NULL;
            /* the touch keys come or go, and with them where the lower-screen view fits: played by touch
             * it stays clear of them (host/render/gpu_present.c, view_below) */
            khdays_present_resized();
            tell_game();
#endif
            redraw(window, renderer);
        }
#ifdef KHDAYS_MOBILE
        if (pad_lost && event.type == SDL_EVENT_KEY_DOWN) {
            pad_lost_set(0); /* played on the keyboard from here */
            redraw(window, renderer);
        }
#endif
        if (khdays_devices_take_lower_view_request() & 1) {
            toggle_lower_view();
            redraw(window, renderer);
        }
        for (int n = khdays_devices_take_hud_mode_request(); n > 0; --n) {
            khdays_situation_hud_mode_next(); /* the one-screen HUD mode */
            khdays_log("one screen: the HUD mode now %d (0 complete, 1 exploration, 2 none)",
                       khdays_situation_hud_mode());
        }
        if (khdays_devices_take_settings_request()) {
            open_settings();
            redraw(window, renderer);
        }
    }
}

void khdays_host_tell_no_data(const char *where, const char *broken)
{
    const int language = khdays_language_of_system();
    const char *format = khdays_text_in(broken != NULL ? TXT_GAME_DATA_BROKEN : TXT_NEEDS_GAME_DATA, language);
    char text[1600];
    if (format == NULL) {
        format = khdays_text_in(broken != NULL ? TXT_GAME_DATA_BROKEN : TXT_NEEDS_GAME_DATA, KHDAYS_LANGUAGE_ENGLISH);
    }
    if (broken != NULL) {
        SDL_snprintf(text, sizeof(text), format, where, broken);
    } else {
        SDL_snprintf(text, sizeof(text), format, where);
    }
    fprintf(stderr, "host: %s\n", text);
    fflush(stderr);
    if (getenv("KHDAYS_HEADLESS") == NULL) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kingdom Hearts 358/2 Days (native)", text, NULL);
    }
}

#ifdef KHDAYS_IOS
static void SDLCALL ios_host_loop(void *unused) { host_thread(unused); }
#endif

int khdays_host_start(void)
{
    if (frame_lock == NULL) {
        frame_lock = SDL_CreateMutex();
        input_lock = SDL_CreateMutex();
    }
    if (frame_lock == NULL || input_lock == NULL) {
        return 0;
    }
    if (getenv("KHDAYS_HEADLESS") != NULL) {
        /* No window and no devices: a KHDAYS_INPUT script plays instead. It
         * uses the enhancements (and their speeds) from the settings file
         * named by KHDAYS_SETTINGS, if any; the regression walks name none,
         * so they run with the defaults. */
        const char *path = SDL_getenv("KHDAYS_SETTINGS");
        if (path != NULL) {
            KhdaysProfile profile;
            KhdaysDeviceState nothing;
            khdays_profile_load(&profile, path, notice);
            khdays_bindings_reset(&nothing);
            SDL_LockMutex(input_lock);
            khdays_bindings_resolve(&profile, &nothing, &input);
            SDL_UnlockMutex(input_lock);
        }
        return 1;
    }
    if (packages[0] == NULL) {
        for (int n = 0; n < 3; ++n) {
            packages[n] = (KhdaysFramePackage *)SDL_calloc(1, sizeof(KhdaysFramePackage));
            if (packages[n] == NULL) {
                return 0;
            }
        }
        SDL_SetAtomicInt(&mailbox, 2);
    }
    ready = SDL_CreateSemaphore(0);
    closed = SDL_CreateSemaphore(0);
    if (ready == NULL || closed == NULL) return 0;
#ifdef KHDAYS_IOS
    if (!SDL_RunOnMainThread(ios_host_loop, NULL, false)) {
#else
    if (SDL_CreateThread(host_thread, "host", NULL) == NULL) {
#endif
        return 0;
    }
    SDL_WaitSemaphore(ready);
    return SDL_GetAtomicInt(&started) != 0;
}

void khdays_host_close(void)
{
    SDL_Event event;
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_SetAtomicInt(&started, 0); /* accept no more frames */
    SDL_zero(event);
    event.type = close_event;
    if (SDL_PushEvent(&event)) {
        SDL_WaitSemaphoreTimeout(closed, 5000);
    }
}

void khdays_host_wake(void)
{
    SDL_Event event;
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_zero(event);
    event.type = wake_event;
    SDL_PushEvent(&event);
}

void khdays_host_present(const uint32_t *upper, const uint32_t *lower)
{
    if (!SDL_GetAtomicInt(&started)) {
        return;
    }
    SDL_LockMutex(frame_lock);
    memcpy(frame, upper, W * H * 4);
    memcpy(frame + W * H, lower, W * H * 4);
    SDL_UnlockMutex(frame_lock);
    SDL_SetAtomicInt(&frame_new, 1);
    if (SDL_SetAtomicInt(&frame_pending, 1) == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = frame_event;
        SDL_PushEvent(&event);
    }
}

int khdays_host_frame_wants(void)
{
    return SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&wants) : 0;
}

float khdays_host_frame_widen(void)
{
    const int n = SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&columns) : 0;
    return n > 256 ? (float)n / 256.0f : 1.0f;
}

int khdays_host_frame_scale(void)
{
    const int n = SDL_GetAtomicInt(&started) ? SDL_GetAtomicInt(&scale) : 1;
    return n < 1 ? 1 : n;
}

KhdaysFramePackage *khdays_host_frame_begin(void)
{
    return SDL_GetAtomicInt(&started) && packages[0] != NULL ? packages[game_slot] : NULL;
}

void khdays_host_frame_publish(KhdaysFramePackage *package)
{
    if (package != packages[game_slot]) {
        return;
    }
    game_slot = SDL_SetAtomicInt(&mailbox, game_slot | FRESH) & 3;
    if (SDL_GetAtomicInt(&started) && SDL_SetAtomicInt(&frame_pending, 1) == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = frame_event;
        SDL_PushEvent(&event);
    }
}

/* Copies the latest snapshot. Headless (no window), everything is 0. */
static void khdays_host_input(KhdaysInputSnapshot *out)
{
    if (input_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(input_lock);
    *out = input;
    SDL_UnlockMutex(input_lock);
}

void khdays_host_input_take(KhdaysInputSnapshot *out)
{
    if (input_lock == NULL) {
        memset(out, 0, sizeof(*out));
        return;
    }
    SDL_LockMutex(input_lock);
    *out = input;
    out->ds_buttons = khdays_press_edges_take(&edges, input.ds_buttons);
    out->ds_buttons_field = khdays_press_edges_take(&edges_field, input.ds_buttons_field);
    SDL_UnlockMutex(input_lock);
}

uint16_t khdays_host_buttons(void)
{
    KhdaysInputSnapshot now;
    khdays_host_input(&now);
    return now.ds_buttons;
}

int khdays_host_touch(int *x, int *y)
{
    KhdaysInputSnapshot now;
    khdays_host_input(&now);
    if (!now.touching) {
        return 0;
    }
    *x = now.touch_x;
    *y = now.touch_y;
    return 1;
}
