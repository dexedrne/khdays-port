/* The HD video's shared side (host/movies/movie_hd_core.c: the player every system but Windows uses
 * once it has a decoder) against a decoder that makes its pictures and sound up, with known times, and
 * SDL's dummy audio driver, which plays at the real pace: the first picture waiting, the start, the
 * pictures due in order on the sound's clock, a part of a file, the fades, the pause, the volume, the
 * end, a failure part-way, the closing and reopening, a voice of two files, and the colours. Then what
 * a wrong clock or hand-over would get past those: sound longer than the pictures, a pause before the
 * start, a decoder slower than the video, one that has nothing for a while (an interrupted reader's
 * "later"), the sound's device stopped behind the player's back (the clock must stand with it), the
 * clock told to the theater's music (never back), an output latency, and a closing from another
 * thread while this one takes pictures. */
#include "../host/movies/movie_hd.h"
#include "../host/movies/movie_hd_decoder.h"
#include "../host/movies/theater_music.h"

#include <SDL3/SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;
#define CHECK(c)                                                                                                       \
    do {                                                                                                               \
        if (!(c)) {                                                                                                    \
            fprintf(stderr, "movie_hd_core_test:%d: failed: %s\n", __LINE__, #c);                                      \
            ++failures;                                                                                                \
        }                                                                                                              \
    } while (0)

/* --- The clips the decoder makes up --------------------------------------------------------- */

typedef struct Clip {
    const char *name;
    int width, height; /* as decoded; the core shows the even part */
    double fps, seconds;
    double says; /* the length the file says it has; 0: `seconds` */
    int channels, rate;
    float level;         /* every sample of its sound */
    int fail_picture;    /* the picture that cannot be decoded; -1 none */
    int fail_sound;      /* the sound block that cannot be decoded; -1 none */
    int colours;         /* its pictures: white, red, black and grey quarters instead of the frame's number */
    double sound_seconds; /* how long its sound lasts; 0: as long as its pictures */
    int delay_ms;         /* how long each picture takes to decode */
    int later_before;     /* before this picture and this sound block (counted from 1), the decoder has
                             nothing LATER_TIMES times (KHDAYS_HD_DECODER_LATER); 0 never */
} Clip;
#define LATER_TIMES 12

#define KEY_EVERY 10     /* a key picture every so many: a part begins at the one before it */
#define SOUND_BLOCK 1024 /* frames a block */
#define PADDING 24       /* bytes of each row past the picture's width, filled with 0xEE */

static const Clip clips[] = {
    {"one", 64, 36, 30.0, 1.0, 0.0, 2, 48000, 0.5f, -1, -1, 0},
    {"two", 64, 36, 30.0, 2.0, 0.0, 2, 48000, 0.5f, -1, -1, 0},
    {"odd", 67, 39, 25.0, 0.4, 0.0, 1, 22050, 0.5f, -1, -1, 0},
    {"breaks", 64, 36, 30.0, 1.0, 0.0, 2, 48000, 0.5f, 12, -1, 0},
    {"silent", 64, 36, 30.0, 0.5, 0.0, 0, 0, 0.0f, -1, -1, 0},
    {"soundbreaks", 64, 36, 30.0, 0.6, 0.0, 2, 48000, 0.5f, -1, 5, 0},
    {"short", 64, 36, 30.0, 0.4, 3.0, 2, 48000, 0.5f, -1, -1, 0},
    {"colours", 64, 36, 30.0, 0.3, 0.0, 2, 48000, 0.5f, -1, -1, 1},
    {"voice-a", 0, 0, 0.0, 0.4, 0.0, 1, 22050, 0.25f, -1, -1, 0},
    {"voice-b", 0, 0, 0.0, 0.4, 0.0, 2, 48000, -0.25f, -1, -1, 0},
    {"voice-long", 0, 0, 0.0, 1.5, 0.0, 2, 48000, 0.125f, -1, -1, 0},
    {"longsound", 64, 36, 30.0, 0.5, 0.0, 2, 48000, 0.5f, -1, -1, 0, 0.8, 0, 0},
    {"slow", 64, 36, 30.0, 1.0, 0.0, 2, 48000, 0.5f, -1, -1, 0, 0.0, 50, 0},
    {"later", 64, 36, 30.0, 1.0, 0.0, 2, 48000, 0.5f, -1, -1, 0, 0.0, 0, 9},
    {"laterfirst", 64, 36, 30.0, 0.5, 0.0, 2, 48000, 0.5f, -1, -1, 0, 0.0, 0, 1},
    {"voice-later", 0, 0, 0.0, 0.4, 0.0, 2, 48000, 0.25f, -1, -1, 0, 0.0, 0, 3},
};

struct KhdaysHdDecoder {
    const Clip *clip;
    int what;
    int next_picture;
    long long next_frame; /* of its sound */
    int blocks;
    int later_left; /* how many more times it has nothing, where the clip says so */
    uint8_t *luma, *chroma;
    float *samples;
};

/* what the decoders were asked, in order (any thread) */
static SDL_Mutex *events_lock;
static char events[64][48];
static int event_count;
static SDL_AtomicInt open_decoders;
static double asked_from, asked_until; /* the last opening's range */
static SDL_AtomicInt laters;           /* how many times a decoder said it had nothing now */
static SDL_AtomicInt latency_ms;       /* what khdays_hd_decoder_output_latency says, in milliseconds */
static SDL_AtomicInt latency_asked;

static void event(const char *what, const char *name)
{
    SDL_LockMutex(events_lock);
    if (event_count < 64) {
        snprintf(events[event_count++], sizeof(events[0]), "%s %s", what, name);
    }
    SDL_UnlockMutex(events_lock);
}

static int event_index(const char *text)
{
    int found = -1;
    SDL_LockMutex(events_lock);
    for (int i = 0; i < event_count && found < 0; ++i) {
        if (strcmp(events[i], text) == 0) {
            found = i;
        }
    }
    SDL_UnlockMutex(events_lock);
    return found;
}

static void events_clear(void)
{
    SDL_LockMutex(events_lock);
    event_count = 0;
    SDL_UnlockMutex(events_lock);
}

KhdaysHdDecoder *khdays_hd_decoder_open(const char *path, int what, double from, double until, KhdaysHdFormat *format,
                                        char *why, size_t size)
{
    const Clip *clip = NULL;
    KhdaysHdDecoder *d;
    for (size_t i = 0; i < sizeof(clips) / sizeof(clips[0]); ++i) {
        if (strcmp(clips[i].name, path) == 0) {
            clip = &clips[i];
        }
    }
    if (clip == NULL) {
        snprintf(why, size, "the made-up decoder has no clip %s", path);
        return NULL;
    }
    if (what == KHDAYS_HD_DECODE_PICTURES && clip->width == 0) {
        snprintf(why, size, "the clip has no picture");
        return NULL;
    }
    if (what == KHDAYS_HD_DECODE_SOUND && clip->channels == 0) {
        snprintf(why, size, "the clip has no sound");
        return NULL;
    }
    d = (KhdaysHdDecoder *)calloc(1, sizeof(*d));
    if (d == NULL) {
        snprintf(why, size, "no memory");
        return NULL;
    }
    d->clip = clip;
    d->what = what;
    d->later_left = clip->later_before > 0 ? LATER_TIMES : 0;
    memset(format, 0, sizeof(*format));
    format->duration = clip->says > 0.0 ? clip->says : SDL_max(clip->seconds, clip->sound_seconds);
    asked_from = from;
    asked_until = until;
    if (what == KHDAYS_HD_DECODE_PICTURES) {
        const int at = (int)floor(from * clip->fps + 1e-6);
        d->next_picture = at / KEY_EVERY * KEY_EVERY; /* the key picture before the part: earlier than asked */
        d->luma = (uint8_t *)malloc((size_t)(clip->width + PADDING) * (size_t)clip->height);
        d->chroma = (uint8_t *)malloc((size_t)(clip->width + 1 + PADDING) * (size_t)(clip->height / 2 + 1));
        format->width = clip->width;
        format->height = clip->height;
        format->fps = clip->fps;
        event("open pictures", path);
    } else {
        const long long at = (long long)(from * clip->rate);
        d->next_frame = at / SOUND_BLOCK * SOUND_BLOCK; /* a whole block: earlier than asked */
        d->samples = (float *)malloc(sizeof(float) * SOUND_BLOCK * (size_t)clip->channels);
        format->channels = clip->channels;
        format->rate = clip->rate;
        event("open sound", path);
    }
    SDL_AddAtomicInt(&open_decoders, 1);
    return d;
}

int khdays_hd_decoder_picture(KhdaysHdDecoder *d, KhdaysHdDecoded *out, char *why, size_t size)
{
    const Clip *c = d->clip;
    const int total = (int)(c->seconds * c->fps + 0.5);
    const int luma_pitch = c->width + PADDING, chroma_pitch = c->width + 1 + PADDING;
    const int index = d->next_picture;
    if (index == c->fail_picture) {
        snprintf(why, size, "made-up failure at picture %d", index);
        return -1;
    }
    if (index >= total) {
        return 0; /* it does not stop at the part's end: the core does */
    }
    if (index == c->later_before - 1 && d->later_left > 0) {
        --d->later_left;
        SDL_AddAtomicInt(&laters, 1);
        return KHDAYS_HD_DECODER_LATER;
    }
    if (c->delay_ms > 0) {
        SDL_Delay((Uint32)c->delay_ms);
    }
    memset(d->luma, 0xEE, (size_t)luma_pitch * (size_t)c->height);
    memset(d->chroma, 0xEE, (size_t)chroma_pitch * (size_t)(c->height / 2 + 1));
    for (int y = 0; y < c->height; ++y) {
        for (int x = 0; x < c->width; ++x) {
            uint8_t luma = (uint8_t)(16 + index % 200), u = 100, v = 160;
            if (c->colours) {
                const int right = x >= c->width / 2, low = y >= c->height / 2;
                luma = !right && !low ? 235 : right && !low ? 63 : !right && low ? 16 : 126;
                u = right && !low ? 102 : 128;
                v = right && !low ? 240 : 128;
            }
            d->luma[y * luma_pitch + x] = luma;
            if (y / 2 < c->height / 2 + 1) {
                d->chroma[(y / 2) * chroma_pitch + (x & ~1)] = u;
                d->chroma[(y / 2) * chroma_pitch + (x & ~1) + 1] = v;
            }
        }
    }
    out->luma = d->luma;
    out->chroma = d->chroma;
    out->luma_pitch = luma_pitch;
    out->chroma_pitch = chroma_pitch;
    out->width = c->width;
    out->height = c->height;
    out->time = (double)index / c->fps;
    d->next_picture = index + 1;
    return 1;
}

int khdays_hd_decoder_sound(KhdaysHdDecoder *d, float **samples, int *frames, double *time, char *why, size_t size)
{
    const Clip *c = d->clip;
    const long long total = (long long)((c->sound_seconds > 0.0 ? c->sound_seconds : c->seconds) * c->rate + 0.5);
    long long n = total - d->next_frame;
    if (d->blocks == c->fail_sound) {
        snprintf(why, size, "made-up failure at sound block %d", d->blocks);
        return -1;
    }
    if (n <= 0) {
        return 0;
    }
    if (d->blocks == c->later_before - 1 && d->later_left > 0) {
        --d->later_left;
        SDL_AddAtomicInt(&laters, 1);
        return KHDAYS_HD_DECODER_LATER;
    }
    if (n > SOUND_BLOCK) {
        n = SOUND_BLOCK;
    }
    for (long long i = 0; i < n * c->channels; ++i) {
        d->samples[i] = c->level;
    }
    *samples = d->samples;
    *frames = (int)n;
    *time = (double)d->next_frame / c->rate;
    d->next_frame += n;
    ++d->blocks;
    return 1;
}

void khdays_hd_decoder_close(KhdaysHdDecoder *d)
{
    event(d->what == KHDAYS_HD_DECODE_PICTURES ? "close pictures" : "close sound", d->clip->name);
    SDL_AddAtomicInt(&open_decoders, -1);
    free(d->luma);
    free(d->chroma);
    free(d->samples);
    free(d);
}

double khdays_hd_decoder_output_latency(double buffer)
{
    (void)buffer;
    SDL_AddAtomicInt(&latency_asked, 1);
    return SDL_GetAtomicInt(&latency_ms) / 1000.0;
}

/* --- The theater's music, as the player drives it (theater_music.h) ------------------------------- */

static SDL_AtomicInt music_opens, music_ticks, music_closes, music_started_ticks, music_paused_ticks;
static SDL_AtomicInt music_last_follows;
static char music_path[64];
/* the video's clock as the music is told it, for the video open now (under music_lock) */
static SDL_Mutex *music_lock;
static double music_seconds; /* told last */
static int music_told;       /* times told since the opening */
static int music_went_back;  /* a telling earlier than the one before it: never, whatever the clock is on */

void khdays_theater_music_open(const char *video_path)
{
    snprintf(music_path, sizeof(music_path), "%s", video_path);
    SDL_AddAtomicInt(&music_opens, 1);
}

static double music_clock(void)
{
    double seconds;
    SDL_LockMutex(music_lock);
    seconds = music_told > 0 ? music_seconds : -1.0;
    SDL_UnlockMutex(music_lock);
    return seconds;
}

void khdays_theater_music_tick(double seconds, int started, int paused)
{
    SDL_LockMutex(music_lock);
    if (music_told > 0 && seconds < music_seconds - 1e-9) {
        if (!music_went_back) {
            fprintf(stderr, "movie_hd_core_test: the clock told to the music went back: %.6f s after %.6f s (%s)\n",
                    seconds, music_seconds, music_path);
        }
        music_went_back = 1;
    }
    music_seconds = seconds;
    ++music_told;
    SDL_UnlockMutex(music_lock);
    SDL_AddAtomicInt(&music_ticks, 1);
    if (started) {
        SDL_AddAtomicInt(&music_started_ticks, 1);
    }
    if (paused) {
        SDL_AddAtomicInt(&music_paused_ticks, 1);
    }
}

void khdays_theater_music_close(int next_follows)
{
    SDL_LockMutex(music_lock);
    music_told = 0; /* the next video's clock begins anew */
    SDL_UnlockMutex(music_lock);
    SDL_AddAtomicInt(&music_closes, 1);
    SDL_SetAtomicInt(&music_last_follows, next_follows);
}

void khdays_theater_music_set_gain(float gain)
{
    (void)gain;
}

/* --- The sound the player gave its streams (the core's tap) ------------------------------------ */

typedef struct Heard {
    long long frames;
    double first_time, last_end;
    float first, last, at_mark; /* its first and last samples; the one at `mark` seconds */
    double mark;
    int blocks;
} Heard;
static SDL_Mutex *heard_lock;
static Heard heard[2];
static int voice_order[64], voice_orders; /* each voice block's rate, in order */

static void tap(int voice, const float *samples, int frames, int channels, int rate, double time)
{
    Heard *h = &heard[voice != 0];
    SDL_LockMutex(heard_lock);
    if (h->frames == 0) {
        h->first_time = time;
        h->first = samples[0];
    }
    if (h->mark >= time && h->mark < time + (double)frames / rate) {
        h->at_mark = samples[(size_t)((h->mark - time) * rate) * (size_t)channels];
    }
    h->frames += frames;
    h->last = samples[(size_t)(frames - 1) * (size_t)channels];
    h->last_end = time + (double)frames / rate;
    ++h->blocks;
    if (voice && voice_orders < 64) {
        voice_order[voice_orders++] = rate;
    }
    SDL_UnlockMutex(heard_lock);
}

static Heard heard_now(int voice)
{
    Heard h;
    SDL_LockMutex(heard_lock);
    h = heard[voice != 0];
    SDL_UnlockMutex(heard_lock);
    return h;
}

static void heard_clear(double mark)
{
    SDL_LockMutex(heard_lock);
    memset(heard, 0, sizeof(heard));
    heard[0].mark = heard[1].mark = mark;
    voice_orders = 0;
    SDL_UnlockMutex(heard_lock);
}

/* --- The course ---------------------------------------------------------------------------------- */

static double seconds_since(Uint64 start)
{
    return (double)(SDL_GetPerformanceCounter() - start) / (double)SDL_GetPerformanceFrequency();
}

typedef struct Played {
    int pictures;
    double first, last;     /* their times */
    int in_order;           /* each later than the one before */
    int on_clock;           /* none earlier than the real time since the start allows, none far behind it */
    int levels_right;       /* each one's level as the fades say */
    int pitch_right;        /* each one's rows packed, none of the decoder's padding */
    double took;            /* seconds to the status that ended the loop */
    int status;
    double base;            /* the video time at the start */
    double fade_from, fade_until, fade_in, fade_out;
    int with_clock;         /* none taken before the clock told to the music (the player's own) made it due */
    double clock_slack;     /* how stale that telling may be: 0 the usual, more for a decoder that sleeps */
    int past_sound;         /* pictures taken later than the sound given so far (the sound's clock cannot) */
    int has_sound;
} Played;

static float level_at(const Played *p, double t)
{
    double level = 1.0;
    if (p->fade_in > 0.0) {
        level = SDL_min(level, (t - p->fade_from) / p->fade_in);
    }
    if (p->fade_out > 0.0) {
        level = SDL_min(level, (p->fade_until - t) / p->fade_out);
    }
    return (float)SDL_clamp(level, 0.0, 1.0);
}

/* Takes pictures as a window would, every few milliseconds, until the status is no longer 0 or
 * `limit` seconds have passed (`paused` seconds of them not counted for the clock's check). */
static void play(Played *p, double limit, Uint64 start, double paused)
{
    char why[128];
    while ((p->status = khdays_hd_status(why, sizeof(why))) == 0 && seconds_since(start) < limit) {
        KhdaysHdPicture picture;
        if (khdays_hd_picture_begin(&picture)) {
            const double real = seconds_since(start) - paused + p->base;
            const double frame = picture.fps > 0.0 ? 1.0 / picture.fps : 0.04;
            if (p->pictures == 0) {
                p->first = picture.time;
            } else if (picture.time <= p->last) {
                p->in_order = 0;
            }
            /* due: not ahead of the real time by more than half a picture and the test's own slack;
             * not behind it by more than the dummy device's steps and a loaded machine allow */
            if (picture.time > real + frame * 0.5 + 0.06 || picture.time < real - 0.6) {
                fprintf(stderr, "movie_hd_core_test: a picture of %.3f s taken at %.3f s of real time\n",
                        picture.time, real);
                p->on_clock = 0;
            }
            {
                /* the player's own clock, as it told the music a moment ago (its thread tells it every
                 * 2 ms): a picture is due at half a picture before its time at the earliest */
                const double told = music_clock();
                if (told >= 0.0 && picture.time > told + frame * 0.5 + 0.03 + p->clock_slack) {
                    fprintf(stderr, "movie_hd_core_test: a picture of %.3f s taken with the clock at %.3f s\n",
                            picture.time, told);
                    p->with_clock = 0;
                }
                /* and the sound's clock is never past the sound given to the stream */
                if (p->has_sound && heard_now(0).frames > 0 && picture.time > heard_now(0).last_end + frame * 0.5) {
                    ++p->past_sound;
                }
            }
            if (fabsf(picture.level - level_at(p, picture.time)) > 0.002f) {
                fprintf(stderr, "movie_hd_core_test: level %.3f at %.3f s, expected %.3f\n", picture.level,
                        picture.time, level_at(p, picture.time));
                p->levels_right = 0;
            }
            if (picture.pitch != picture.width || picture.chroma != picture.luma + picture.pitch * picture.height) {
                p->pitch_right = 0;
            }
            for (int y = 0; y < picture.height && p->pitch_right; ++y) {
                const uint8_t expected = (uint8_t)(16 + (int)(picture.time * picture.fps + 0.5) % 200);
                if (picture.luma[y * picture.pitch] != expected ||
                    picture.luma[y * picture.pitch + picture.width - 1] != expected ||
                    (y < picture.height / 2 && (picture.chroma[y * picture.pitch] != 100 ||
                                                picture.chroma[y * picture.pitch + picture.width - 1] != 160))) {
                    p->pitch_right = 0;
                }
            }
            p->last = picture.time;
            ++p->pictures;
            khdays_hd_picture_done();
        }
        SDL_Delay(3);
    }
    p->took = seconds_since(start);
}

static Played played(double base)
{
    Played p;
    memset(&p, 0, sizeof(p));
    p.in_order = p.on_clock = p.levels_right = p.pitch_right = p.with_clock = 1;
    p.base = base;
    return p;
}

/* Whether no picture is due now (one that is, is given back at once: the player waits on it). */
static int nothing_due(void)
{
    KhdaysHdPicture picture;
    if (khdays_hd_picture_begin(&picture)) {
        khdays_hd_picture_done();
        return 0;
    }
    return 1;
}

/* A picture taken where one is expected, and given back only if it was (a failed check goes on). */
static int taken;
static int take(KhdaysHdPicture *picture)
{
    taken = khdays_hd_picture_begin(picture);
    if (!taken) {
        static const uint8_t blank[128 * 64]; /* what the checks after it read instead: all zero */
        memset(picture, 0, sizeof(*picture));
        picture->luma = picture->chroma = blank;
    }
    return taken;
}
static void done(void)
{
    if (taken) {
        khdays_hd_picture_done();
    }
    taken = 0;
}

static SDL_AudioStream *stream_of(int voice)
{
    return (SDL_AudioStream *)khdays_hd_core_stream(voice);
}

/* Another thread's part of the closing test: the volume a few times, then the closing. */
static SDL_AtomicInt closer_done;
static int SDLCALL closer(void *unused)
{
    (void)unused;
    for (int i = 0; i < 40; ++i) {
        khdays_hd_set_gain(0.25f + 0.01f * (float)i);
        SDL_Delay(2);
    }
    khdays_hd_close();
    SDL_SetAtomicInt(&closer_done, 1);
    return 0;
}

int main(void)
{
    KhdaysHdInfo info;
    KhdaysHdPicture picture;
    char why[256];
    uint8_t *rgba = NULL;
    size_t capacity = 0;
    int w = 0, h = 0;

    SDL_SetHint(SDL_HINT_AUDIO_DRIVER, "dummy");
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        fprintf(stderr, "movie_hd_core_test: SDL's audio did not start: %s\n", SDL_GetError());
        return 1;
    }
    events_lock = SDL_CreateMutex();
    heard_lock = SDL_CreateMutex();
    music_lock = SDL_CreateMutex();
    khdays_hd_core_sound_tap(tap);

    /* nothing open */
    CHECK(khdays_hd_status(why, sizeof(why)) == 2);
    CHECK(nothing_due());
    CHECK(!khdays_hd_picture_rgba(&rgba, &capacity, &w, &h));
    khdays_hd_close(); /* harmless */
    khdays_hd_start();
    khdays_hd_pause(1);
    khdays_hd_pause(0);

    /* a file that cannot be opened */
    memset(why, 0, sizeof(why));
    CHECK(!khdays_hd_open("missing", &info, why, sizeof(why)));
    CHECK(strstr(why, "no clip missing") != NULL);
    CHECK(khdays_hd_status(why, sizeof(why)) == 2 && SDL_GetAtomicInt(&open_decoders) == 0);
    CHECK(SDL_GetAtomicInt(&music_opens) == 0);

    /* --- a whole video: the first picture waits, the start, the pictures in order, the end --- */
    {
        Played p = played(0.0);
        Heard before;
        Uint64 start;
        p.has_sound = 1;
        heard_clear(-1.0);
        khdays_hd_set_gain(0.25f); /* set with none open: the next one's */
        CHECK(khdays_hd_open("one", &info, why, sizeof(why)));
        CHECK(info.width == 64 && info.height == 36 && info.fps == 30.0 && info.channels == 2 && info.rate == 48000);
        CHECK(SDL_GetAtomicInt(&music_opens) == 1 && strcmp(music_path, "one") == 0);
        CHECK(event_index("open pictures one") == 0 && event_index("open sound one") == 1);
        CHECK(khdays_hd_status(why, sizeof(why)) == 0);
        CHECK(stream_of(0) != NULL && SDL_GetAudioStreamGain(stream_of(0)) == 0.25f);
        CHECK(stream_of(0) != NULL && SDL_AudioStreamDevicePaused(stream_of(0))); /* silent until the start */
        CHECK(!khdays_hd_picture_rgba(&rgba, &capacity, &w, &h));                   /* none taken yet */
        CHECK(take(&picture));
        CHECK(picture.time == 0.0 && picture.width == 64 && picture.height == 36 && picture.pitch == 64 &&
              picture.fps == 30.0 && picture.level == 1.0f && picture.luma[0] == 16 && picture.luma[63] == 16 &&
              picture.luma[64 * 35 + 63] == 16 && picture.chroma[0] == 100 && picture.chroma[1] == 160 &&
              picture.chroma[64 * 17 + 63] == 160);
        done();
        SDL_Delay(150);
        CHECK(nothing_due()); /* it waits on its first picture */
        CHECK(khdays_hd_status(why, sizeof(why)) == 0);
        before = heard_now(0);
        /* a little sound is read ahead, no more: 0.3 s and at most a block past it */
        CHECK(before.frames >= 14400 && before.frames <= 14400 + SOUND_BLOCK && before.first_time == 0.0 &&
              before.first == 0.5f);
        CHECK(SDL_GetAtomicInt(&music_started_ticks) == 0 && SDL_GetAtomicInt(&music_ticks) > 0);
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        khdays_hd_start(); /* twice is once */
        CHECK(!SDL_AudioStreamDevicePaused(stream_of(0)));
        khdays_hd_set_gain(0.5f);
        CHECK(SDL_GetAudioStreamGain(stream_of(0)) == 0.5f);
        play(&p, 4.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: whole: %d pictures, %.3f to %.3f s, ended (status %d) after %.3f s\n",
                p.pictures, p.first, p.last, p.status, p.took);
        CHECK(p.status == 1);
        CHECK(p.pictures >= 15 && p.pictures <= 29); /* the first was taken before the start */
        CHECK(p.in_order && p.on_clock && p.levels_right && p.pitch_right);
        CHECK(p.with_clock && p.past_sound == 0);
        CHECK(SDL_GetAtomicInt(&latency_asked) >= 2); /* at the opening, then about once a second */
        CHECK(fabs(p.last - 29.0 / 30.0) < 1e-9);  /* the last picture was shown */
        CHECK(p.took >= 0.95 && p.took <= 2.5);     /* its second of sound was played, not skipped */
        CHECK(heard_now(0).frames == 48000);        /* all of its sound, once */
        CHECK(SDL_GetAtomicInt(&music_started_ticks) > 0);
        CHECK(khdays_hd_status(why, sizeof(why)) == 1); /* and stays ended */
        CHECK(khdays_hd_picture_rgba(&rgba, &capacity, &w, &h) && w == 64 && h == 36);
        CHECK(SDL_GetAtomicInt(&open_decoders) == 2); /* open until it is closed */
    }

    /* --- a part of a file with fades, opened over the one before --- */
    {
        Played p = played(0.5);
        Heard all;
        Uint64 start;
        const int closes = SDL_GetAtomicInt(&music_closes);
        heard_clear(0.8);
        events_clear();
        khdays_hd_fades(0.2, 0.2);
        CHECK(khdays_hd_open_part("two", 0.5, 1.1, &info, why, sizeof(why)));
        /* the one before was closed first, its music let go on into this one */
        CHECK(SDL_GetAtomicInt(&music_closes) == closes + 1 && SDL_GetAtomicInt(&music_last_follows) == 1);
        CHECK(event_index("close pictures one") >= 0 && event_index("close sound one") >= 0 &&
              event_index("close sound one") < event_index("open pictures two"));
        CHECK(asked_from == 0.5 && asked_until == 1.1);
        CHECK(SDL_GetAudioStreamGain(stream_of(0)) == 0.5f); /* the volume is kept */
        CHECK(take(&picture));
        /* the decoder began at picture 10 (0.333 s): the part's first is picture 15, black under its fade */
        CHECK(fabs(picture.time - 0.5) < 1e-9 && picture.level == 0.0f && picture.luma[0] == 16 + 15);
        done();
        p.fade_from = 0.5;
        p.fade_until = 1.1;
        p.fade_in = p.fade_out = 0.2;
        p.first = p.last = 0.5;
        p.pictures = 1;
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 4.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: part: %d pictures, %.3f to %.3f s, ended (status %d) after %.3f s\n",
                p.pictures, p.first, p.last, p.status, p.took);
        CHECK(p.status == 1 && p.in_order && p.on_clock && p.levels_right && p.pitch_right);
        CHECK(p.pictures >= 9 && p.pictures <= 18);
        CHECK(fabs(p.last - 32.0 / 30.0) < 1e-9); /* picture 32 (1.067 s) is the last before 1.1 s */
        CHECK(p.took >= 0.55 && p.took <= 2.0);
        all = heard_now(0);
        /* its sound: from 0.5 s (not the block before), 0.6 s of it, silent at both ends, whole between */
        CHECK(fabs(all.first_time - 0.5) < 1.0 / 48000 && llabs(all.frames - 28800) <= 2);
        CHECK(fabs(all.last_end - 1.1) < 2.0 / 48000);
        CHECK(fabsf(all.first) < 0.001f && fabsf(all.last) < 0.001f && fabsf(all.at_mark - 0.5f) < 0.001f);
    }

    /* --- a part past the file's end: one that begins there cannot play; one that ends there is the
     * file to its end, fading out over its own last moments --- */
    {
        Played p = played(0.6);
        Uint64 start;
        memset(why, 0, sizeof(why));
        CHECK(!khdays_hd_open_part("one", 1.5, 0.0, &info, why, sizeof(why)));
        CHECK(strstr(why, "past its end") != NULL && khdays_hd_status(why, sizeof(why)) == 2);
        CHECK(SDL_GetAtomicInt(&open_decoders) == 0);
        khdays_hd_fades(0.0, 0.2);
        CHECK(khdays_hd_open_part("one", 0.6, 7.5, &info, why, sizeof(why)));
        CHECK(info.duration == 1.0);
        CHECK(take(&picture));
        CHECK(fabs(picture.time - 0.6) < 1e-9 && picture.level == 1.0f);
        done();
        p.fade_until = 1.0;
        p.fade_out = 0.2;
        p.first = p.last = 0.6;
        p.pictures = 1;
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 3.0, start, 0.0);
        CHECK(p.status == 1 && p.in_order && p.on_clock && p.levels_right && fabs(p.last - 29.0 / 30.0) < 1e-9);
    }

    /* --- fades are the next video's only; the pause holds the clock and the decoding --- */
    {
        Played p = played(0.0);
        Heard at_pause, after_pause;
        Uint64 start, pause_start;
        double paused;
        int pictures_at_pause, paused_ticks;
        heard_clear(-1.0);
        CHECK(khdays_hd_open("one", &info, why, sizeof(why)));
        CHECK(take(&picture));
        CHECK(picture.level == 1.0f); /* no fade left over from the part */
        done();
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 0.3, start, 0.0);
        CHECK(p.status == 0 && p.pictures >= 3);
        khdays_hd_pause(1);
        khdays_hd_pause(1);
        CHECK(SDL_AudioStreamDevicePaused(stream_of(0)));
        pause_start = SDL_GetPerformanceCounter();
        SDL_Delay(60); /* what the device had already taken plays out */
        while (khdays_hd_picture_begin(&picture)) {
            p.last = picture.time;
            ++p.pictures;
            khdays_hd_picture_done(); /* taken just above */
        }
        at_pause = heard_now(0);
        pictures_at_pause = p.pictures;
        paused_ticks = SDL_GetAtomicInt(&music_paused_ticks);
        SDL_Delay(400);
        CHECK(nothing_due()); /* the clock stands: no newer picture is due */
        CHECK(khdays_hd_status(why, sizeof(why)) == 0);
        after_pause = heard_now(0);
        CHECK(after_pause.frames == at_pause.frames); /* nothing more is decoded */
        CHECK(SDL_GetAtomicInt(&music_paused_ticks) > paused_ticks); /* the music is told */
        CHECK(p.last < 0.6);                                          /* 0.3 s of play, however long the pause */
        khdays_hd_pause(0);
        paused = seconds_since(pause_start);
        CHECK(!SDL_AudioStreamDevicePaused(stream_of(0)));
        play(&p, 5.0, start, paused);
        fprintf(stderr,
                "movie_hd_core_test: pause: %d pictures by the pause, %d in all, last %.3f s, ended (status %d) "
                "after %.3f s with %.3f s paused\n",
                pictures_at_pause, p.pictures, p.last, p.status, p.took, paused);
        CHECK(p.status == 1 && p.in_order && p.on_clock && p.pitch_right);
        CHECK(fabs(p.last - 29.0 / 30.0) < 1e-9);
        CHECK(p.took >= 0.95 + paused && p.took <= 2.5 + paused);
        khdays_hd_close();
        CHECK(khdays_hd_status(why, sizeof(why)) == 2 && SDL_GetAtomicInt(&open_decoders) == 0);
        CHECK(SDL_GetAtomicInt(&music_last_follows) == 0);
        CHECK(stream_of(0) == NULL && nothing_due());
        CHECK(!khdays_hd_picture_rgba(&rgba, &capacity, &w, &h));
    }

    /* --- a picture that cannot be decoded part-way: -1 with the reason, then closed --- */
    {
        Played p = played(0.0);
        Uint64 start;
        CHECK(khdays_hd_open("breaks", &info, why, sizeof(why)));
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 3.0, start, 0.0);
        memset(why, 0, sizeof(why));
        CHECK(p.status == -1 && khdays_hd_status(why, sizeof(why)) == -1);
        CHECK(strstr(why, "made-up failure at picture 12") != NULL);
        CHECK(p.last < 12.0 / 30.0 && p.took < 1.5);
        khdays_hd_close();
        CHECK(khdays_hd_status(why, sizeof(why)) == 2 && SDL_GetAtomicInt(&open_decoders) == 0);
    }

    /* --- a file whose pictures end well before the length it says: it fails as a broken one --- */
    {
        Played p = played(0.0);
        Uint64 start;
        CHECK(khdays_hd_open("short", &info, why, sizeof(why)));
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 3.0, start, 0.0);
        memset(why, 0, sizeof(why));
        CHECK(khdays_hd_status(why, sizeof(why)) == -1 && strstr(why, "cut short") != NULL);
        khdays_hd_close();
    }

    /* --- no sound: the counter is the clock; odd sizes show their even part --- */
    {
        Played p = played(0.0);
        Uint64 start;
        CHECK(khdays_hd_open("silent", &info, why, sizeof(why)));
        CHECK(info.channels == 0 && info.rate == 0 && stream_of(0) == NULL);
        SDL_Delay(100); /* the counter does not run before the start */
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 3.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: silent: %d pictures, last %.3f s, status %d after %.3f s\n", p.pictures,
                p.last, p.status, p.took);
        CHECK(p.status == 1 && p.in_order && p.on_clock && p.pitch_right && fabs(p.last - 14.0 / 30.0) < 1e-9);
        CHECK(p.took >= 0.45 && p.took <= 1.5);
        CHECK(khdays_hd_open("odd", &info, why, sizeof(why)));
        CHECK(info.width == 66 && info.height == 38 && info.fps == 25.0 && info.channels == 1 && info.rate == 22050);
        CHECK(take(&picture));
        CHECK(picture.width == 66 && picture.height == 38 && picture.pitch == 66 && picture.luma[65] == 16 &&
              picture.luma[66 * 37 + 65] == 16 && picture.chroma[66 * 18 + 64] == 100 &&
              picture.chroma[66 * 18 + 65] == 160);
        done();
    }

    /* --- sound that cannot be decoded part-way ends there; the pictures go on to their end --- */
    {
        Played p = played(0.0);
        Uint64 start;
        heard_clear(-1.0);
        CHECK(khdays_hd_open("soundbreaks", &info, why, sizeof(why)));
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 3.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: sound breaks: %d pictures, last %.3f s, status %d after %.3f s\n",
                p.pictures, p.last, p.status, p.took);
        CHECK(p.status == 1 && p.in_order && fabs(p.last - 17.0 / 30.0) < 1e-9);
        CHECK(heard_now(0).frames == 5 * SOUND_BLOCK);
        CHECK(p.took >= 0.55 && p.took <= 2.0);
    }

    /* --- closed in the middle, then opened again from the start --- */
    {
        Played p = played(0.0);
        Uint64 start;
        CHECK(khdays_hd_open("two", &info, why, sizeof(why)));
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 0.25, start, 0.0);
        CHECK(p.status == 0 && p.pictures >= 2);
        khdays_hd_close();
        CHECK(khdays_hd_status(why, sizeof(why)) == 2 && SDL_GetAtomicInt(&open_decoders) == 0);
        CHECK(nothing_due());
        CHECK(khdays_hd_open("two", &info, why, sizeof(why)));
        CHECK(take(&picture));
        CHECK(picture.time == 0.0 && picture.luma[0] == 16);
        done();
        p = played(0.0);
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 0.3, start, 0.0);
        CHECK(p.status == 0 && p.in_order && p.on_clock && p.pictures >= 2);
        khdays_hd_close();
        khdays_hd_close();
        CHECK(SDL_GetAtomicInt(&open_decoders) == 0);
    }


    /* --- sound longer than the pictures: it has not ended while its sound is still to be heard --- */
    {
        Played p = played(0.0);
        Uint64 start;
        heard_clear(-1.0);
        CHECK(khdays_hd_open("longsound", &info, why, sizeof(why)));
        CHECK(info.duration == 0.8);
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 4.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: long sound: %d pictures, last %.3f s, status %d after %.3f s, %lld frames\n",
                p.pictures, p.last, p.status, p.took, heard_now(0).frames);
        CHECK(p.status == 1 && p.in_order && p.on_clock && p.with_clock);
        CHECK(fabs(p.last - 14.0 / 30.0) < 1e-9); /* its last picture, at 0.467 s */
        CHECK(heard_now(0).frames == 38400);       /* all 0.8 s of its sound */
        CHECK(p.took >= 0.78 && p.took <= 2.5);    /* ended with the sound, not 0.3 s before it with the pictures */
        khdays_hd_close();
    }

    /* --- paused before its start: the start leaves it silent and still, the resume plays it whole; the
     * time paused before the start is none of the clock's (with sound, and on the counter) --- */
    {
        static const char *const names[] = {"one", "silent"};
        static const double lengths[] = {1.0, 0.5}, lasts[] = {29.0 / 30.0, 14.0 / 30.0};
        for (int n = 0; n < 2; ++n) {
            Played p = played(0.0);
            Uint64 start;
            double paused;
            CHECK(khdays_hd_open(names[n], &info, why, sizeof(why)));
            CHECK(take(&picture));
            done();
            khdays_hd_pause(1);
            SDL_Delay(1200); /* (long: a clock that counted it would end the video this much late) */
            start = SDL_GetPerformanceCounter();
            khdays_hd_start();
            CHECK(n == 1 || (stream_of(0) != NULL && SDL_AudioStreamDevicePaused(stream_of(0))));
            SDL_Delay(200);
            CHECK(nothing_due());
            CHECK(khdays_hd_status(why, sizeof(why)) == 0);
            khdays_hd_pause(0);
            paused = seconds_since(start);
            CHECK(n == 1 || (stream_of(0) != NULL && !SDL_AudioStreamDevicePaused(stream_of(0))));
            p.first = p.last = 0.0;
            p.pictures = 1;
            play(&p, 6.0, start, paused);
            fprintf(stderr, "movie_hd_core_test: paused before the start (%s): last %.3f s, status %d after %.3f s with "
                            "%.3f s paused\n",
                    names[n], p.last, p.status, p.took, paused);
            CHECK(p.status == 1 && p.in_order && p.on_clock && p.with_clock && fabs(p.last - lasts[n]) < 1e-9);
            CHECK(p.took - paused >= lengths[n] - 0.05 && p.took - paused <= lengths[n] + 0.9);
            khdays_hd_close();
        }
    }

    /* --- a decoder slower than the video (50 ms a picture of 33 ms): every picture is still shown, in
     * order, and it ends only after the last; its sound is heard whole --- */
    {
        Played p = played(0.0);
        Uint64 start;
        heard_clear(-1.0);
        CHECK(khdays_hd_open("slow", &info, why, sizeof(why)));
        p.clock_slack = 0.12; /* (the player's thread tells the clock between pictures: 50 ms apart here) */
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 8.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: slow decoder: %d pictures, last %.3f s, status %d after %.3f s\n", p.pictures,
                p.last, p.status, p.took);
        CHECK(p.status == 1 && p.in_order && p.with_clock && p.pitch_right);
        CHECK(fabs(p.last - 29.0 / 30.0) < 1e-9);
        CHECK(p.took >= 1.4 && p.took <= 5.0); /* 29 pictures after the first at 50 ms each: not ended before them */
        CHECK(heard_now(0).frames == 48000);
        khdays_hd_close();
    }

    /* --- a decoder that has nothing for a while (a reader being opened again): no failure, nothing
     * lost or twice; at the first picture too --- */
    {
        Played p = played(0.0);
        Uint64 start;
        int before = SDL_GetAtomicInt(&laters);
        heard_clear(-1.0);
        CHECK(khdays_hd_open("later", &info, why, sizeof(why)));
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 5.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: later: %d pictures, last %.3f s, status %d after %.3f s, asked again %d times\n",
                p.pictures, p.last, p.status, p.took, SDL_GetAtomicInt(&laters) - before);
        CHECK(SDL_GetAtomicInt(&laters) - before == 2 * LATER_TIMES); /* the pictures' decoder and the sound's */
        CHECK(p.status == 1 && p.in_order && p.on_clock && p.with_clock && fabs(p.last - 29.0 / 30.0) < 1e-9);
        CHECK(heard_now(0).frames == 48000 && fabs(heard_now(0).last_end - 1.0) < 1e-9);
        CHECK(p.took >= 0.95 && p.took <= 2.5);
        before = SDL_GetAtomicInt(&laters);
        heard_clear(-1.0);
        CHECK(khdays_hd_open("laterfirst", &info, why, sizeof(why))); /* (the opening waits for its first picture) */
        CHECK(take(&picture));
        CHECK(picture.time == 0.0);
        done();
        p = played(0.0);
        p.pictures = 1;
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 5.0, start, 0.0);
        CHECK(SDL_GetAtomicInt(&laters) - before == 2 * LATER_TIMES);
        CHECK(p.status == 1 && p.in_order && fabs(p.last - 14.0 / 30.0) < 1e-9 && heard_now(0).frames == 24000);
        khdays_hd_close();
    }

    /* --- the clock is the sound's: the device stopped behind the player's back (not the player's
     * pause), the clock stands with it -- a counter would run on -- and goes on when it does --- */
    {
        Played p = played(0.0);
        Uint64 start, stop;
        double before, after, away;
        CHECK(khdays_hd_open("two", &info, why, sizeof(why)));
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 0.3, start, 0.0);
        CHECK(p.status == 0 && p.pictures >= 3);
        CHECK(stream_of(0) != NULL && SDL_PauseAudioStreamDevice(stream_of(0)));
        stop = SDL_GetPerformanceCounter();
        SDL_Delay(80); /* the buffer it took last plays out on the clock */
        while (khdays_hd_picture_begin(&picture)) {
            p.last = picture.time;
            ++p.pictures;
            khdays_hd_picture_done();
        }
        before = music_clock();
        SDL_Delay(500);
        after = music_clock();
        fprintf(stderr, "movie_hd_core_test: device stopped: the clock at %.3f s, then %.3f s half a second later\n",
                before, after);
        CHECK(after - before < 0.03); /* it stands (a counter would be 0.5 s on) */
        CHECK(nothing_due());
        CHECK(p.last < 0.3 + 0.08 + 0.1);
        CHECK(khdays_hd_status(why, sizeof(why)) == 0);
        CHECK(stream_of(0) != NULL && SDL_ResumeAudioStreamDevice(stream_of(0)));
        away = seconds_since(stop);
        play(&p, 6.0, start, away);
        CHECK(p.status == 1 && p.in_order && p.with_clock && fabs(p.last - 59.0 / 30.0) < 1e-9);
        CHECK(p.took - away >= 1.9 && p.took - away <= 3.5);
        khdays_hd_close();
    }

    /* --- an output latency (what the system holds ahead of the speaker): the clock is that much behind
     * the sound taken, so the video lasts that much longer than without it --- */
    {
        Played p = played(0.0);
        Uint64 start;
        SDL_SetAtomicInt(&latency_ms, 300);
        CHECK(khdays_hd_open("one", &info, why, sizeof(why)));
        start = SDL_GetPerformanceCounter();
        khdays_hd_start();
        play(&p, 5.0, start, 0.0);
        fprintf(stderr, "movie_hd_core_test: latency 300 ms: last %.3f s, status %d after %.3f s\n", p.last, p.status,
                p.took);
        SDL_SetAtomicInt(&latency_ms, 0);
        CHECK(p.status == 1 && p.in_order && p.with_clock && fabs(p.last - 29.0 / 30.0) < 1e-9);
        CHECK(p.took >= 1.25 && p.took <= 2.8); /* 1 s and its 0.3 s behind */
        khdays_hd_close();
    }

    /* --- the clock told to the music never went back, in any video above --- */
    CHECK(!music_went_back);

    /* --- closed and its volume set from another thread while this one takes pictures and asks its
     * status, as the game's thread does to the window's: nothing read after it is freed, no deadlock --- */
    for (int round = 0; round < 6; ++round) {
        SDL_Thread *other;
        Uint64 start;
        int pictures = 0;
        CHECK(khdays_hd_open(round % 2 ? "silent" : "two", &info, why, sizeof(why)));
        khdays_hd_start();
        SDL_SetAtomicInt(&closer_done, 0);
        other = SDL_CreateThread(closer, "closer", NULL);
        CHECK(other != NULL);
        start = SDL_GetPerformanceCounter();
        while (other != NULL && !SDL_GetAtomicInt(&closer_done) && seconds_since(start) < 5.0) {
            if (khdays_hd_picture_begin(&picture)) {
                ++pictures;
                CHECK(picture.luma != NULL && picture.luma[0] >= 16);
                khdays_hd_picture_done();
            }
            (void)khdays_hd_status(why, sizeof(why));
            khdays_hd_set_gain(0.5f);
        }
        SDL_WaitThread(other, NULL);
        CHECK(SDL_GetAtomicInt(&closer_done) == 1 && pictures >= 1);
        CHECK(khdays_hd_status(why, sizeof(why)) == 2 && nothing_due() && stream_of(0) == NULL);
        CHECK(SDL_GetAtomicInt(&open_decoders) == 0);
    }

    /* --- the colours of the picture taken last, as the window shows them --- */
    {
        CHECK(khdays_hd_open("colours", &info, why, sizeof(why)));
        CHECK(take(&picture));
        done();
        w = h = 0;
        CHECK(khdays_hd_picture_rgba(&rgba, &capacity, &w, &h) && w == 64 && h == 36 && capacity >= 64 * 36 * 4);
        if (w == 64 && h == 36) {
            const uint8_t *white = rgba + (5 * 64 + 5) * 4, *red = rgba + (5 * 64 + 50) * 4;
            const uint8_t *black = rgba + (30 * 64 + 5) * 4, *grey = rgba + (30 * 64 + 50) * 4;
            fprintf(stderr, "movie_hd_core_test: colours: white %d %d %d, red %d %d %d, black %d %d %d, grey %d %d %d\n",
                    white[0], white[1], white[2], red[0], red[1], red[2], black[0], black[1], black[2], grey[0],
                    grey[1], grey[2]);
            CHECK(white[0] == 255 && white[1] == 255 && white[2] == 255 && white[3] == 255);
            CHECK(red[0] >= 253 && red[1] <= 2 && red[2] <= 2 && red[3] == 255); /* BT.709's red, limited range */
            CHECK(black[0] == 0 && black[1] == 0 && black[2] == 0);
            CHECK(abs(grey[0] - 128) <= 1 && grey[0] == grey[1] && grey[1] == grey[2]);
        }
        khdays_hd_close();
    }

    /* --- a voice: the sound of two files one after the other, on its own stream --- */
    {
        const char *two[] = {"voice-a", "voice-b"}, *one[] = {"voice-long"}, *none[] = {"missing", "silent"};
        Heard v;
        Uint64 start = SDL_GetPerformanceCounter();
        int switched = 0, in_order = 1;
        heard_clear(-1.0);
        events_clear();
        khdays_hd_voice_set_gain(0.75f);
        khdays_hd_voice_stop(); /* none heard: harmless */
        khdays_hd_voice_play(two, 2);
        while (event_index("close sound voice-b") < 0 && seconds_since(start) < 4.0) {
            SDL_Delay(5);
        }
        v = heard_now(1);
        fprintf(stderr, "movie_hd_core_test: voice: %lld frames in %d blocks after %.3f s\n", v.frames, v.blocks,
                seconds_since(start));
        CHECK(event_index("open sound voice-a") == 0 && event_index("close sound voice-a") == 1 &&
              event_index("open sound voice-b") == 2 && event_index("close sound voice-b") == 3);
        CHECK(event_index("open pictures voice-a") < 0 && event_index("open pictures voice-b") < 0);
        CHECK(v.frames == 8820 + 19200); /* 0.4 s of each, whole */
        SDL_LockMutex(heard_lock);
        for (int i = 1; i < voice_orders; ++i) {
            if (voice_order[i] != voice_order[i - 1]) {
                ++switched;
            }
            if (voice_order[i] == 22050 && voice_order[i - 1] == 48000) {
                in_order = 0;
            }
        }
        SDL_UnlockMutex(heard_lock);
        CHECK(switched == 1 && in_order && voice_order[0] == 22050);
        CHECK(seconds_since(start) >= 0.3); /* read a little ahead of the device, not all at once */
        CHECK(stream_of(1) != NULL && SDL_GetAudioStreamGain(stream_of(1)) == 0.75f);
        CHECK(stream_of(1) != NULL && !SDL_AudioStreamDevicePaused(stream_of(1)));
        CHECK(stream_of(0) == NULL); /* its own stream, not the video's */
        khdays_hd_voice_set_gain(0.5f);
        CHECK(SDL_GetAudioStreamGain(stream_of(1)) == 0.5f);
        khdays_hd_voice_stop();
        CHECK(stream_of(1) == NULL && SDL_GetAtomicInt(&open_decoders) == 0);

        /* paused before it begins: nothing is read until it is resumed; then stopped part-way */
        heard_clear(-1.0);
        events_clear();
        khdays_hd_voice_pause(1);
        khdays_hd_voice_play(one, 1);
        SDL_Delay(250);
        CHECK(event_index("open sound voice-long") == 0);
        CHECK(heard_now(1).frames == 0);
        CHECK(stream_of(1) != NULL && SDL_AudioStreamDevicePaused(stream_of(1)));
        khdays_hd_voice_pause(0);
        SDL_Delay(250);
        v = heard_now(1);
        CHECK(v.frames >= 14400 && v.frames < 72000 && v.first == 0.125f);
        CHECK(stream_of(1) != NULL && !SDL_AudioStreamDevicePaused(stream_of(1)));
        khdays_hd_voice_pause(1); /* paused part-way: the reading stops with the device */
        SDL_Delay(50);
        v = heard_now(1);
        SDL_Delay(300);
        CHECK(heard_now(1).frames == v.frames);
        khdays_hd_voice_play(two, 2); /* a new voice stops the one still heard */
        CHECK(event_index("close sound voice-long") >= 0);
        khdays_hd_voice_stop();
        CHECK(stream_of(1) == NULL && SDL_GetAtomicInt(&open_decoders) == 0);
        khdays_hd_voice_pause(0);

        /* a voice whose decoder has nothing for a while: heard whole all the same */
        heard_clear(-1.0);
        {
            const char *later[] = {"voice-later"};
            const int before = SDL_GetAtomicInt(&laters);
            Uint64 began = SDL_GetPerformanceCounter();
            events_clear();
            khdays_hd_voice_play(later, 1);
            while (event_index("close sound voice-later") < 0 && seconds_since(began) < 4.0) {
                SDL_Delay(5);
            }
            CHECK(SDL_GetAtomicInt(&laters) - before == LATER_TIMES && heard_now(1).frames == 19200);
            khdays_hd_voice_stop();
        }

        /* files without a voice are said and passed over */
        khdays_hd_voice_play(none, 2);
        SDL_Delay(100);
        CHECK(stream_of(1) == NULL);
        khdays_hd_voice_stop();
        khdays_hd_voice_play(none, 0);
        CHECK(SDL_GetAtomicInt(&open_decoders) == 0);
    }

    SDL_free(rgba);
    SDL_Quit();
    if (failures) {
        fprintf(stderr, "movie_hd_core_test: %d checks FAILED\n", failures);
        return 1;
    }
    printf("movie_hd_core_test: passed\n");
    return 0;
}
