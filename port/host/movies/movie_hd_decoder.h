/* What the HD video's shared side (movie_hd_core.c) asks of a system's decoder: one file opened for its
 * pictures or for its sound, read forward from a place in it. The core opens one of each for a video
 * (each read at its own pace, on the video's thread) and one for the sound alone of each voice file.
 * AVFoundation's is movie_hd_apple.m; the core's test has one that makes pictures and sound up
 * (tests/movie_hd_core_test.c). Windows keeps its own whole player (movie_hd_win.c). */
#ifndef KHDAYS_HOST_MOVIE_HD_DECODER_H
#define KHDAYS_HOST_MOVIE_HD_DECODER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KhdaysHdDecoder KhdaysHdDecoder;

enum { KHDAYS_HD_DECODE_PICTURES = 1, KHDAYS_HD_DECODE_SOUND = 2 };

typedef struct KhdaysHdFormat {
    int width, height;  /* the picture shown (the display area); pictures only */
    double fps;         /* the file's frame rate, 0 when it does not say; pictures only */
    double duration;    /* how long the file says it lasts, 0 when it does not say */
    int channels, rate; /* the sound as it will be read (interleaved floats); sound only */
} KhdaysHdFormat;

/* Opens `path` (UTF-8) for `what` (one of the two), to be read from `from` seconds of it (0 its start)
 * up to `until` (0 its end). What is read may begin a little before `from` (the key picture before it,
 * a whole block of sound) and need not stop at `until`: the core leaves out what lies outside.
 * Returns NULL with the reason in `why`: for the sound, also when the file has none. */
KhdaysHdDecoder *khdays_hd_decoder_open(const char *path, int what, double from, double until, KhdaysHdFormat *format,
                                        char *why, size_t size);

/* One picture as NV12, BT.709 limited range: `luma` of `height` rows and `chroma` (U and V
 * interleaved) of height / 2 rows, each row `width` bytes of its pitch. */
typedef struct KhdaysHdDecoded {
    const uint8_t *luma, *chroma;
    int luma_pitch, chroma_pitch;
    int width, height;
    double time; /* its presentation time in the file, in seconds */
} KhdaysHdDecoded;

/* What the two reading calls below may also return: nothing now, and no failure -- the decoder is
 * getting its reader back (the system took it away: the app was in the background) and is to be asked
 * again a little later. It never sleeps long itself, so that a closing is not kept waiting; it says on
 * the console what it tries, and fails (-1) once it has tried long enough. */
#define KHDAYS_HD_DECODER_LATER 2

/* The next picture, in presentation order. Returns 1 with `out` (valid until the next call for this
 * decoder or its closing), 0 at the end of what there is to read, -1 on a failure with the reason in
 * `why`, or KHDAYS_HD_DECODER_LATER. */
int khdays_hd_decoder_picture(KhdaysHdDecoder *decoder, KhdaysHdDecoded *out, char *why, size_t size);

/* The next block of sound: `*frames` frames of the format's channels as interleaved floats at
 * `*samples` (valid until the next call for this decoder or its closing; the core may write into
 * them), the first one's time in the file in `*time`. Returns 1, 0 at the end, -1 on a failure with the
 * reason in `why`, or KHDAYS_HD_DECODER_LATER. Blocks follow one another without a gap. */
int khdays_hd_decoder_sound(KhdaysHdDecoder *decoder, float **samples, int *frames, double *time, char *why,
                            size_t size);

void khdays_hd_decoder_close(KhdaysHdDecoder *decoder);

/* How long after its turn to play a buffer of sound is really heard on this system, in seconds: what
 * the system queues ahead of the one playing, and the output route's own delay (large on wireless
 * earphones). `buffer` is what SDL's device takes from a stream at a time, in seconds. The core takes
 * it off the video's clock, so that the picture goes with the sound heard, not the sound handed over.
 * 0 where it is not known. Asked about once a second while a video with sound plays. */
double khdays_hd_decoder_output_latency(double buffer);

/* Tests only: every block of sound the core gives an audio stream, as given (after the part's cut and
 * the fades): `voice` 0 the video's, 1 a voice's. NULL for none. */
typedef void (*KhdaysHdSoundTap)(int voice, const float *samples, int frames, int channels, int rate, double time);
void khdays_hd_core_sound_tap(KhdaysHdSoundTap tap);

/* Tests only: the video's audio stream now (`voice` 0) or the voice's (1), an SDL_AudioStream * or
 * NULL: for its gain and whether its device is paused. */
void *khdays_hd_core_stream(int voice);

#ifdef __cplusplus
}
#endif

#endif
