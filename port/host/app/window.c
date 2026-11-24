/* The window, run by SDL3 on a thread of its own: the game's thread hands over frames and reads the
 * input through SDL's locks and atomics; gpu_present.c draws, devices.c reads the devices through the
 * player's profile, the settings menu opens on Esc or the pad's settings button, sound_output.c plays.
 * Closing the window ends the process (docs/PLATFORM.md, "The PC's side"). */
#include "host.h"
#include "fs_path.h"
#include "instance.h"
#include "hud_settings.h"
#include "mobile_touch.h"
#if defined(KHDAYS_MOBILE_TEST) || defined(KHDAYS_IOS)
#include "pad_script.h"
#endif
#include "devices.h"
#include "press_edges.h"
#include "../render/gpu_present.h"
#include "../render/onescreen.h"
#include "../render/png_io.h"
#include "../movies/movie_hd.h"
#include "../movies/movie_subtitles.h"
#include "../screens/game_pause.h"
#include "profile.h"
#include "../../runtime/menus/newgame.h"
#include "../../runtime/menus/savelist.h"
#include "../../runtime/battle/stop_magic.h"
#include "../../runtime/plugins/plugins.h"
#include "../settings/settings_model.h"
#include "../settings/settings_sample.h"
#include "../settings/settings_ui.h"
#include "../screens/cine_screen.h"
#include "../screens/day_screen.h"
#include "../movies/theater_music.h"
#include "../../runtime/menus/cinelist.h"
#include "../screens/mod_screen.h"
#include "../../runtime/menus/camp.h"
#include "../screens/room_screen.h"
#include "../screens/select_screen.h"
#include "../screens/ds_select_pages.h"
#include "../screens/camp_screen.h"
#include "../screens/shop_screen.h"
#include "../screens/movie_pause.h"
#include "../screens/modern_pause.h"
#include "../screens/save_screen.h"
#include "sound_output.h"
#include "updates.h"
#include "../screens/ui_font.h"
#include "../screens/ui_text.h"
#include "../../runtime/core/clock.h"
#include "../../runtime/display/display.h"
#include "../../runtime/display/frame_package.h"
#include "../../runtime/core/language.h"
#include "../../runtime/mods/installed.h"
#include "../../runtime/movies/movie_hd.h"
#include "../../runtime/movies/scene_trace.h"
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
#include "../render/render_counts.h" /* (the allocations and read-backs counted) */

#define W 256
#define H 192
#define WALK_SPEED_DEFAULT 50 /* profile.c's walk_speed */
#define WALK_SPEED_MOD 55     /* the Walk mod's */

/* The plain two-screen frame of khdays_host_present (when there are no frame
 * packages). */
static SDL_Mutex *frame_lock;
static uint32_t frame[2 * W * H];
static SDL_AtomicInt frame_new;     /* set by khdays_host_present, cleared by the window */
static SDL_AtomicInt frame_pending; /* a frame event is already on its way */
static Uint32 frame_event;          /* our SDL event: "a new frame is ready" */
static Uint64 last_redraw;          /* SDL_GetTicks of the last redraw */
#define MOVIE_REDRAW_MS 8           /* an HD video drawn at least this often (ms) */
#define MOVIE_REDRAW_LATE_MS 25     /* ... and, while events keep coming, at most this late (ms) */

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
/* The DS buttons pressed since the window's last look for the HD movies' pause panel (even let go
 * again since), under input_lock: a quick tap is never lost however long a look takes. */
static uint16_t pause_taps;

static SDL_Semaphore *ready; /* signalled when the window is up (or failed) */
static SDL_AtomicInt started;
static SDL_Semaphore *closed; /* signalled when the window and the sound are gone */
static Uint32 close_event;    /* our SDL event: khdays_host_close's request */
static Uint32 wake_event;     /* our SDL event: khdays_host_wake's, the loop looks again */

/* KHDAYS_TRACE_LATENCY: a press's way to the screen, said every 20 presses -- from SDL taking it (the
 * event's time) to the window handing it to the game, to the game's pad sample, to the presentation of
 * the first picture made after that sample (submitted: the screen shows it at its next refresh). One
 * press at a time: one made before the last reached the screen is not counted. */
static int trace_latency;
static SDL_AtomicInt latency_stage; /* 0 none, 1 handed to the game, 2 sampled by it */
static Uint64 latency_ns[3];        /* SDL's, handed, sampled */
static uint32_t latency_frame;      /* the VBlank the game sampled it at */
static uint32_t latency_shown;      /* the last picture presented: its VBlank (window thread) */
static struct {
    int presses;
    double handed, sampled, shown, worst;
} latency;

/* A press of a key nothing uses (F24) every 370 ms, at no fixed place in the frames: what is timed. */
static Uint32 SDLCALL latency_press(void *userdata, SDL_TimerID timer, Uint32 interval)
{
    SDL_Event e;
    (void)userdata;
    (void)timer;
    SDL_zero(e);
    e.type = SDL_EVENT_KEY_DOWN;
    e.key.scancode = SDL_SCANCODE_F24;
    e.key.down = true;
    SDL_PushEvent(&e);
    e.type = SDL_EVENT_KEY_UP;
    e.key.down = false;
    SDL_PushEvent(&e);
    return interval;
}

static void latency_start(void)
{
    trace_latency = SDL_getenv("KHDAYS_TRACE_LATENCY") != NULL;
    if (trace_latency) {
        SDL_AddTimer(370, latency_press, NULL);
    }
}

static void latency_pressed(const SDL_Event *event)
{
    if (!trace_latency || SDL_GetAtomicInt(&latency_stage) != 0 ||
        !((event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat) || event->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN)) {
        return;
    }
    latency_ns[0] = event->common.timestamp;
    latency_ns[1] = SDL_GetTicksNS();
    SDL_SetAtomicInt(&latency_stage, 1);
}

void khdays_host_latency_sampled(uint32_t vblank)
{
    if (trace_latency && SDL_GetAtomicInt(&latency_stage) == 1) {
        latency_ns[2] = SDL_GetTicksNS();
        latency_frame = vblank;
        SDL_SetAtomicInt(&latency_stage, 2);
    }
}

static void latency_presented(void)
{
    if (!trace_latency || SDL_GetAtomicInt(&latency_stage) != 2 || latency_shown <= latency_frame) {
        return;
    }
    {
        const Uint64 now = SDL_GetTicksNS();
        const double handed = (double)(latency_ns[1] - latency_ns[0]) / 1e6;
        const double sampled = (double)(latency_ns[2] - latency_ns[1]) / 1e6;
        const double shown = (double)(now - latency_ns[2]) / 1e6;
        latency.handed += handed;
        latency.sampled += sampled;
        latency.shown += shown;
        latency.worst = SDL_max(latency.worst, handed + sampled + shown);
        if (++latency.presses == 20) {
            fprintf(stderr,
                    "latency: 20 presses: %.1f ms to the window, %.1f to the game's sample, %.1f to the picture "
                    "presented (%.1f on average, %.1f at most)\n",
                    latency.handed / 20, latency.sampled / 20, latency.shown / 20,
                    (latency.handed + latency.sampled + latency.shown) / 20, latency.worst);
            SDL_zero(latency);
        }
        SDL_SetAtomicInt(&latency_stage, 0);
    }
}

/* Stores the latest input for the game thread to read. */
static void publish(const KhdaysInputSnapshot *snapshot)
{
    SDL_LockMutex(input_lock);
    const uint32_t generation = input.generation + 1;
    khdays_press_edges_note(&edges, input.ds_buttons, snapshot->ds_buttons);
    khdays_press_edges_note(&edges_field, input.ds_buttons_field, snapshot->ds_buttons_field);
    pause_taps |= (uint16_t)(snapshot->ds_buttons & ~input.ds_buttons);
    const int changed = snapshot->ds_buttons != input.ds_buttons || snapshot->move_x != input.move_x ||
                        snapshot->move_y != input.move_y;
    input = *snapshot;
    input.generation = generation;
    SDL_UnlockMutex(input_lock);
    if (changed) {
        khdays_clock_wake(); /* a game frame waiting out its pace may start at once (input.c's press pace) */
    }
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

/* The window as [video] display and resolution say (profile.h KhdaysDisplay): a window (that size,
 * when one is set), one without borders over its screen (SDL's full screen at the desktop's mode,
 * NULL), or the screen the game's alone at that mode (the screen's closest), else the desktop's. */
static SDL_Window *host_window;
static int last_full = KHDAYS_DISPLAY_BORDERLESS; /* the full screen Alt+Enter goes back to */

static void display_now(int display, int resolution)
{
    static int shown = -1, shown_resolution = -1;
#ifdef KHDAYS_IOS
    /* the phone's window is always the whole screen: leaving full screen (the default setting is "a
     * window") would bring the status bar over the game and lay the view out again as it appears */
    (void)shown;
    (void)shown_resolution;
    (void)display;
    (void)resolution;
    return;
#else
    if (host_window == NULL || (display == shown && resolution == shown_resolution)) {
        return;
    }
    shown = display;
    shown_resolution = resolution;
    if (display != KHDAYS_DISPLAY_WINDOW) {
        last_full = display;
    }
    if (display == KHDAYS_DISPLAY_WINDOW) {
        SDL_SetWindowFullscreen(host_window, false);
        SDL_SetWindowBordered(host_window, true);
        if (resolution != 0) {
            SDL_SetWindowSize(host_window, KHDAYS_SCREEN_WIDTH(resolution), KHDAYS_SCREEN_HEIGHT(resolution));
            SDL_SetWindowPosition(host_window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
        }
    } else {
        const SDL_DisplayID screen = SDL_GetDisplayForWindow(host_window);
        const SDL_DisplayMode *mode = SDL_GetDesktopDisplayMode(screen);
        SDL_DisplayMode closest;
        if (display == KHDAYS_DISPLAY_FULLSCREEN && resolution != 0 &&
            SDL_GetClosestFullscreenDisplayMode(screen, KHDAYS_SCREEN_WIDTH(resolution),
                                                KHDAYS_SCREEN_HEIGHT(resolution), 0.0f, true, &closest)) {
            mode = &closest;
        }
        SDL_SetWindowFullscreenMode(host_window, display == KHDAYS_DISPLAY_FULLSCREEN ? mode : NULL);
        SDL_SetWindowFullscreen(host_window, true);
    }
    SDL_SyncWindow(host_window);
    {
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(host_window, &w, &h);
        khdays_log("window: %s, %d x %d",
                   display == KHDAYS_DISPLAY_WINDOW       ? "a window"
                   : display == KHDAYS_DISPLAY_BORDERLESS ? "a borderless window over the screen"
                                                          : "full screen",
                   w, h);
    }
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
    display_now(v->display, v->screen_resolution);
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

/* The DS buttons pressed since the last call (pause_taps), taken. */
static uint16_t take_pause_taps(void)
{
    uint16_t taps;
    if (input_lock == NULL) {
        return 0;
    }
    SDL_LockMutex(input_lock);
    taps = pause_taps;
    pause_taps = 0;
    SDL_UnlockMutex(input_lock);
    return taps;
}

/* The menu sounds of the HD movies' pause panel (the game's se_sys_*, settings_sample.h): opening,
 * the cursor moving, a choice made, backing out. Looked at once a pass of the loop. */
static void pause_panel_sounds(void)
{
    static int was_open, was_row;
    int row = 0, can_skip = 0;
    const int open = khdays_movie_pause_menu(&row, &can_skip);
    const int by_choice = khdays_movie_pause_closed_by_choice();
    if (open && !was_open) {
        khdays_menu_cue(KHDAYS_SETTINGS_CUE_OPEN);
    } else if (!open && was_open) {
        khdays_menu_cue(by_choice ? KHDAYS_SETTINGS_CUE_ACCEPT : KHDAYS_SETTINGS_CUE_BACK);
    } else if (open && row != was_row) {
        khdays_menu_cue(KHDAYS_SETTINGS_CUE_CURSOR);
    }
    was_open = open;
    was_row = row;
}

/* A day's monologue heard in the theater's voice (runtime/movies/scene_trace.c): the theater's
 * subtitles of what it says, at the voice's clock, over the game's page -- the video named by the
 * voice's clock, loaded when it changes. */
static void draw_voice_subtitles(SDL_Renderer *renderer)
{
    static char loaded[64] = "";
    char name[64] = "";
    double seconds = 0.0;
    int lw = 0, lh = 0;
    SDL_RendererLogicalPresentation mode;
    if (!khdays_subtitles_shown() || !khdays_hd_voice_clock(name, sizeof(name), &seconds)) {
        if (loaded[0] != '\0') {
            loaded[0] = '\0';
            khdays_movie_subtitles_load("", 0); /* none */
        }
        return;
    }
    if (strcmp(name, loaded) != 0) {
        const int chosen = khdays_language_choose(khdays_devices_profile()->language);
        snprintf(loaded, sizeof(loaded), "%s", name);
        khdays_movie_subtitles_load(strncmp(name, "hd", 2) == 0 ? name + 2 : name,
                                    chosen >= 1 && chosen <= 5 ? chosen : 1);
    }
    SDL_GetRenderLogicalPresentation(renderer, &lw, &lh, &mode);
    SDL_SetRenderLogicalPresentation(renderer, 1920, 1080, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    khdays_movie_subtitles_draw(renderer, seconds, 0.0, 1920, 1080);
    SDL_SetRenderLogicalPresentation(renderer, lw, lh, mode);
}

/* A port's screen with the swaying glove showed at the last picture (save, chapters, rooms, camp, shop). */
static int glove_screen;

/* The theater's music's lines (host/movies/theater_music.c) in the session log. */
static void theater_music_line(const char *line)
{
    khdays_log("theater music: %s", line);
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
            /* the player's (or the test run's, KHDAYS_USER_FOLDER) logs folder, not a fixed saves/logs: a test run
             * keeps its pictures with its own logs, where device_movie_test.sh fetches them */
            static char logs[256];
            SDL_snprintf(logs, sizeof(logs), "%s/logs", khdays_host_user_folder());
            shot_dir = logs;
        }
#endif
        if (shot_dir != NULL) {
            /* (a whole path: SDL on iOS would make a relative one in Application Support, fs_path.h) */
            khdays_fs_create_directory(shot_dir);
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
            !khdays_settings_is_open() && khdays_mobile_keys_visible() && !pad_lost ? "drawn" : "not drawn",
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
#ifdef KHDAYS_MOBILE
    {
        /* the HUD and what the window draws over the picture, each with whether it is inside the visible safe
         * area (the safe area, clear of the rounded corners: mobile_touch.h khdays_mobile_visible) */
        SDL_FRect vis, box;
        float corner = 0;
        if (khdays_mobile_visible(window, &vis, &corner)) {
            const int pieces = khdays_present_hud_pieces(), overlays = khdays_mobile_overlay_count();
            fprintf(stderr,
                    "picture %u: visible safe area %.0f,%.0f to %.0f,%.0f (window pixels, %.0f clear of the edges); "
                    "%d HUD pieces, %d overlays\n",
                    n, (double)vis.x, (double)vis.y, (double)(vis.x + vis.w), (double)(vis.y + vis.h), (double)corner,
                    pieces, overlays);
            for (int i = 0; i < pieces + overlays; ++i) {
                const char *name =
                    i < pieces ? khdays_present_hud_piece(i, &box) : khdays_mobile_overlay(i - pieces, &box);
                const int inside = box.x >= vis.x - 1 && box.y >= vis.y - 1 && box.x + box.w <= vis.x + vis.w + 1 &&
                                   box.y + box.h <= vis.y + vis.h + 1;
                fprintf(stderr, "picture %u: %s %s at %.0f,%.0f to %.0f,%.0f, %s\n", n,
                        i < pieces ? "hud piece" : "overlay", name != NULL ? name : "?", (double)box.x, (double)box.y,
                        (double)(box.x + box.w), (double)(box.y + box.h), inside ? "inside" : "OUTSIDE");
            }
        }
    }
#endif
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

/* An HD video that stays paused whatever else lets it go: one its course keeps paused, or one under the
 * Menu Overhaul's pause panel (as close_settings has it). */
static int video_kept_paused(void)
{
    int row, skip;
    return khdays_movie_hd_course_paused() || khdays_movie_pause_menu(&row, &skip);
}

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
        if (!video_kept_paused()) {
            khdays_hd_pause(0);
        }
        khdays_hd_voice_pause(0);
    }
    fprintf(stderr, "window: the app is %s: an HD video and a voice %s\n", away ? "in the background" : "back in front",
            away                             ? "wait"
            : settings_stopped               ? "still wait under the settings menu"
            : pad_lost                       ? "still wait for a gamepad"
            : video_kept_paused()            ? "go on (a video under its own pause stays paused)"
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
    if (lost || (!video_kept_paused() && !app_background)) {
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
    {
        /* the lines in the middle of what is seen (the visible safe area), each as wide as fits it */
        const SDL_FRect vis = khdays_mobile_visible_output(r);
        for (int i = 0; i < 2; ++i) {
            const float length = (float)SDL_strlen(lines[i]);
            float k = SDL_max(1.0f, SDL_floorf(vis.w / (8.0f * 60.0f))) * (i == 0 ? 1.5f : 1.0f);
            k = SDL_max(1.0f, SDL_min(k, SDL_floorf(vis.w / (8.0f * length + 16.0f))));
            const float x = vis.x + (vis.w - 8.0f * length * k) / 2.0f;
            const float y = vis.y + vis.h / 2.0f + (i == 0 ? -14.0f : 6.0f) * SDL_max(1.0f, vis.w / 480.0f);
            SDL_SetRenderScale(r, k, k);
            SDL_RenderDebugText(r, x / k, y / k, lines[i]);
            khdays_mobile_overlay_box(i == 0 ? "controller notice 0" : "controller notice 1",
                                      (SDL_FRect){x, y, 8.0f * length * k, 8.0f * k});
        }
    }
    (void)w;
    (void)h;
    SDL_SetRenderScale(r, 1.0f, 1.0f);
    SDL_SetRenderLogicalPresentation(r, lw, lh, presentation);
}
#endif

/* Draws the last frame, with the FPS and ping displays and the settings menu
 * over it. Draws nothing while the graphics card is lost (the loop recreates
 * it). */
static void redraw(SDL_Window *window, SDL_Renderer *renderer)
{
    int pictures, fps, corner_rows;
    char *ping;
    const Uint64 began = SDL_GetTicksNS();
    Uint64 drawn;
    last_redraw = SDL_GetTicks();
    if (khdays_present_lost()) {
        return;
    }
#ifdef KHDAYS_MOBILE
    khdays_mobile_overlay_clear(); /* (what this redraw draws over the picture, for the reports) */
#endif
#ifdef KHDAYS_MOBILE_TEST
    if (khdays_mobile_test_slow_ms() > 0) {
        /* the pad script's "!slow=MS": a phone too slow for its picture rate, each picture this much longer */
        SDL_DelayPrecise((Uint64)khdays_mobile_test_slow_ms() * 1000000u);
    }
#endif
    khdays_present_draw();
    {
        static int logging;
        if (!logging) {
            logging = 1;
            khdays_theater_music_log_to(theater_music_line);
            khdays_theater_music_overhaul(khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL));
        }
    }
    khdays_theater_music_poll(); /* the theater's music kept for a next scene's video, or let go */
    /* (the port's lists sway their glove: drawn at the display's rate while one shows, glove_screen) */
    glove_screen =
        khdays_save_screen_draw(renderer); /* the port's save list over the game's (runtime/menus/savelist.h) */
    glove_screen |=
        khdays_cine_screen_draw(renderer); /* MODO CINE's list as the HD theater's (runtime/menus/cinelist.h) */
    khdays_day_screen_draw(renderer); /* the day's card as the HD theater's chapter days (runtime/menus/daycard.h) */
    glove_screen |=
        khdays_room_screen_draw(renderer); /* the port's list of rooms over the game's (runtime/net/roomlist.h) */
    khdays_select_screen_draw(renderer);   /* Mission Mode's character select on one screen (charselect.h) */
    /* the Moogle Shop on one screen (shop.h): before the camp's, whose black comes over it when Mission
     * Mode's shop opens from its menu or closes back to it */
    glove_screen |= khdays_shop_screen_draw(renderer);
    glove_screen |= khdays_camp_screen_draw(renderer);      /* Mission Mode's camp menu on one screen (camp.h) */
    glove_screen |= khdays_shop_screen_draw_over(renderer); /* Mission Mode's shop and menu into each other */
    khdays_mod_screen_draw(renderer); /* a mod's screen, the title's mods' prompt (runtime/scripts/screens.h) */
    if (khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) && !khdays_movie_hd_showing()) {
        draw_voice_subtitles(renderer); /* a monologue's voice: what it says */
    }
    fps = khdays_present_fps(&pictures);
    if (fps) {
        khdays_settings_fps_draw(renderer, pictures);
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
    khdays_movie_pause_draw(renderer);
    khdays_settings_draw(renderer, window);
#ifdef KHDAYS_MOBILE
    if (pad_lost) {
        pad_lost_draw(renderer);
    } else if (!khdays_settings_is_open() && khdays_mobile_keys_visible()) {
        khdays_mobile_draw(renderer);
    }
#endif
    picture_report(window, renderer);
    drawn = SDL_GetTicksNS();
    present_picture(renderer);
    latency_presented();
    khdays_present_redraw_took(drawn - began, SDL_GetTicksNS() - drawn); /* (the shop's timings) */
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

/* Keeps Chase Camera's Free as the Config's page chose it (runtime/menus/camp.h): in the profile, the game's input
 * and the settings file. */
static void keep_chase_free(void)
{
    KhdaysProfile p;
    char path[512];
    int on;
    if (!khdays_camp_chase_free_chosen(&on) || khdays_devices_profile()->enh.chase_free == on) {
        return;
    }
    p = *khdays_devices_profile();
    p.enh.chase_free = on;
    khdays_devices_set_profile(&p);
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
    display_now(p.video.display, p.video.screen_resolution);
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

/* With the Menu Overhaul, whether the settings menu may open now: in the game's menus and pauses (a situation
 * the port does not know taken as one), not in the middle of play -- the field, Sora's memories, the DS's
 * movies, the day's card, the end -- as the one-screen picture decided last. With two screens: always. */
static int settings_allowed(void)
{
    static int refused_said;
    int allowed = 1;
    if (khdays_devices_profile()->video.one_screen) {
        switch (khdays_onescreen_scene()) {
        case KHDAYS_SIT_FIELD:
        case KHDAYS_SIT_FIELD_3D_BOTH:
        case KHDAYS_SIT_MOVIE:
        case KHDAYS_SIT_DAY_COUNTER:
        case KHDAYS_SIT_THOUGHTS:
        case KHDAYS_SIT_THE_END: allowed = 0; break;
        default: break;
        }
    }
    if (!allowed && !refused_said) {
        refused_said = 1;
        khdays_log("settings: not opened in the middle of play (%s); the game's menus and pauses open it",
                   khdays_situation_name(khdays_onescreen_scene()));
    }
    return allowed;
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
        {
            /* an HD video's pause still up (the menu opened from it), or one its course keeps paused: the
             * video stays paused; nor does it go on with the app in the background */
            int row, skip;
            if (!khdays_movie_hd_course_paused() && !khdays_movie_pause_menu(&row, &skip) && !app_background) {
                khdays_hd_pause(0);
            }
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
    int row, skip;
    const int menu = khdays_settings_is_open() || khdays_movie_pause_menu(&row, &skip);
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
static int opens_settings(const SDL_Event *event, SDL_Renderer *renderer)
{
    if (event->type == SDL_EVENT_KEY_DOWN && !event->key.repeat && event->key.scancode == SDL_SCANCODE_ESCAPE) {
        return 1;
    }
    if (event->type == SDL_EVENT_MOUSE_BUTTON_DOWN && event->button.button == SDL_BUTTON_LEFT &&
        ((khdays_present_settings_button() && khdays_present_settings_button_at(event->button.x, event->button.y)) ||
         khdays_movie_pause_settings_at(renderer, event->button.x, event->button.y))) {
        return 1; /* the picture's own settings key (the title's), or the one a pause shows */
    }
    return khdays_devices_take_settings_request();
}

/* The settings key pressed: the settings menu. With the Menu Overhaul, over an HD video or a monologue it opens
 * that mod's pause (as START does), and with the pause up the settings menu, the video staying paused under it;
 * elsewhere the settings menu where settings_allowed lets it. */
static void settings_key(void)
{
    int row, skip;
    if (!khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL)) {
        open_settings();
    } else if (khdays_movie_pause_menu(&row, &skip)) {
        open_settings();
    } else if (khdays_movie_hd_showing()) {
        khdays_movie_pause_toggle();
    } else if (khdays_monologue_running()) {
        khdays_monologue_pause_toggle(); /* a day's monologue: its pause first, as a movie's */
    } else if (settings_allowed()) {
        open_settings();
    }
}

/* Creates the window: the DS's two screens at 2x (one above the other, or
 * side by side), or with one picture the largest whole multiple of 342 x 192
 * (16:9) that fits in three quarters of the desktop. */
static SDL_Window *open_window(const KhdaysProfile *profile)
{
    const int beside = profile->video.two_screens == KHDAYS_TWO_SIDE_BY_SIDE;
    int w = beside ? 4 * W : 2 * W, h = beside ? 2 * H : 4 * H;
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
            char *text = (char *)khdays_fs_load_file(path, NULL);
            const int pad_said = text != NULL && (SDL_strstr(text, "[gamepad") != NULL || SDL_strstr(text, "preset") != NULL);
            const int aspect_said = text != NULL && SDL_strstr(text, "aspect") != NULL;
            const int resolution_said = text != NULL && SDL_strstr(text, "internal_resolution") != NULL;
            if (!pad_said && SDL_getenv("KHDAYS_MOBILE_PRESET_DS") == NULL) {
                khdays_profile_apply_preset(&profile, KHDAYS_PRESET_KH2, KHDAYS_AVAILABLE_ENHANCEMENTS);
                fprintf(stderr, "host: %s: the Kingdom Hearts II gamepad layout\n",
                        text == NULL ? "no settings file yet" : "the settings file names no gamepad layout");
            }
            if (!aspect_said) {
                profile.video.aspect = KHDAYS_ASPECT_WINDOW;
            }
            if (!resolution_said) {
                /* the 3D at a scale the phone holds at 120 pictures a second (profile.h's auto: 4 on the
                 * unfolded phone, 3 folded lying) instead of the window's 9 or 10, which ran at about 10; the
                 * picture rate itself is the picture pace's (host/render/picture_pace.h): 120 while it holds */
                profile.video.internal_resolution = KHDAYS_RESOLUTION_AUTO;
                fprintf(stderr, "host: %s: internal resolution auto (the 3D kept to about 1088 x 768)\n",
                        text == NULL ? "no settings file yet" : "the settings file names no internal resolution");
            }
            SDL_free(text);
            {
                /* Builds before the phone's faster default wrote "internal_resolution = window" into every
                 * settings file they made, so the default never reached an existing install. Once (a marker
                 * file beside the settings says it was done), that old value becomes auto; a "window" chosen
                 * in Settings afterwards is the player's and stays. */
                char marker[600];
                SDL_snprintf(marker, sizeof(marker), "%s.resolution-auto", path);
                if (!khdays_fs_get_path_info(marker, NULL)) {
                    if (resolution_said && profile.video.internal_resolution == KHDAYS_RESOLUTION_WINDOW &&
                        SDL_getenv("KHDAYS_KEEP_RESOLUTION_WINDOW") == NULL) {
                        profile.video.internal_resolution = KHDAYS_RESOLUTION_AUTO;
                        khdays_profile_save(&profile, path, notice);
                        fprintf(stderr, "host: the settings file said internal_resolution = window (an older build's "
                                        "default): changed to auto once\n");
                    }
                    khdays_fs_save_file(marker, "1\n", 2);
                }
            }
        }
#endif
        if (khdays_mod_installed(KHDAYS_MOD_WALK) && profile.enh.walk_speed == WALK_SPEED_DEFAULT) {
            /* the Walk mod's walk is timed for 55% of the game's speed: the setting set so once (a value of the
             * player's own, any but the port's default, kept) */
            profile.enh.walk_speed = WALK_SPEED_MOD;
            khdays_profile_save(&profile, path, notice);
        }
        khdays_ui_set_language(khdays_language_choose(profile.language));
        khdays_sound_output_set(&profile.sound);
        khdays_log_want(profile.session_log);
        khdays_savelist_set_settings(profile.save.all_files, profile.save.autosave);
        khdays_stop_set_enabled(profile.gameplay.stop_magic);     /* for this run: a change waits for the next */
        khdays_plugins_set_native_code(profile.mods.native_code); /* the same */
    }
    window = open_window(&profile);
    host_window = window;
    display_now(profile.video.display, profile.video.screen_resolution);
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
#if defined(KHDAYS_MOBILE_TEST) || defined(KHDAYS_IOS) /* (the phone: only with KHDAYS_PAD_SCRIPT in khdays-env.txt) */
    const int pad_script = khdays_pad_script_start();
#else
    const int pad_script = 0;
#endif
    khdays_settings_set_graphics_card(khdays_present_gpu());
    tell_game();
    khdays_updates_start(profile.network.check_updates); /* in the background; told of on the title */
    SDL_SetAtomicInt(&started, 1);
    SDL_SignalSemaphore(ready);

    latency_start();
    for (;;) {
        SDL_Event event;
        /* While the settings menu shows, it keeps animating even if the paused
         * game sends no frames. */
        int animating;
        int menu_row, menu_skip; /* the pause panel's (khdays_movie_pause_menu) */
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
        /* the DS's VBlank kept just before the screen's refresh (clock.h), but in a multiplayer session
         * (the players' games keep one time) or minimized; KHDAYS_SCREEN_LOCK=off for comparing */
        khdays_clock_lock_screen(
            !khdays_net_in_session() && !(SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) &&
            (SDL_getenv("KHDAYS_SCREEN_LOCK") == NULL || SDL_strcmp(SDL_getenv("KHDAYS_SCREEN_LOCK"), "off") != 0));
        khdays_devices_frame(); /* a wheel's notch let go */
#if defined(KHDAYS_MOBILE_TEST) || defined(KHDAYS_IOS)
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
        /* So, with the Menu Overhaul, are the game's pause drawn as KH2's (game_pause.h: the paused game
         * sends a picture a second or so, its PAUSE and glove move on), a port's screen with its swaying
         * glove (glove_screen) and a monologue's pause */
        const int monologue = !movie && khdays_monologue_running();
        const int kh2_pause = !movie && khdays_mod_installed(KHDAYS_MOD_MENU_OVERHAUL) &&
                              (khdays_game_pause_kh2_showing() || glove_screen ||
                               (monologue && khdays_movie_pause_menu(&menu_row, &menu_skip)));
        /* So is an animated backdrop (the title's, ui/composition.h): its pieces move while the
         * game's still menu sends no frames. */
        const int backdrop = khdays_present_backdrop_moving();
        /* So is a situation still settling on a still picture (a pause opened where the 3D was
         * on both screens): it is decided again as the game's VBlanks pass. */
        const int settling = khdays_present_settling();
        /* the smooth pace's pictures at the packages and the refreshes between, without VSync, while the
         * screen's lock holds (khdays_present_smooth_unblocked) */
        const int unblocked = smooth && khdays_present_smooth_unblocked();
        /* VSync only while the window draws on its own time; the game's pictures as they come without it */
        khdays_present_wait_for_refresh((smooth && !unblocked) || movie || backdrop || kh2_pause || settling ||
                                        animating || monologue);
        if (unblocked && khdays_present_between_due_ms(1) == 0 && khdays_present_refresh()) {
            redraw(window, renderer); /* a refresh between packages, on time while events keep coming */
        }
        keep_subtitles();
        keep_chase_free();
        if (!khdays_settings_is_open()) {
            khdays_cine_screen_buttons(khdays_cinelist_seen()); /* MODO CINE's chapters, the port's list */
        }
        {
            /* taken on every pass, so presses from before a movie never reach its pause */
            const uint16_t taps = take_pause_taps();
            if ((movie || monologue) && !khdays_settings_is_open()) {
                /* (a day's monologue too: with the Menu Overhaul its START opens the pause panel) */
                khdays_media_host_presses(khdays_host_buttons(), taps, monologue);
            }
        }
        pause_panel_sounds();
        if (kh2_pause && !khdays_settings_is_open()) {
            const Uint64 since = SDL_GetTicks() - last_redraw;
            if (since >= MOVIE_REDRAW_MS &&
                (since >= MOVIE_REDRAW_LATE_MS || !SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST))) {
                redraw(window, renderer);
            }
        }
        if (movie && !khdays_settings_is_open()) {
            khdays_movie_hd_poll();
            /* drawn on time even while events keep coming (a gamepad's stream of them): the game,
             * held for a scene's video, sends no frames that would draw it */
            /* but never one picture per event: a picture waits for the display's refresh (~16 ms), and a
             * gamepad's or a mouse's stream of events (hundreds a second, SDL sending a joystick's and a
             * gamepad's of each) then queued faster than one a picture could take them, the pause's
             * presses seconds -- minutes -- behind. The waiting events first, a picture at least every
             * MOVIE_REDRAW_LATE_MS however many keep coming. */
            const Uint64 since = SDL_GetTicks() - last_redraw;
            if (since >= MOVIE_REDRAW_MS &&
                (since >= MOVIE_REDRAW_LATE_MS || !SDL_HasEvents(SDL_EVENT_FIRST, SDL_EVENT_LAST))) {
                redraw(window, renderer);
            }
        }
        if (khdays_present_lost()) {
            /* the port noticed it itself (SDL's renderer does not on D3D12):
             * handle it like SDL's event */
            SDL_zero(event);
            event.type = SDL_EVENT_RENDER_DEVICE_LOST;
        } else if (!((smooth && !unblocked) || movie || backdrop || kh2_pause || pad_script
                         ? SDL_WaitEventTimeout(&event, 1)
                     : unblocked ? SDL_WaitEventTimeout(&event, khdays_present_between_due_ms(settling    ? 4
                                                                                             : animating ? 50
                                                                                                         : 100))
                     : settling  ? SDL_WaitEventTimeout(&event, 4) /* (a VBlank is ~17 ms) */
                     : animating ? SDL_WaitEventTimeout(&event, 50)
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
                (settling && khdays_present_refresh_settling()) || animating || movie || kh2_pause) {
                redraw(window, renderer);
            }
            continue;
        }
        mouse_follow(window);
        if (event.type == SDL_EVENT_QUIT) {
            if (khdays_settings_is_open() && khdays_settings_profile_changed()) {
                save_settings(); /* closed with the menu open: keep what was changed */
            }
            khdays_plugins_quit(1000); /* stopped on the game's thread, if it answers within a second */
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
            khdays_day_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_mod_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_ds_select_pages_renderer_gone();
            khdays_camp_screen_renderer_gone();
            khdays_shop_screen_renderer_gone();
            khdays_movie_pause_renderer_gone();
            khdays_modern_pause_renderer_gone();
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
                latency_shown = packages[host_slot]->frame;
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
            khdays_day_screen_renderer_gone();
            khdays_room_screen_renderer_gone();
            khdays_mod_screen_renderer_gone();
            khdays_select_screen_renderer_gone();
            khdays_ds_select_pages_renderer_gone();
            khdays_camp_screen_renderer_gone();
            khdays_shop_screen_renderer_gone();
            khdays_movie_pause_renderer_gone();
            khdays_modern_pause_renderer_gone();
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
#ifdef KHDAYS_MOBILE
        if (event.type == SDL_EVENT_FINGER_DOWN) {
            int count = 0;
            SDL_free(SDL_GetTouchFingers(event.tfinger.touchID, &count));
            int w, h;
            if (!pad_lost && !khdays_settings_is_open() && SDL_GetWindowSize(window, &w, &h)) {
                const float x = event.tfinger.x * w, y = event.tfinger.y * h;
                if ((khdays_mobile_keys_visible() && khdays_mobile_settings_at(window, x, y)) ||
                    (khdays_present_settings_button() && khdays_present_settings_button_at(x, y)) ||
                    khdays_movie_pause_settings_at(renderer, x, y)) {
                    settings_key();
                    redraw(window, renderer);
                    continue;
                }
            }
            if (count >= 3) {
                if (!khdays_settings_is_open()) {
                    settings_key();
                    redraw(window, renderer);
                    continue;
                }
                /* the three fingers are Escape: the menu must not go on following the second one (its lift
                 * would be a tap on whatever row is under it) */
                khdays_settings_touch_reset();
                SDL_zero(event);
                event.type = SDL_EVENT_KEY_DOWN;
                event.key.scancode = SDL_SCANCODE_ESCAPE;
            }
        }
#endif
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
        if (opens_settings(&event, renderer)) {
            settings_key();
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
        if (khdays_movie_pause_event(renderer, &event)) {
            /* The pause owns this click. Publishing it as a bound game A
             * would take the row the click just chose a second time. Not drawn here: the
             * movie's pictures above draw it (a picture for each of a mouse's moves left the pause
             * behind the player). */
            continue;
        }
        latency_pressed(&event);
        khdays_devices_handle(&event, renderer); /* keys, mouse, gamepads, focus */
        if (khdays_devices_take_pad_change()) {
            /* the playing gamepad changed: the phone's touch keys show without one and hide with one */
            fprintf(stderr, "host: gamepads: %d connected, %s%s; touch keys %s (game frame %u)\n",
                    khdays_devices_pad_count(), khdays_devices_gamepad() != NULL ? "playing: " : "none plays",
                    khdays_devices_gamepad() != NULL ? khdays_devices_gamepad_name() : "",
#ifdef KHDAYS_MOBILE
                    khdays_mobile_keys_visible() ? "shown" : "hidden",
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
            settings_key();
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
         * uses the enhancements (and their speeds) and whether mods' native code
         * runs from the settings file named by KHDAYS_SETTINGS, if any; the
         * regression walks name none, so they run with the defaults. */
        const char *path = SDL_getenv("KHDAYS_SETTINGS");
        if (path != NULL) {
            KhdaysProfile profile;
            KhdaysDeviceState nothing;
            khdays_profile_load(&profile, path, notice);
            khdays_plugins_set_native_code(profile.mods.native_code);
            khdays_character_shadows_publish(profile.enh.character_shadows);
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

void khdays_host_shot_soon(void)
{
    khdays_present_shot_soon();
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

void khdays_host_movement(int *x, int *y)
{
    KhdaysInputSnapshot now;
    khdays_host_input(&now);
    *x = now.move_x;
    *y = now.move_y;
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
