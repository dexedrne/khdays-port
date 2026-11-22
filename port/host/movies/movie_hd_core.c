/* The player's HD video (movie_hd.h) where the system's decoder is behind movie_hd_decoder.h (Apple's:
 * movie_hd_apple.m): everything but the decoding itself, and the same course as Windows' player
 * (movie_hd_win.c), function by function. A thread of its own reads the pictures and the sound ahead,
 * each from a decoder of its own at its own pace (the pictures as NV12, the sound as floats into its
 * SDL stream); the clock is the sound heard so far (what the device has taken from the stream, less the
 * buffer it is playing and what the system holds ahead of the speaker, moving on smoothly between the
 * device's takings), else the performance counter less the pauses; each refresh takes the newest
 * picture due. Apart from it, a voice: files' sound alone over the game,
 * on a thread and a stream of their own.
 *
 * Every opening, start, pause, end, closing and failure is said on stderr ("movies: ..."): on a phone
 * the console is all there is to tell what happened. */
#include "movie_hd.h"

#include "movie_hd_decoder.h"
#include "theater_music.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PICTURES_AHEAD 0.15 /* seconds of pictures decoded ahead of the clock */
#define SOUND_AHEAD 0.30    /* seconds of sound queued ahead of the device */

typedef struct Picture {
    double time; /* its presentation time in the file, in seconds */
    long number; /* counted from 0, in decoding order */
    int width, height;
    uint8_t *data;
    size_t size;
} Picture;

static void *lock_slot, *clock_slot, *voice_slot; /* SDL_Mutex *, made at first use (lock_of) */
static float gain = 1.0f;                         /* the sound's volume (khdays_hd_set_gain) */
static KhdaysHdSoundTap sound_tap;                /* tests only (khdays_hd_core_sound_tap) */

static struct {
    SDL_Thread *thread;
    SDL_Semaphore *opened; /* signalled once the thread has opened the file, or failed to */
    SDL_Semaphore *wake;   /* wakes the thread early (room ahead, a pause, the start) */
    SDL_AtomicInt stop, started, paused;
    int open_ok;
    int status; /* what khdays_hd_status says (movie_hd.h): 0 playing, 1 ended, -1 failed, 2 none */
    char why[320];
    char *path;
    KhdaysHdInfo info;
    /* the picture */
    KhdaysHdDecoder *video;
    int video_eos;
    Picture *pictures; /* decoded, oldest first */
    int count, capacity;
    Picture *spare; /* picture buffers kept to be used again */
    int spare_count, spare_capacity;
    long decoded, taken;      /* pictures decoded; the number of the last one taken (-1: none) */
    double first_time;        /* the first picture's time */
    double duration;          /* how long the file says it lasts, or 0 when it does not say */
    double from, end;         /* the part played (khdays_hd_open_part): its start, its end (0 the file's) */
    double fade_in, fade_out; /* its fades (khdays_hd_fades), in seconds; 0 none */
    /* the sound */
    KhdaysHdDecoder *audio;
    SDL_AudioStream *sound;
    int sound_eos;
    Sint64 put_frames;  /* sound frames put into the stream so far (under the clock's lock) */
    double sound_start; /* the first sound sample's time (under the clock's lock) */
    int last_queued;    /* after the sound's end: what was still queued, and since when */
    Uint64 still_since;
    /* the sound's clock between the device's takings (all under the clock's lock) */
    double device_buffer; /* seconds of sound the device takes from the stream at a time; 0 not known */
    double latency;       /* seconds from a taken buffer's turn to play to its being heard (the decoder's
                             khdays_hd_decoder_output_latency) */
    Uint64 latency_at;    /* when it was asked last */
    Sint64 taken_frames;  /* the frames the device had taken when last looked at */
    Uint64 taken_at;      /* the counter when that number last changed */
    double sound_clock;   /* the latest the sound's clock has read: it never goes back */
    /* the counter's clock: used without sound, or once the sound has all played */
    SDL_AtomicInt counter_clock;
    double counter_base;            /* the video time it starts from */
    Uint64 counter_start;           /* the counter's value at that time */
    Uint64 paused_at, paused_ticks; /* when the pause began; the counter ticks spent paused */
    Uint64 frequency;
    /* what happened, for the report when it ends and closes */
    long shown, late;
    double worst_offset;
    int end_said;
} hd = {.status = 2, .taken = -1};
static double next_fade_in, next_fade_out; /* khdays_hd_fades: the next opening's */

/* A lock made at first use (any thread may be first). */
static SDL_Mutex *lock_of(void **slot)
{
    SDL_Mutex *m = (SDL_Mutex *)SDL_GetAtomicPointer(slot);
    if (m == NULL) {
        SDL_Mutex *made = SDL_CreateMutex();
        if (!SDL_CompareAndSwapAtomicPointer(slot, NULL, made)) {
            SDL_DestroyMutex(made);
        }
        m = (SDL_Mutex *)SDL_GetAtomicPointer(slot);
    }
    return m;
}

/* Guards the pictures, the status and the clock's bases. */
static void lock(void)
{
    SDL_LockMutex(lock_of(&lock_slot));
}
static void unlock(void)
{
    SDL_UnlockMutex(lock_of(&lock_slot));
}

/* Guards the count of sound frames put, with the putting itself: the clock never reads one without
 * the other. Taken inside the other lock, never around it. */
static void clock_lock(void)
{
    SDL_LockMutex(lock_of(&clock_slot));
}
static void clock_unlock(void)
{
    SDL_UnlockMutex(lock_of(&clock_slot));
}

void khdays_hd_core_sound_tap(KhdaysHdSoundTap tap)
{
    sound_tap = tap;
}

static void *voice_stream_now(void);

void *khdays_hd_core_stream(int voice)
{
    return voice ? voice_stream_now() : (void *)hd.sound;
}

/* The file's name without its folders, for the messages after the opening's. */
static const char *name_of(const char *path)
{
    const char *name = path != NULL ? path : "";
    for (const char *p = name; *p != '\0'; ++p) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    return name;
}

/* How much of the video shows and sounds at `t` seconds of the file through its fades: 0 to 1. */
static float fade_level(double t)
{
    const double end = hd.end > 0.0 ? hd.end : hd.duration;
    double level = 1.0;
    if (hd.fade_in > 0.0) {
        level = SDL_min(level, (t - hd.from) / hd.fade_in);
    }
    if (hd.fade_out > 0.0 && end > 0.0) {
        level = SDL_min(level, (end - t) / hd.fade_out);
    }
    return (float)SDL_clamp(level, 0.0, 1.0);
}

/* The video's clock, in seconds of the file's time (see the top of this file). */
static double clock_now(void)
{
    if (!SDL_GetAtomicInt(&hd.started)) {
        return hd.first_time;
    }
    if (!SDL_GetAtomicInt(&hd.counter_clock)) {
        double now = 0.0;
        int sound;
        clock_lock(); /* (the closing takes the stream away under this lock: looked at inside it) */
        sound = hd.sound != NULL;
        if (sound) {
            const int frame_bytes = hd.info.channels * (int)sizeof(float);
            const Uint64 counter = SDL_GetAtomicInt(&hd.paused) ? hd.paused_at : SDL_GetPerformanceCounter();
            int queued = SDL_GetAudioStreamQueued(hd.sound);
            Sint64 taken;
            double since;
            if (queued < 0) {
                queued = 0;
            }
            /* What the device has taken is not yet heard: it took a whole buffer at once, which it
             * plays from then on (so the clock moves on with the counter from each taking, a buffer's
             * length at most), and the system may hold more ahead of the speaker (the latency). */
            taken = hd.put_frames - queued / frame_bytes;
            if (taken != hd.taken_frames || hd.taken_at == 0) {
                hd.taken_frames = taken;
                hd.taken_at = counter;
            }
            since = (double)(Sint64)(counter - hd.taken_at) / (double)(Sint64)hd.frequency;
            now = hd.sound_start + (double)taken / hd.info.rate - hd.device_buffer +
                  SDL_clamp(since, 0.0, hd.device_buffer) - hd.latency;
            if (now < hd.sound_start) {
                now = hd.sound_start;
            }
            if (now < hd.first_time) {
                now = hd.first_time; /* (what it read before its start: a part's sound may begin a sample early) */
            }
            if (now < hd.sound_clock) {
                now = hd.sound_clock; /* (a taking smaller or earlier than a buffer: never back) */
            }
            hd.sound_clock = now;
        }
        clock_unlock();
        if (sound) {
            return now;
        }
    }
    {
        const Uint64 now = SDL_GetAtomicInt(&hd.paused) ? hd.paused_at : SDL_GetPerformanceCounter();
        return hd.counter_base +
               (double)(Sint64)(now - hd.counter_start - hd.paused_ticks) / (double)(Sint64)hd.frequency;
    }
}

/* A buffer for a picture of `size` bytes: a spare one of that size, or a new one. The decoder's
 * thread asks while the presenter's gives spares back (khdays_hd_picture_begin, keep_spare): under
 * the lock, or the list is taken apart under both. */
static uint8_t *picture_buffer(size_t size)
{
    lock();
    while (hd.spare_count > 0) {
        Picture p = hd.spare[--hd.spare_count];
        if (p.size == size) {
            unlock();
            return p.data;
        }
        free(p.data);
    }
    unlock();
    return (uint8_t *)malloc(size);
}

/* Keeps a picture's buffer to be used again (freed if there is no room). */
static void keep_spare(Picture p)
{
    if (hd.spare_count == hd.spare_capacity) {
        const int capacity = hd.spare_capacity ? hd.spare_capacity * 2 : 8;
        Picture *grown = (Picture *)realloc(hd.spare, (size_t)capacity * sizeof(Picture));
        if (grown == NULL) {
            free(p.data);
            return;
        }
        hd.spare = grown;
        hd.spare_capacity = capacity;
    }
    hd.spare[hd.spare_count++] = p;
}

/* Adds a decoded picture (`size` bytes at `data`) to the list. */
static int add_picture(uint8_t *data, size_t size, double time, int width, int height)
{
    lock();
    if (hd.count == hd.capacity) {
        const int capacity = hd.capacity ? hd.capacity * 2 : 16;
        Picture *grown = (Picture *)realloc(hd.pictures, (size_t)capacity * sizeof(Picture));
        if (grown == NULL) {
            unlock();
            free(data);
            snprintf(hd.why, sizeof(hd.why), "out of memory for the pictures");
            return 0;
        }
        hd.pictures = grown;
        hd.capacity = capacity;
    }
    hd.pictures[hd.count].time = time;
    hd.pictures[hd.count].number = hd.decoded++;
    hd.pictures[hd.count].width = width;
    hd.pictures[hd.count].height = height;
    hd.pictures[hd.count].data = data;
    hd.pictures[hd.count].size = size;
    hd.count++;
    unlock();
    return 1;
}

/* Copies a decoded picture's planes, row by row out of their pitches, into a new picture of the
 * shown size and adds it to the list. */
static int keep_picture(const KhdaysHdDecoded *d)
{
    const int w = d->width & ~1, h = d->height & ~1; /* NV12's chroma: one sample per two columns and rows */
    const size_t size = (size_t)w * (size_t)h * 3 / 2;
    uint8_t *data;
    if (w <= 0 || h <= 0 || d->luma == NULL || d->chroma == NULL || d->luma_pitch < w || d->chroma_pitch < w) {
        snprintf(hd.why, sizeof(hd.why), "a picture is not laid out as NV12 of %dx%d (pitches %d and %d)", d->width,
                 d->height, d->luma_pitch, d->chroma_pitch);
        return 0;
    }
    data = picture_buffer(size);
    if (data == NULL) {
        snprintf(hd.why, sizeof(hd.why), "out of memory for a picture of %dx%d", w, h);
        return 0;
    }
    for (int y = 0; y < h; ++y) {
        memcpy(data + (size_t)y * (size_t)w, d->luma + (size_t)y * (size_t)d->luma_pitch, (size_t)w);
    }
    for (int y = 0; y < h / 2; ++y) {
        memcpy(data + (size_t)w * (size_t)h + (size_t)y * (size_t)w, d->chroma + (size_t)y * (size_t)d->chroma_pitch,
               (size_t)w);
    }
    return add_picture(data, size, d->time, w, h);
}

/* The newest decoded picture's time (very negative when there is none). */
static double newest_time(void)
{
    double t;
    lock();
    t = hd.count > 0 ? hd.pictures[hd.count - 1].time : -1.0e9;
    unlock();
    return t;
}

/* Decodes one picture (or finds the end). Returns 0 on an error, 2 when the decoder has none now and is
 * to be asked again a little later (KHDAYS_HD_DECODER_LATER), else 1. */
static int read_picture(void)
{
    KhdaysHdDecoded d;
    int got;
    memset(&d, 0, sizeof(d));
    hd.why[0] = '\0';
    got = khdays_hd_decoder_picture(hd.video, &d, hd.why, sizeof(hd.why));
    if (got == KHDAYS_HD_DECODER_LATER) {
        return 2;
    }
    if (got < 0) {
        if (hd.why[0] == '\0') {
            snprintf(hd.why, sizeof(hd.why), "the picture cannot be decoded");
        }
        return 0;
    }
    if (got == 0) {
        const double last = newest_time();
        hd.video_eos = 1;
        if (hd.decoded > 0 && hd.end == 0.0 && hd.duration > 0.0 && last + 0.5 < hd.duration) {
            /* its pictures end well before its length: a file cut short. It
             * fails as a broken file does (and the game's movie is shown) */
            snprintf(hd.why, sizeof(hd.why), "its pictures end at %.2f s of its %.2f s (the file cut short?)", last,
                     hd.duration);
            return 0;
        }
        return 1;
    }
    if (hd.end > 0.0 && d.time >= hd.end) {
        /* the part's end: neither this picture nor those after it are shown */
        hd.video_eos = 1;
        return 1;
    }
    if (hd.from > 0.0 && d.time + (hd.info.fps > 0.0 ? 0.5 / hd.info.fps : 0.008) < hd.from) {
        return 1; /* before the part's start (the decoder begins at the key picture before it) */
    }
    return keep_picture(&d);
}

/* Decodes some sound into the stream (or finds its end). An error ends the
 * sound there. Returns 0 when the decoder has none now and is to be asked again a little later. */
static int read_sound(void)
{
    float *samples = NULL;
    int frames = 0;
    double at = 0.0;
    char why[256] = "";
    const int got = khdays_hd_decoder_sound(hd.audio, &samples, &frames, &at, why, sizeof(why));
    if (got == KHDAYS_HD_DECODER_LATER) {
        return 0;
    }
    if (got < 0) {
        fprintf(stderr, "movies: the sound of %s cannot be decoded (%s); it ends there\n", name_of(hd.path),
                why[0] ? why : "no reason given");
        hd.sound_eos = 1;
        SDL_FlushAudioStream(hd.sound);
        return 1;
    }
    if (got > 0 && samples != NULL && frames > 0) {
        const int channels = hd.info.channels;
        if (hd.from > 0.0 && at < hd.from) {
            /* before the part's start: left out */
            const double early = (hd.from - at) * hd.info.rate;
            const int skipped = early < (double)frames ? (int)early : frames;
            samples += (size_t)skipped * (size_t)channels;
            frames -= skipped;
            at += (double)skipped / hd.info.rate;
        }
        if (hd.end > 0.0 && at + (double)frames / hd.info.rate > hd.end) {
            /* the part's end: the sound up to it */
            const double kept = hd.end > at ? (hd.end - at) * hd.info.rate : 0.0;
            frames = kept < (double)frames ? (int)kept : frames;
            hd.sound_eos = 1;
        }
        if (hd.fade_in > 0.0 || hd.fade_out > 0.0) {
            for (int i = 0; i < frames; ++i) {
                const float level = fade_level(at + (double)i / hd.info.rate);
                for (int c = 0; c < channels; ++c) {
                    samples[(size_t)i * (size_t)channels + (size_t)c] *= level;
                }
            }
        }
        if (frames > 0) {
            if (sound_tap != NULL) {
                sound_tap(0, samples, frames, channels, hd.info.rate, at);
            }
            clock_lock();
            if (hd.put_frames == 0) {
                hd.sound_start = at;
            }
            if (SDL_PutAudioStreamData(hd.sound, samples, frames * channels * (int)sizeof(float))) {
                hd.put_frames += frames;
            } else {
                fprintf(stderr, "movies: the sound of %s was not taken by its stream: %s\n", name_of(hd.path),
                        SDL_GetError());
            }
            clock_unlock();
        }
    }
    if (got == 0) {
        hd.sound_eos = 1;
    }
    if (hd.sound_eos) {
        /* nothing more comes: flush, so what the stream holds back for its
         * resampler plays too (otherwise a few frames stay queued for ever) */
        SDL_FlushAudioStream(hd.sound);
    }
    return 1;
}

/* Asks how far behind its taking the sound is heard, at the opening and about once a second after (the
 * route may change: earphones connected); said when it is first known and when it changes. */
static void follow_latency(void)
{
    const Uint64 counter = SDL_GetPerformanceCounter();
    double latency, before;
    if (hd.sound == NULL || (hd.latency_at != 0 && counter - hd.latency_at < hd.frequency)) {
        return;
    }
    latency = khdays_hd_decoder_output_latency(hd.device_buffer);
    if (!(latency >= 0.0 && latency < 2.0)) {
        latency = 0.0;
    }
    clock_lock();
    before = hd.latency_at != 0 ? hd.latency : -1.0;
    hd.latency = latency;
    hd.latency_at = counter;
    clock_unlock();
    if (before < 0.0 || SDL_fabs(before - latency) > 0.005) {
        fprintf(stderr,
                "movies: sound output of %s: the device takes %.1f ms at a time; heard %.1f ms after its turn to "
                "play (the clock is the sound heard)\n",
                name_of(hd.path), hd.device_buffer * 1000.0, latency * 1000.0);
    }
}

/* The seconds of sound queued in the stream, not yet played. */
static double sound_queued(void)
{
    const int queued = SDL_GetAudioStreamQueued(hd.sound);
    return queued > 0 ? (double)queued / (double)(hd.info.channels * (int)sizeof(float)) / hd.info.rate : 0.0;
}

/* Switches to the counter's clock from now on, starting at `time`. */
static void use_counter_clock(double time)
{
    lock();
    hd.counter_base = time;
    hd.counter_start = SDL_GetPerformanceCounter();
    hd.paused_ticks = 0;
    if (SDL_GetAtomicInt(&hd.paused)) {
        hd.paused_at = hd.counter_start;
    }
    SDL_SetAtomicInt(&hd.counter_clock, 1);
    unlock();
}

/* Opens the sound, if the file has one the system can decode: its own decoder
 * and audio stream (paused until the video starts). Returns 0 if there is no
 * sound to play; the video then plays silent. */
static int open_audio(void)
{
    KhdaysHdFormat format;
    char why[256] = "";
    memset(&format, 0, sizeof(format));
    hd.audio = khdays_hd_decoder_open(hd.path, KHDAYS_HD_DECODE_SOUND, hd.from, hd.end, &format, why, sizeof(why));
    if (hd.audio == NULL || format.channels <= 0 || format.rate <= 0) {
        fprintf(stderr, "movies: %s plays without sound (%s)\n", name_of(hd.path), why[0] ? why : "it has none");
        if (hd.audio != NULL) {
            khdays_hd_decoder_close(hd.audio);
            hd.audio = NULL;
        }
        return 0;
    }
    {
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, format.channels, format.rate};
        hd.sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
    }
    if (hd.sound == NULL) {
        fprintf(stderr, "movies: no sound for the HD video %s: %s\n", name_of(hd.path), SDL_GetError());
        khdays_hd_decoder_close(hd.audio);
        hd.audio = NULL;
        return 0;
    }
    SDL_SetAudioStreamGain(hd.sound, gain);
    hd.info.channels = format.channels;
    hd.info.rate = format.rate;
    {
        /* what the device takes from the stream at a time, for the clock between its takings */
        SDL_AudioSpec device;
        int frames = 0;
        if (SDL_GetAudioDeviceFormat(SDL_GetAudioStreamDevice(hd.sound), &device, &frames) && frames > 0 &&
            device.freq > 0) {
            hd.device_buffer = SDL_min((double)frames / device.freq, 0.25);
        } else {
            fprintf(stderr, "movies: the sound device of %s does not say its buffer (%s): its clock moves in steps\n",
                    name_of(hd.path), SDL_GetError());
        }
    }
    follow_latency();
    return 1;
}

/* Opens the file and decodes its first picture and a little of its sound.
 * Returns whether the video can play (if not, hd.why says why). */
static int open_and_begin(void)
{
    KhdaysHdFormat format;
    memset(&format, 0, sizeof(format));
    hd.video = khdays_hd_decoder_open(hd.path, KHDAYS_HD_DECODE_PICTURES, hd.from, hd.end, &format, hd.why,
                                      sizeof(hd.why));
    if (hd.video == NULL) {
        if (hd.why[0] == '\0') {
            snprintf(hd.why, sizeof(hd.why), "the file cannot be opened");
        }
        return 0;
    }
    hd.info.width = format.width & ~1;
    hd.info.height = format.height & ~1;
    hd.info.fps = format.fps;
    hd.info.duration = format.duration;
    if (hd.from > 0.0 && format.duration > 0.0 && hd.from >= format.duration) {
        /* said here, by name, whatever the system's reader would make of a start past the end */
        snprintf(hd.why, sizeof(hd.why), "the part asked of it begins at %.2f s, past its end (it is %.2f s long)",
                 hd.from, format.duration);
        return 0;
    }
    if (hd.end > 0.0 && format.duration > 0.0 && hd.end >= format.duration) {
        /* a part that ends past the file's end is the file to its end: its fade out is then the file's
         * last moments, not ones it does not have (a video shorter than the one the part was written for) */
        fprintf(stderr, "movies: %s is %.3f s long: the part asked to %.3f s plays to its end\n", name_of(hd.path),
                format.duration, hd.end);
        hd.end = 0.0;
    }
    hd.duration = format.duration;
    while (hd.count == 0 && !hd.video_eos) { /* the first picture */
        const int read = read_picture();
        if (!read) {
            break;
        }
        if (read == 2) {
            SDL_Delay(20); /* none now (the decoder says how long it goes on trying): asked again */
        }
    }
    if (hd.count == 0) {
        if (hd.why[0] == '\0') {
            snprintf(hd.why, sizeof(hd.why), "the file has no picture");
        }
        return 0;
    }
    hd.first_time = hd.pictures[0].time;
    hd.info.width = hd.pictures[0].width; /* as decoded: what the window is given */
    hd.info.height = hd.pictures[0].height;
    if (open_audio()) {
        while (!hd.sound_eos && sound_queued() < SOUND_AHEAD) {
            if (!read_sound()) {
                break; /* none now: read as the video plays */
            }
        }
    } else {
        use_counter_clock(hd.first_time);
    }
    return 1;
}

/* Once the sound has all played -- or what is left of it has not moved for a
 * quarter of a second -- the clock goes on from the sound's end on the
 * performance counter. */
static void follow_sound_end(void)
{
    const int queued = SDL_GetAudioStreamQueued(hd.sound);
    if (queued != hd.last_queued || hd.still_since == 0) {
        hd.last_queued = queued;
        hd.still_since = SDL_GetPerformanceCounter();
    }
    if (queued <= 0 || SDL_GetPerformanceCounter() - hd.still_since > hd.frequency / 4) {
        use_counter_clock(clock_now());
    }
}

/* One turn of the thread while the video plays: decodes pictures and sound to
 * keep them a little ahead of the clock. Returns 1 if it decoded something, 0
 * if nothing was needed, or -1 if a picture cannot be decoded. */
static int decode_ahead(void)
{
    const double now = clock_now();
    int did = 0;
    if (!hd.video_eos && newest_time() < now + PICTURES_AHEAD) {
        const int read = read_picture();
        if (!read) {
            return -1;
        }
        did = read == 1; /* (2: none now, asked again at the next turn) */
    }
    if (hd.sound != NULL && !hd.sound_eos && sound_queued() < SOUND_AHEAD && read_sound()) {
        did = 1;
    }
    follow_latency();
    if (SDL_GetAtomicInt(&hd.started) && hd.sound != NULL && !SDL_GetAtomicInt(&hd.counter_clock) && hd.sound_eos) {
        follow_sound_end();
    }
    return did;
}

/* The video's thread: opens the file, then keeps decoding ahead until it is
 * told to stop or a picture fails. */
static int SDLCALL run(void *unused)
{
    (void)unused;
    if (open_and_begin()) {
        hd.open_ok = 1;
        hd.status = 0;
    }
    SDL_SignalSemaphore(hd.opened);
    while (hd.open_ok && !SDL_GetAtomicInt(&hd.stop)) {
        const int paused = SDL_GetAtomicInt(&hd.paused);
        const int did = paused ? 0 : decode_ahead();
        khdays_theater_music_tick(clock_now(), SDL_GetAtomicInt(&hd.started), paused);
        if (did < 0) {
            fprintf(stderr, "movies: %s FAILED at %.3f s after %ld pictures: %s\n", name_of(hd.path), clock_now(),
                    hd.decoded, hd.why);
            lock();
            hd.status = -1;
            unlock();
            break;
        }
        if (did == 0) {
            SDL_WaitSemaphoreTimeout(hd.wake, 2); /* nothing to do: wait a little, or until woken */
        }
    }
    if (hd.video != NULL) {
        khdays_hd_decoder_close(hd.video);
        hd.video = NULL;
    }
    if (hd.audio != NULL) {
        khdays_hd_decoder_close(hd.audio);
        hd.audio = NULL;
    }
    return 0;
}

/* The video closed; the theater's music with it, unless `next_follows` (another video opens at once)
 * and the music goes on into it. */
static void close_video(int next_follows)
{
    khdays_theater_music_close(next_follows);
    if (hd.thread == NULL) {
        return;
    }
    SDL_SetAtomicInt(&hd.stop, 1);
    SDL_SignalSemaphore(hd.wake);
    SDL_WaitThread(hd.thread, NULL);
    SDL_DestroySemaphore(hd.opened);
    SDL_DestroySemaphore(hd.wake);
    if (hd.open_ok) {
        const double sound = hd.info.rate > 0 ? (double)hd.put_frames / hd.info.rate : 0.0;
        const int queued = hd.sound != NULL ? SDL_GetAudioStreamQueued(hd.sound) : 0;
        const double unheard = hd.info.rate > 0 && hd.info.channels > 0 && queued > 0
                                   ? (double)queued / (double)(hd.info.channels * (int)sizeof(float)) / hd.info.rate
                                   : 0.0;
        fprintf(stderr,
                "movies: close %s at %.3f s (%s): %ld pictures decoded, %ld shown, %ld late (passed over), its "
                "picture at most %.1f ms off its clock; %.2f s of sound given, %.2f s of it not played\n",
                name_of(hd.path), clock_now(),
                hd.status == -1                     ? "failed"
                : !SDL_GetAtomicInt(&hd.started)    ? "never started"
                : hd.end_said                       ? "ended"
                : SDL_GetAtomicInt(&hd.paused) != 0 ? "closed while paused"
                                                    : "closed while playing",
                hd.decoded, hd.shown, hd.late, hd.worst_offset * 1000.0, sound, unheard);
    }
    {
        /* The stream is taken away under both locks before it is destroyed outside them (closing a
         * device can wait on SDL's audio thread): the window's thread, which reads the clock under them
         * while this one closes, never looks at a freed stream; and with nothing open left to show, it
         * takes no picture on a clock that lost its sound. */
        SDL_AudioStream *sound;
        lock();
        clock_lock();
        sound = hd.sound;
        hd.sound = NULL;
        hd.open_ok = 0;
        hd.status = 2;
        clock_unlock();
        unlock();
        if (sound != NULL) {
            SDL_DestroyAudioStream(sound); /* the sound stops at once */
        }
    }
    lock();
    for (int i = 0; i < hd.count; ++i) {
        free(hd.pictures[i].data);
    }
    for (int i = 0; i < hd.spare_count; ++i) {
        free(hd.spare[i].data);
    }
    free(hd.pictures);
    free(hd.spare);
    SDL_free(hd.path);
    memset(&hd, 0, sizeof(hd));
    hd.status = 2;
    hd.taken = -1;
    unlock();
}

void khdays_hd_close(void)
{
    close_video(0);
}

void khdays_hd_close_scene_end(void)
{
    close_video(2);
}

int khdays_hd_open(const char *path, KhdaysHdInfo *info, char *why, size_t size)
{
    return khdays_hd_open_part(path, 0.0, 0.0, info, why, size);
}

int khdays_hd_open_part(const char *path, double from, double until, KhdaysHdInfo *info, char *why, size_t size)
{
    close_video(1);
    hd.from = from > 0.0 ? from : 0.0;
    hd.end = until > hd.from ? until : 0.0;
    hd.fade_in = next_fade_in;
    hd.fade_out = next_fade_out;
    next_fade_in = next_fade_out = 0.0;
    hd.frequency = SDL_GetPerformanceFrequency();
    hd.path = path != NULL ? SDL_strdup(path) : NULL;
    if (hd.path == NULL) {
        snprintf(why, size, "no memory for its path");
        fprintf(stderr, "movies: open FAILED: no path, or no memory for it\n");
        return 0;
    }
    hd.opened = SDL_CreateSemaphore(0);
    hd.wake = SDL_CreateSemaphore(0);
    hd.thread = hd.opened != NULL && hd.wake != NULL ? SDL_CreateThread(run, "khdays hd video", NULL) : NULL;
    if (hd.thread == NULL) {
        snprintf(why, size, "no thread for it (%s)", SDL_GetError());
        fprintf(stderr, "movies: open %s FAILED: %s\n", path, why);
        if (hd.opened != NULL) {
            SDL_DestroySemaphore(hd.opened);
        }
        if (hd.wake != NULL) {
            SDL_DestroySemaphore(hd.wake);
        }
        hd.opened = hd.wake = NULL;
        SDL_free(hd.path);
        hd.path = NULL;
        return 0;
    }
    SDL_WaitSemaphore(hd.opened); /* the thread always signals it, opened or not */
    if (!hd.open_ok) {
        snprintf(why, size, "%s", hd.why[0] ? hd.why : "it cannot be played");
        fprintf(stderr, "movies: open %s FAILED: %s\n", path, why);
        khdays_hd_close();
        return 0;
    }
    *info = hd.info;
    {
        char part[96] = "whole", sound[96] = "no sound";
        if (hd.from > 0.0 || hd.end > 0.0) {
            if (hd.end > 0.0) {
                snprintf(part, sizeof(part), "part %.3f to %.3f s", hd.from, hd.end);
            } else {
                snprintf(part, sizeof(part), "part %.3f s to its end", hd.from);
            }
        }
        if (hd.sound != NULL) {
            snprintf(sound, sizeof(sound), "sound %d channels %d Hz as floats", hd.info.channels, hd.info.rate);
        }
        fprintf(stderr,
                "movies: open %s: %dx%d, %.3f frames a second, %.3f s long, %s, %s, fades in %.2f s out %.2f s, "
                "first picture at %.3f s\n",
                path, hd.info.width, hd.info.height, hd.info.fps, hd.duration, sound, part, hd.fade_in, hd.fade_out,
                hd.first_time);
    }
    khdays_theater_music_open(path);
    return 1;
}

void khdays_hd_fades(double in, double out)
{
    next_fade_in = in > 0.0 ? in : 0.0;
    next_fade_out = out > 0.0 ? out : 0.0;
}

void khdays_hd_start(void)
{
    if (!hd.open_ok || SDL_GetAtomicInt(&hd.started)) {
        return;
    }
    lock();
    {
        const Uint64 counter = SDL_GetPerformanceCounter();
        if (SDL_GetAtomicInt(&hd.counter_clock)) {
            hd.counter_start = counter;
        }
        if (SDL_GetAtomicInt(&hd.paused)) {
            hd.paused_at = counter; /* paused before its start: the pause counts from the start only */
        }
    }
    SDL_SetAtomicInt(&hd.started, 1);
    if (hd.sound != NULL && !SDL_GetAtomicInt(&hd.paused)) {
        SDL_ResumeAudioStreamDevice(hd.sound); /* (paused before its start: it stays silent until resumed) */
    }
    unlock();
    fprintf(stderr, "movies: start %s at %.3f s, its clock %s\n", name_of(hd.path), clock_now(),
            hd.sound != NULL ? "the sound played" : "the counter (no sound)");
    SDL_SignalSemaphore(hd.wake);
}

void khdays_hd_pause(int pause)
{
    if (!hd.open_ok || (SDL_GetAtomicInt(&hd.paused) != 0) == (pause != 0)) {
        return;
    }
    lock();
    if (pause) {
        hd.paused_at = SDL_GetPerformanceCounter();
    } else {
        const Uint64 away = SDL_GetPerformanceCounter() - hd.paused_at;
        hd.paused_ticks += away;
        clock_lock();
        if (hd.taken_at != 0) {
            hd.taken_at += away; /* the sound's clock: the time since the device's last taking, less the pause */
        }
        clock_unlock();
    }
    SDL_SetAtomicInt(&hd.paused, pause != 0);
    if (hd.sound != NULL) {
        if (pause) {
            SDL_PauseAudioStreamDevice(hd.sound);
        } else if (SDL_GetAtomicInt(&hd.started)) {
            SDL_ResumeAudioStreamDevice(hd.sound);
        }
    }
    unlock();
    fprintf(stderr, "movies: %s %s at %.3f s\n", pause ? "pause" : "resume", name_of(hd.path), clock_now());
    SDL_SignalSemaphore(hd.wake);
}

int khdays_hd_status(char *why, size_t size)
{
    static int trace = -1;
    static Uint64 traced_at;
    int status;
    if (trace < 0) {
        trace = getenv("KHDAYS_TRACE_MOVIE_HD") != NULL;
    }
    lock();
    if (trace && SDL_GetTicks() - traced_at >= 1000) {
        /* with KHDAYS_TRACE_MOVIE_HD: once a second, where the video is */
        traced_at = SDL_GetTicks();
        fprintf(stderr,
                "movie hd: status %d, started %d, clock %.3f, pictures %d (newest %.3f), taken %ld, "
                "decoded %ld, video end %d, sound end %d, queued %d, counter clock %d\n",
                hd.status, SDL_GetAtomicInt(&hd.started), clock_now(), hd.count,
                hd.count > 0 ? hd.pictures[hd.count - 1].time : -1.0, hd.taken, hd.decoded, hd.video_eos, hd.sound_eos,
                hd.sound != NULL ? SDL_GetAudioStreamQueued(hd.sound) : -1, SDL_GetAtomicInt(&hd.counter_clock));
    }
    status = hd.status;
    if (status == 0 && SDL_GetAtomicInt(&hd.started) && hd.video_eos &&
        (hd.sound == NULL || SDL_GetAtomicInt(&hd.counter_clock))) {
        /* every picture is decoded; it has ended once the last one was taken and its time has passed */
        const double last = hd.count > 0 ? hd.pictures[hd.count - 1].time : hd.first_time;
        const double frame = hd.info.fps > 0.0 ? 1.0 / hd.info.fps : 0.0;
        if ((hd.count == 0 || hd.pictures[hd.count - 1].number == hd.taken) && clock_now() >= last + frame) {
            status = 1;
            if (!hd.end_said) {
                hd.end_said = 1;
                fprintf(stderr, "movies: end %s at %.3f s: %ld pictures shown, %ld late (passed over), %.2f s of sound\n",
                        name_of(hd.path), clock_now(), hd.shown, hd.late,
                        hd.info.rate > 0 ? (double)hd.put_frames / hd.info.rate : 0.0);
            }
        }
    }
    if (status == -1 && why != NULL && size > 0) {
        snprintf(why, size, "%s", hd.why);
    }
    unlock();
    return status;
}

void khdays_hd_set_gain(float g)
{
    lock(); /* (against the closing, which takes the stream away under it) */
    gain = g;
    if (hd.sound != NULL) {
        SDL_SetAudioStreamGain(hd.sound, g);
    }
    unlock();
}

int khdays_hd_picture_begin(KhdaysHdPicture *picture)
{
    double now, half;
    int pick = -1;
    lock();
    if (!hd.open_ok || hd.count == 0) {
        unlock();
        return 0;
    }
    now = clock_now();
    half = hd.info.fps > 0.0 ? 0.5 / hd.info.fps : 0.0;
    for (int i = 0; i < hd.count && hd.pictures[i].time <= now + half; ++i) {
        pick = i;
    }
    if (pick < 0 || hd.pictures[pick].number == hd.taken) {
        unlock();
        return 0;
    }
    for (int i = 0; i < pick; ++i) {
        if (hd.pictures[i].number != hd.taken) {
            ++hd.late; /* its time passed before a refresh took it: it was never shown */
        }
        keep_spare(hd.pictures[i]);
    }
    memmove(hd.pictures, hd.pictures + pick, (size_t)(hd.count - pick) * sizeof(Picture));
    hd.count -= pick;
    hd.taken = hd.pictures[0].number;
    ++hd.shown;
    if (SDL_GetAtomicInt(&hd.started)) {
        const double off = now - hd.pictures[0].time;
        if ((off < 0 ? -off : off) > hd.worst_offset) {
            hd.worst_offset = off < 0 ? -off : off;
        }
    }
    picture->width = hd.pictures[0].width;
    picture->height = hd.pictures[0].height;
    picture->pitch = hd.pictures[0].width;
    picture->luma = hd.pictures[0].data;
    picture->chroma = hd.pictures[0].data + (size_t)hd.pictures[0].width * (size_t)hd.pictures[0].height;
    picture->time = hd.pictures[0].time;
    picture->fps = hd.info.fps;
    picture->level = fade_level(hd.pictures[0].time);
    SDL_SignalSemaphore(hd.wake); /* there is room ahead now: the thread decodes on */
    return 1;                     /* the lock stays held until khdays_hd_picture_done */
}

void khdays_hd_picture_done(void)
{
    unlock();
}

static uint8_t clamp8(int v)
{
    return (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
}

int khdays_hd_picture_rgba(uint8_t **rgba, size_t *capacity, int *w, int *h)
{
    int found = 0;
    lock();
    for (int i = 0; i < hd.count && !found; ++i) {
        const Picture *p = &hd.pictures[i];
        size_t need;
        if (p->number != hd.taken) {
            continue;
        }
        found = 1;
        need = (size_t)p->width * (size_t)p->height * 4u;
        if (*capacity < need) {
            uint8_t *grown = (uint8_t *)SDL_realloc(*rgba, need);
            if (grown == NULL) {
                found = 0;
                break;
            }
            *rgba = grown;
            *capacity = need;
        }
        *w = p->width;
        *h = p->height;
        /* NV12: the luma, then the chroma at half the rows, U and V interleaved; BT.709 limited range
         * in 16.16 fixed point (Y' = 1.164 (Y - 16), R += 1.793 V, G -= 0.213 U + 0.533 V, B += 2.112 U) */
        for (int y = 0; y < p->height; ++y) {
            const uint8_t *luma = p->data + (size_t)y * (size_t)p->width;
            const uint8_t *chroma =
                p->data + (size_t)p->width * (size_t)p->height + (size_t)(y / 2) * (size_t)p->width;
            uint8_t *out = *rgba + (size_t)y * (size_t)p->width * 4u;
            for (int x = 0; x < p->width; ++x) {
                const int yy = 76284 * (luma[x] - 16);
                const int u = chroma[(x & ~1)] - 128, v = chroma[(x & ~1) + 1] - 128;
                out[4 * x + 0] = clamp8((yy + 117504 * v + 32768) >> 16);
                out[4 * x + 1] = clamp8((yy - 13954 * u - 34903 * v + 32768) >> 16);
                out[4 * x + 2] = clamp8((yy + 138392 * u + 32768) >> 16);
                out[4 * x + 3] = 255;
            }
        }
    }
    unlock();
    return found;
}

/* --- A voice over the game (khdays_hd_voice_play) ---------------------------------------------- */

static float voice_gain = 1.0f;    /* khdays_hd_voice_set_gain */
static SDL_AtomicInt voice_paused; /* khdays_hd_voice_pause */

static struct {
    SDL_Thread *thread;
    SDL_AtomicInt stop;
    char **files;
    int count;
    SDL_AudioStream *sound; /* opened by the thread for the first file; destroyed once it has ended */
    /* The voice's clock, under the voice's lock: the seconds of sound put into the stream, where each
     * file began among them, the files begun, their videos' names ("hd007b"), the format queued now,
     * and whether the thread has put all it will. */
    double put;
    double *began; /* as many as the files */
    int begun;
    char **names;
    int channels, rate;
    SDL_AtomicInt done;
    Uint64 quiet_since; /* when the last of it was heard (SDL's ms; 0: not yet) */
} voice;

/* Guards the voice's stream against the pause and the volume. */
static void voice_lock(void)
{
    SDL_LockMutex(lock_of(&voice_slot));
}
static void voice_unlock(void)
{
    SDL_UnlockMutex(lock_of(&voice_slot));
}

static void *voice_stream_now(void)
{
    void *stream;
    voice_lock();
    stream = voice.sound;
    voice_unlock();
    return stream;
}

/* The voice's stream, opened for `spec` the first time, else told the next file's format (what is
 * queued plays on in its own). NULL if the system gives no sound. */
static SDL_AudioStream *voice_stream(const SDL_AudioSpec *spec)
{
    SDL_AudioStream *stream;
    voice_lock();
    if (voice.sound == NULL) {
        voice.sound = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, spec, NULL, NULL);
        if (voice.sound == NULL) {
            fprintf(stderr, "movies: no sound for the voice: %s\n", SDL_GetError());
        } else {
            SDL_SetAudioStreamGain(voice.sound, voice_gain);
            if (!SDL_GetAtomicInt(&voice_paused)) {
                SDL_ResumeAudioStreamDevice(voice.sound);
            }
        }
    } else {
        SDL_SetAudioStreamFormat(voice.sound, spec, NULL);
    }
    stream = voice.sound;
    voice_unlock();
    return stream;
}

/* One file's sound into the voice's stream, a little ahead of the device, to its end or a stop. */
static void voice_file(const char *file, int nth)
{
    KhdaysHdFormat format;
    KhdaysHdDecoder *decoder;
    SDL_AudioStream *stream = NULL;
    char why[256] = "";
    double put = 0.0; /* seconds of sound put into the stream */
    memset(&format, 0, sizeof(format));
    decoder = khdays_hd_decoder_open(file, KHDAYS_HD_DECODE_SOUND, 0.0, 0.0, &format, why, sizeof(why));
    if (decoder == NULL || format.channels <= 0 || format.rate <= 0) {
        fprintf(stderr, "movies: no voice in %s (%s)\n", file, why[0] ? why : "it has no sound");
    } else {
        const SDL_AudioSpec spec = {SDL_AUDIO_F32, format.channels, format.rate};
        stream = voice_stream(&spec);
        if (stream != NULL) {
            voice_lock();
            if (nth < voice.count) {
                voice.began[nth] = voice.put;
                voice.begun = nth + 1;
            }
            voice.channels = format.channels;
            voice.rate = format.rate;
            voice_unlock();
            fprintf(stderr, "movies: voice start %s: %d channels %d Hz as floats, %.3f s long\n", file,
                    format.channels, format.rate, format.duration);
        }
    }
    while (stream != NULL && !SDL_GetAtomicInt(&voice.stop)) {
        float *samples = NULL;
        int frames = 0, got;
        double at = 0.0;
        const double queued =
            (double)SDL_GetAudioStreamQueued(stream) / (double)(format.channels * (int)sizeof(float)) / format.rate;
        if (SDL_GetAtomicInt(&voice_paused) || queued >= SOUND_AHEAD) {
            SDL_Delay(10);
            continue;
        }
        got = khdays_hd_decoder_sound(decoder, &samples, &frames, &at, why, sizeof(why));
        if (got == KHDAYS_HD_DECODER_LATER) {
            SDL_Delay(10); /* none now: asked again */
            continue;
        }
        if (got < 0) {
            fprintf(stderr, "movies: the voice in %s cannot be decoded (%s); it ends there\n", file,
                    why[0] ? why : "no reason given");
            break;
        }
        if (got == 0) {
            break;
        }
        if (samples != NULL && frames > 0) {
            if (sound_tap != NULL) {
                sound_tap(1, samples, frames, format.channels, format.rate, at);
            }
            voice_lock();
            SDL_PutAudioStreamData(stream, samples, frames * format.channels * (int)sizeof(float));
            voice.put += (double)frames / format.rate;
            voice_unlock();
            put += (double)frames / format.rate;
        }
    }
    if (stream != NULL) {
        SDL_FlushAudioStream(stream); /* what the resampler holds back plays too */
        fprintf(stderr, "movies: voice end %s: %.1f s of its sound %s\n", file, put,
                SDL_GetAtomicInt(&voice.stop) ? "before it was stopped" : "to its end");
    }
    if (decoder != NULL) {
        khdays_hd_decoder_close(decoder);
    }
}

/* The voice's thread: the files' sound one after another; the stream plays on what is queued. */
static int SDLCALL voice_run(void *unused)
{
    (void)unused;
    for (int i = 0; i < voice.count && !SDL_GetAtomicInt(&voice.stop); ++i) {
        voice_file(voice.files[i], i);
    }
    SDL_SetAtomicInt(&voice.done, 1);
    return 0;
}

void khdays_hd_voice_stop(void)
{
    if (voice.thread == NULL) {
        return;
    }
    SDL_SetAtomicInt(&voice.stop, 1);
    SDL_WaitThread(voice.thread, NULL);
    voice_lock();
    if (voice.sound != NULL) {
        SDL_DestroyAudioStream(voice.sound); /* the voice stops at once */
    }
    for (int i = 0; i < voice.count; ++i) {
        SDL_free(voice.files[i]);
        SDL_free(voice.names[i]);
    }
    free(voice.files);
    free(voice.names);
    free(voice.began);
    memset(&voice, 0, sizeof(voice));
    voice_unlock();
}

void khdays_hd_voice_play(const char *const *paths, int count)
{
    char **files, **names;
    double *began;
    int n = 0;
    khdays_hd_voice_stop();
    if (count <= 0) {
        return;
    }
    files = (char **)calloc((size_t)count, sizeof(*files));
    names = (char **)calloc((size_t)count, sizeof(*names));
    began = (double *)calloc((size_t)count, sizeof(*began));
    if (files == NULL || names == NULL || began == NULL) {
        free(files);
        free(names);
        free(began);
        return;
    }
    for (int i = 0; i < count; ++i) {
        char *file = paths[i] != NULL ? SDL_strdup(paths[i]) : NULL;
        const char *base = paths[i];
        char *dot;
        if (file == NULL) {
            continue;
        }
        /* its video's name, for its subtitles: the path's file name without its extension */
        for (const char *p = paths[i]; *p; ++p) {
            if (*p == '/' || *p == '\\') {
                base = p + 1;
            }
        }
        names[n] = SDL_strdup(base);
        dot = names[n] != NULL ? strrchr(names[n], '.') : NULL;
        if (dot != NULL) {
            *dot = '\0';
        }
        files[n++] = file;
    }
    voice.files = files;
    voice.names = names;
    voice.began = began;
    voice.count = n;
    voice.thread = SDL_CreateThread(voice_run, "khdays hd voice", NULL);
    if (voice.thread == NULL) {
        fprintf(stderr, "movies: no thread for the voice (%s): it is not heard\n", SDL_GetError());
        for (int i = 0; i < n; ++i) {
            SDL_free(files[i]);
            SDL_free(names[i]);
        }
        free(files);
        free(names);
        free(began);
        memset(&voice, 0, sizeof(voice));
    }
}

/* The seconds of the stream's sound not heard yet. Under the voice's lock. */
static double voice_unheard(void)
{
    if (voice.sound == NULL || voice.channels <= 0 || voice.rate <= 0) {
        return 0.0;
    }
    return (double)SDL_GetAudioStreamQueued(voice.sound) / (double)(voice.channels * (int)sizeof(float)) / voice.rate;
}

int khdays_hd_voice_clock(char *name, size_t size, double *seconds)
{
    int found = 0;
    voice_lock();
    if (voice.thread != NULL && voice.sound != NULL && voice.begun > 0) {
        const double heard = voice.put - voice_unheard();
        int file = 0;
        for (int i = 0; i < voice.begun; ++i) {
            if (voice.began[i] <= heard) {
                file = i;
            }
        }
        snprintf(name, size, "%s", voice.names[file] != NULL ? voice.names[file] : "");
        *seconds = heard - voice.began[file] > 0.0 ? heard - voice.began[file] : 0.0;
        found = 1;
    }
    voice_unlock();
    return found;
}

int khdays_hd_voice_speaking(void)
{
    int speaking = 0;
    voice_lock();
    if (voice.thread != NULL) {
        if (!SDL_GetAtomicInt(&voice.done) || SDL_GetAtomicInt(&voice_paused) || voice_unheard() > 0.0) {
            voice.quiet_since = 0;
            speaking = 1;
        } else {
            /* the last word heard: a moment's silence after it, as the theater leaves */
            if (voice.quiet_since == 0) {
                voice.quiet_since = SDL_GetTicks() + 1; /* (never 0) */
            }
            speaking = SDL_GetTicks() + 1 - voice.quiet_since < 700;
        }
    }
    voice_unlock();
    return speaking;
}

void khdays_hd_voice_pause(int pause)
{
    voice_lock();
    if ((SDL_GetAtomicInt(&voice_paused) != 0) != (pause != 0) && voice.thread != NULL) {
        fprintf(stderr, "movies: voice %s\n", pause ? "pause" : "resume");
    }
    SDL_SetAtomicInt(&voice_paused, pause != 0);
    if (voice.sound != NULL) {
        if (pause) {
            SDL_PauseAudioStreamDevice(voice.sound);
        } else {
            SDL_ResumeAudioStreamDevice(voice.sound);
        }
    }
    voice_unlock();
}

void khdays_hd_voice_set_gain(float g)
{
    voice_lock();
    voice_gain = g;
    if (voice.sound != NULL) {
        SDL_SetAudioStreamGain(voice.sound, g);
    }
    voice_unlock();
}
