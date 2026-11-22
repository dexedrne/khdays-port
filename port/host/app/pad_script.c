/* The Linux test build's emulated gamepads (pad_script.h): SDL's virtual gamepads, attached, pressed
 * and pulled out by a script as the game's frames pass, so that what a test proves goes the whole way
 * a real controller's input does -- SDL's gamepad events, devices.c, the player's bindings, the game.
 * Window's thread only. Never in a release (CMake's KHDAYS_MOBILE_TEST); in the phone's own build too, where
 * it does nothing unless Documents/khdays-env.txt names KHDAYS_PAD_SCRIPT (a test launch: MAC-STEPS.md, "The
 * HD movie by a pad script"). On the phone a real controller keeps working beside the script's, and the
 * test-only steps (!safe, !thermal, !slow) are said and skipped. */
#include "pad_script.h"

#include "devices.h"
#include "mobile_touch.h"
#include "profile.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { PADS = 4 };

typedef struct Step {
    int anchor;     /* the index of the anchor its frame counts from, or -1: the game's start */
    unsigned frame; /* VBlanks after the anchor began */
    int pad;        /* 0..PADS-1 */
    char op;        /* + - > * / ! */
    char what[32];
} Step;

typedef struct Anchor {
    char type; /* 's': scene `value` becomes current; 't': the title's menu shows */
    unsigned value;
    unsigned start; /* the VBlank it began at, plus 1; 0 not yet */
} Anchor;

static Step *steps;
static int step_count, step_next;
static Anchor anchors[64];
static int anchor_count;
static struct {
    SDL_JoystickID id;
    SDL_Joystick *joystick;
    Uint32 mask; /* the gamepad buttons it has */
} pads[PADS];
static unsigned previous_scene = 0xffffffffu;

_Noreturn static void bad(const char *item)
{
    fprintf(stderr, "KHDAYS_PAD_SCRIPT: cannot read \"%s\"\n", item);
    exit(2);
}

int khdays_pad_script_start(void)
{
    const char *text = SDL_getenv("KHDAYS_PAD_SCRIPT");
    char *copy, *item, *rest = NULL;
    int anchor = -1;
    if (text == NULL || steps != NULL) {
        return steps != NULL;
    }
    /* the test's hidden window never has the focus, and SDL keeps a gamepad's events from a window
     * without it */
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#ifndef KHDAYS_IOS
    khdays_devices_only_virtual(1); /* not a real controller plugged into the test's machine */
#endif
    copy = SDL_strdup(text);
    steps = (Step *)SDL_calloc(strlen(text) / 2 + 1, sizeof(Step));
    for (item = SDL_strtok_r(copy, " \n\t", &rest); item != NULL; item = SDL_strtok_r(NULL, " \n\t", &rest)) {
        Step step;
        char *end;
        if (item[0] == 's' || item[0] == 't') {
            if (anchor_count == (int)SDL_arraysize(anchors)) {
                bad(item);
            }
            anchors[anchor_count].type = item[0];
            anchors[anchor_count].value = item[0] == 's' ? (unsigned)strtoul(item + 1, &end, 10) : 0u;
            anchors[anchor_count].start = 0;
            anchor = anchor_count++;
            continue;
        }
        memset(&step, 0, sizeof(step));
        step.anchor = anchor;
        step.frame = (unsigned)strtoul(item, &end, 10);
        if (end == item) {
            bad(item);
        }
        if (*end == '#') {
            step.pad = (int)strtol(end + 1, &end, 10) - 1;
            if (step.pad < 0 || step.pad >= PADS) {
                bad(item);
            }
        }
        step.op = *end;
        if (step.op == '\0' || strchr("+->*/!", step.op) == NULL) {
            bad(item);
        }
        SDL_strlcpy(step.what, end + 1, sizeof(step.what));
        steps[step_count++] = step;
    }
    SDL_free(copy);
    fprintf(stderr, "pad script: %d steps\n", step_count);
    return 1;
}

/* An emulated gamepad of a kind: ps5 (a DualSense: Sony's ids, every button), switchpro (Nintendo's
 * ids; no touch pad, Capture on misc1), switchpro-nocapture (as the phone may give it: neither),
 * xbox. */
static void attach(int pad, const char *kind)
{
    SDL_VirtualJoystickDesc desc;
    Uint32 mask = (1u << SDL_GAMEPAD_BUTTON_COUNT) - 1;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT; /* in SDL_GamepadButton order */
    for (int b = SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1; b < SDL_GAMEPAD_BUTTON_COUNT; ++b) {
        if (b != SDL_GAMEPAD_BUTTON_TOUCHPAD) {
            mask &= ~(1u << b); /* no paddles, no extra buttons */
        }
    }
    if (SDL_strcmp(kind, "ps5") == 0) {
        desc.vendor_id = 0x054c;
        desc.product_id = 0x0ce6;
        desc.name = "DualSense Wireless Controller";
        mask &= ~(1u << SDL_GAMEPAD_BUTTON_MISC1); /* (its microphone button is not a button of the game's) */
    } else if (SDL_strncmp(kind, "switchpro", 9) == 0) {
        desc.vendor_id = 0x057e;
        desc.product_id = 0x2009;
        desc.name = "Nintendo Switch Pro Controller";
        mask &= ~(1u << SDL_GAMEPAD_BUTTON_TOUCHPAD);
        if (SDL_strcmp(kind, "switchpro-nocapture") == 0) {
            desc.product_id = 0x3009; /* (ids of its own: SDL keeps a gamepad's mapping by them) */
            desc.name = "Nintendo Switch Pro Controller (no Capture)";
            mask &= ~(1u << SDL_GAMEPAD_BUTTON_MISC1);
        }
    } else if (SDL_strcmp(kind, "xbox") == 0) {
        desc.vendor_id = 0x045e;
        desc.product_id = 0x0b13;
        desc.name = "Xbox Wireless Controller";
        mask &= ~((1u << SDL_GAMEPAD_BUTTON_TOUCHPAD) | (1u << SDL_GAMEPAD_BUTTON_MISC1));
    } else {
        bad(kind);
    }
    desc.button_mask = mask;
    if (pads[pad].joystick != NULL) {
        return;
    }
    pads[pad].mask = mask;
    pads[pad].id = SDL_AttachVirtualJoystick(&desc);
    pads[pad].joystick = pads[pad].id != 0 ? SDL_OpenJoystick(pads[pad].id) : NULL;
    if (pads[pad].joystick == NULL) {
        fprintf(stderr, "pad script: cannot attach a %s: %s\n", kind, SDL_GetError());
        exit(2);
    }
}

static void detach(int pad)
{
    if (pads[pad].joystick != NULL) {
        SDL_CloseJoystick(pads[pad].joystick);
        SDL_DetachVirtualJoystick(pads[pad].id);
        pads[pad].joystick = NULL;
    }
}

static SDL_Window *script_window(void);

static void window_event(Uint32 type)
{
    SDL_Event event;
    SDL_zero(event);
    event.type = type;
    if (type >= SDL_EVENT_WINDOW_FIRST && type <= SDL_EVENT_WINDOW_LAST) {
        event.window.windowID = SDL_GetWindowID(script_window()); /* (SDL's renderer follows its own window's) */
    }
    SDL_PushEvent(&event);
}

static int report_asked;

int khdays_pad_script_take_report(void)
{
    const int asked = report_asked;
    report_asked = 0;
    return asked;
}

/* The script's window (the game has one). */
static SDL_Window *script_window(void)
{
    int count = 0;
    SDL_Window **all = SDL_GetWindows(&count);
    SDL_Window *window = all != NULL && count > 0 ? all[0] : NULL;
    SDL_free(all);
    if (window == NULL) {
        fprintf(stderr, "pad script: no window to change\n");
        exit(2);
    }
    return window;
}

static void run(const Step *s)
{
    SDL_Joystick *joystick = pads[s->pad].joystick;
    switch (s->op) {
    case '*': attach(s->pad, s->what); return;
    case '/': detach(s->pad); return;
    case '!':
        /* the phone's app sent to the background (the phone asleep, another app) and back, as SDL
         * tells it on iOS (SDL_OnApplicationWillEnterBackground and its fellows): the window minimized
         * and its focus gone between the lifecycle events, restored and focused again at the return.
         * SDL's renderer presents nothing between "minimized" and "restored", as on the phone. */
        if (SDL_strcmp(s->what, "background") == 0) {
            window_event(SDL_EVENT_WINDOW_MINIMIZED);
            window_event(SDL_EVENT_WINDOW_FOCUS_LOST);
            window_event(SDL_EVENT_WILL_ENTER_BACKGROUND);
            window_event(SDL_EVENT_DID_ENTER_BACKGROUND);
        } else if (SDL_strcmp(s->what, "foreground") == 0) {
            window_event(SDL_EVENT_WILL_ENTER_FOREGROUND);
            window_event(SDL_EVENT_WINDOW_RESTORED);
            window_event(SDL_EVENT_WINDOW_FOCUS_GAINED);
            window_event(SDL_EVENT_DID_ENTER_FOREGROUND);
        } else if (SDL_strncmp(s->what, "size=", 5) == 0) {
            /* a turned phone: SDL sends the window's size and pixel-size events itself */
            int w = 0, h = 0;
            if (SDL_sscanf(s->what + 5, "%dx%d", &w, &h) != 2 || w <= 0 || h <= 0) {
                bad(s->what);
            }
            if (!SDL_SetWindowSize(script_window(), w, h)) {
                fprintf(stderr, "pad script: the window did not take %dx%d: %s\n", w, h, SDL_GetError());
                exit(2);
            }
        } else if (SDL_strncmp(s->what, "safe=", 5) == 0) {
            int l = 0, t = 0, r = 0, b = 0;
            SDL_Event event;
            if (SDL_sscanf(s->what + 5, "%d,%d,%d,%d", &l, &t, &r, &b) != 4) {
                bad(s->what);
            }
#ifdef KHDAYS_MOBILE_TEST
            khdays_mobile_test_insets(l, t, r, b);
#else
            fprintf(stderr, "pad script: !%s is for the Linux test build: the phone's own safe area stays\n", s->what);
            return;
#endif
            SDL_zero(event);
            event.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
            event.window.windowID = SDL_GetWindowID(script_window());
            SDL_PushEvent(&event);
        } else if (SDL_strncmp(s->what, "touch=", 6) == 0) {
            /* a finger put down at a place of the window (in its points) and lifted again: a tap */
            int x = 0, y = 0, w = 0, h = 0;
            SDL_Event event;
            if (SDL_sscanf(s->what + 6, "%d,%d", &x, &y) != 2 || !SDL_GetWindowSize(script_window(), &w, &h) || w <= 0 ||
                h <= 0) {
                bad(s->what);
            }
            SDL_zero(event);
            event.type = SDL_EVENT_FINGER_DOWN;
            event.tfinger.windowID = SDL_GetWindowID(script_window());
            event.tfinger.touchID = 1;
            event.tfinger.fingerID = 1;
            event.tfinger.x = (float)x / (float)w;
            event.tfinger.y = (float)y / (float)h;
            event.tfinger.pressure = 1.0f;
            SDL_PushEvent(&event);
            event.type = SDL_EVENT_FINGER_UP;
            event.tfinger.pressure = 0.0f;
            SDL_PushEvent(&event);
        } else if (SDL_strncmp(s->what, "thermal=", 8) == 0) {
            /* the phone's thermal state from now on (0 nominal .. 3 critical): the picture rate follows it */
#ifdef KHDAYS_MOBILE_TEST
            khdays_mobile_test_thermal(SDL_atoi(s->what + 8));
#else
            fprintf(stderr, "pad script: !%s is for the Linux test build: skipped\n", s->what);
#endif
        } else if (SDL_strncmp(s->what, "slow=", 5) == 0) {
            /* each picture made this many milliseconds longer from now on: a phone too slow for its rate */
#ifdef KHDAYS_MOBILE_TEST
            khdays_mobile_test_slow(SDL_atoi(s->what + 5));
#else
            fprintf(stderr, "pad script: !%s is for the Linux test build: skipped\n", s->what);
#endif
        } else if (SDL_strcmp(s->what, "report") == 0) {
            report_asked = 1;
        } else {
            bad(s->what);
        }
        return;
    default: break;
    }
    if (joystick == NULL) {
        fprintf(stderr, "pad script: pad %d is not attached for %c%s\n", s->pad + 1, s->op, s->what);
        exit(2);
    }
    if (s->op == '>') {
        static const struct {
            const char *name;
            SDL_GamepadAxis axis;
        } axes[] = {{"lx", SDL_GAMEPAD_AXIS_LEFTX},        {"ly", SDL_GAMEPAD_AXIS_LEFTY},
                    {"rx", SDL_GAMEPAD_AXIS_RIGHTX},       {"ry", SDL_GAMEPAD_AXIS_RIGHTY},
                    {"lt", SDL_GAMEPAD_AXIS_LEFT_TRIGGER}, {"rt", SDL_GAMEPAD_AXIS_RIGHT_TRIGGER}};
        const char *colon = strchr(s->what, ':');
        for (size_t a = 0; colon != NULL && a < SDL_arraysize(axes); ++a) {
            if (SDL_strncmp(s->what, axes[a].name, (size_t)(colon - s->what)) == 0 &&
                strlen(axes[a].name) == (size_t)(colon - s->what)) {
                int v = SDL_atoi(colon + 1);
                if (axes[a].axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER || axes[a].axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) {
                    /* a virtual trigger's raw travel is the whole axis: its rest is the lowest value */
                    v = SDL_clamp(v, 0, 32767) * 2 - 32768;
                }
                SDL_SetJoystickVirtualAxis(joystick, axes[a].axis, (Sint16)v);
                return;
            }
        }
        bad(s->what);
    } else {
        /* a button by its position's name, as the settings file writes it (profile.c: south, east,
         * leftshoulder, touchpad, misc1, start, back ...); the triggers are axes */
        const int input = khdays_pad_input_from_name(s->what);
        const int down = s->op == '+';
        if (input == KHDAYS_PAD_LEFTTRIGGER || input == KHDAYS_PAD_RIGHTTRIGGER) {
            SDL_SetJoystickVirtualAxis(joystick,
                                       input == KHDAYS_PAD_LEFTTRIGGER ? SDL_GAMEPAD_AXIS_LEFT_TRIGGER
                                                                       : SDL_GAMEPAD_AXIS_RIGHT_TRIGGER,
                                       down ? SDL_JOYSTICK_AXIS_MAX : SDL_JOYSTICK_AXIS_MIN);
        } else if (input >= 0 && khdays_devices_sdl_button(input) != SDL_GAMEPAD_BUTTON_INVALID) {
            /* SDL numbers a virtual gamepad's buttons in SDL_GamepadButton order, counting only those
             * it has */
            const Uint32 bit = 1u << khdays_devices_sdl_button(input);
            int raw = 0;
            if (!(pads[s->pad].mask & bit)) {
                fprintf(stderr, "pad script: pad %d has no %s\n", s->pad + 1, s->what);
                exit(2);
            }
            for (Uint32 b = 1; b < bit; b <<= 1) {
                raw += (pads[s->pad].mask & b) != 0;
            }
            SDL_SetJoystickVirtualButton(joystick, (Uint8)raw, down != 0);
        } else {
            bad(s->what);
        }
    }
}

void khdays_pad_script_frame(unsigned frame, unsigned scene, int title_shown)
{
    int ran = 0;
    if (steps == NULL) {
        return;
    }
    for (int i = 0; i < anchor_count; ++i) {
        Anchor *a = &anchors[i];
        if (a->start != 0) {
            continue;
        }
        /* the anchors begin in the script's order: only the first that has not begun can */
        if ((a->type == 's' && a->value == scene && previous_scene != scene) || (a->type == 't' && title_shown)) {
            a->start = frame + 1;
            fprintf(stderr, "pad script: frame %u: anchor %c%u begins\n", frame, a->type, a->value);
        }
        break;
    }
    previous_scene = scene;
    while (step_next < step_count) {
        const Step *s = &steps[step_next];
        if (s->anchor < 0 ? s->frame > frame
                          : anchors[s->anchor].start == 0 || anchors[s->anchor].start - 1 + s->frame > frame) {
            break;
        }
        ++step_next;
        fprintf(stderr, "pad script: frame %u (scene %u): pad %d %c%s\n", frame, scene, s->pad + 1, s->op, s->what);
        run(s);
        /* SDL reads a virtual gamepad's state when it updates the joysticks, which a busy loop (events
         * always waiting) does rarely: at once, or a press and its release could both pass unseen */
        SDL_UpdateJoysticks();
        ran = 1;
    }
    if (ran) {
        fflush(stderr);
        if (step_next == step_count) {
            fprintf(stderr, "pad script: done at frame %u\n", frame);
            fflush(stderr);
        }
    }
}
