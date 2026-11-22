/* The HD video's decoder on Apple systems (movie_hd_decoder.h, for movie_hd_core.c): AVFoundation's
 * AVAssetReader on one track of the file -- its pictures as NV12 (video range) pixel buffers, or its
 * sound as interleaved floats at the file's rate -- read forward from a place in it. iOS 16 and macOS
 * 13 on; no main thread needed: each decoder is opened, read and closed on the player's own thread.
 *
 * Manual reference counting (as the app's ios_main.m is compiled: no ARC): everything alloc'ed or
 * copied here is released here; each call drains its own autorelease pool.
 *
 * A reader that fails part-way is opened again where it stopped. Two kinds of failure:
 *  - interrupted (AVErrorOperationInterrupted, AVErrorMediaServicesWereReset,
 *    AVErrorSessionWasInterrupted: an app sent to the background loses its decoding session): opened
 *    again at once, then every INTERRUPTED_APART seconds for up to INTERRUPTED_TRIES tries in a row
 *    without a sample read between. Between tries the call returns KHDAYS_HD_DECODER_LATER instead of
 *    sleeping, so the player can still be closed, and a try is only spent while the app runs (a
 *    suspended app spends none). The window also pauses the video while the app is in the background
 *    (host/app/window.c), so that nothing is read there at all; this is for what slips through.
 *  - anything else: twice at most (REOPENINGS) without a sample read between; a file that is really
 *    broken fails again at once and is said so.
 * None of this has run on a device: whether iOS reports a lost session with these codes is not proven.
 *
 * It cannot run where it is written, so every step is said on stderr ("movies: avf: ..."), every
 * failure with its NSError's domain, code and text. */
#import <AVFoundation/AVFoundation.h>
#import <CoreMedia/CoreMedia.h>
#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>

#include "movie_hd_decoder.h"

#include <TargetConditionals.h>
#include <dispatch/dispatch.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__has_feature) && __has_feature(objc_arc)
#error "movie_hd_apple.m counts its references itself: compile it with -fno-objc-arc"
#endif

#define REOPENINGS 2            /* in a row, without a sample read between; not after an interruption */
#define INTERRUPTED_TRIES 40    /* reopenings in a row after an interruption, without a sample read between */
#define INTERRUPTED_APART 0.25  /* seconds between them */
#define LOAD_SECONDS 15.0 /* how long the file's tracks may take to be read */

struct KhdaysHdDecoder {
    int what;
    char name[128]; /* the file's name, for the messages */
    AVURLAsset *asset;
    AVAssetTrack *track;
    AVAssetReader *reader;
    AVAssetReaderTrackOutput *output;
    double start, until; /* what the reader was first asked for (0 until: the file's end) */
    double fps;
    long given;      /* pictures or sound blocks given to the core */
    double next;     /* where a reader opened again goes on: the last picture's time; the sound's end so far */
    int reopenings;  /* in a row */
    int interrupted; /* reopenings after an interruption, in a row */
    int waiting;     /* interrupted, its reader let go: opened again at `retry_at` (no_sample) */
    double retry_at; /* in seconds_now()'s time */
    int first_said;  /* the first picture's or block's layout has been said */
    /* the picture given last: locked until the next call */
    CVPixelBufferRef held;
    /* the sound */
    int channels, rate;
    float *samples;
    size_t capacity; /* in floats */
};

/* A four-character code as text. */
static const char *fourcc(uint32_t code, char text[8])
{
    for (int i = 0; i < 4; ++i) {
        const char c = (char)((code >> (24 - 8 * i)) & 0xFF);
        text[i] = c >= 32 && c < 127 ? c : '?';
    }
    text[4] = '\0';
    return text;
}

/* An NSError as text: its domain, code and words, and those of the error under it. */
static void error_text(NSError *error, char *text, size_t size)
{
    if (error == nil) {
        snprintf(text, size, "no NSError given");
        return;
    }
    {
        NSError *under = [error.userInfo objectForKey:NSUnderlyingErrorKey];
        NSString *reason = error.localizedFailureReason;
        int n = snprintf(text, size, "%s %ld \"%s\"", error.domain.UTF8String, (long)error.code,
                         error.localizedDescription.UTF8String);
        if (reason != nil && n > 0 && (size_t)n < size) {
            n += snprintf(text + n, size - (size_t)n, " (%s)", reason.UTF8String);
        }
        if ([under isKindOfClass:[NSError class]] && n > 0 && (size_t)n < size) {
            snprintf(text + n, size - (size_t)n, " under it %s %ld \"%s\"", under.domain.UTF8String, (long)under.code,
                     under.localizedDescription.UTF8String);
        }
    }
}

static const char *reader_status_name(AVAssetReaderStatus status)
{
    switch (status) {
    case AVAssetReaderStatusUnknown:
        return "unknown";
    case AVAssetReaderStatusReading:
        return "reading";
    case AVAssetReaderStatusCompleted:
        return "completed";
    case AVAssetReaderStatusFailed:
        return "failed";
    case AVAssetReaderStatusCancelled:
        return "cancelled";
    }
    return "?";
}

static const char *what_name(const KhdaysHdDecoder *d)
{
    return d->what == KHDAYS_HD_DECODE_PICTURES ? "pictures" : "sound";
}

/* Lets go of the picture given last. */
static void release_held(KhdaysHdDecoder *d)
{
    if (d->held != NULL) {
        CVPixelBufferUnlockBaseAddress(d->held, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(d->held);
        d->held = NULL;
    }
}

/* Stops and frees the reader and its output. */
static void release_reader(KhdaysHdDecoder *d)
{
    if (d->reader != nil) {
        if (d->reader.status == AVAssetReaderStatusReading) {
            [d->reader cancelReading];
        }
        [d->reader release];
        d->reader = nil;
    }
    [d->output release];
    d->output = nil;
}

/* A new reader on the decoder's track, reading from `from` seconds. Returns 0 with the reason. */
static int start_reader(KhdaysHdDecoder *d, double from, char *why, size_t size)
{
    NSError *error = nil;
    NSDictionary *settings;
    char text[512];
    release_reader(d);
    if (d->what == KHDAYS_HD_DECODE_PICTURES) {
        /* NV12, video range: two planes, each with its own bytes per row */
        settings = [NSDictionary
            dictionaryWithObject:[NSNumber numberWithUnsignedInt:kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange]
                          forKey:(NSString *)kCVPixelBufferPixelFormatTypeKey];
    } else {
        settings = [NSDictionary dictionaryWithObjectsAndKeys:[NSNumber numberWithUnsignedInt:kAudioFormatLinearPCM],
                                                              AVFormatIDKey, [NSNumber numberWithInt:32],
                                                              AVLinearPCMBitDepthKey, [NSNumber numberWithBool:YES],
                                                              AVLinearPCMIsFloatKey, [NSNumber numberWithBool:NO],
                                                              AVLinearPCMIsBigEndianKey, [NSNumber numberWithBool:NO],
                                                              AVLinearPCMIsNonInterleaved,
                                                              [NSNumber numberWithDouble:(double)d->rate],
                                                              AVSampleRateKey, [NSNumber numberWithInt:d->channels],
                                                              AVNumberOfChannelsKey, nil];
    }
    @try {
        d->reader = [[AVAssetReader alloc] initWithAsset:d->asset error:&error];
        if (d->reader == nil) {
            error_text(error, text, sizeof(text));
            snprintf(why, size, "no AVAssetReader for the %s: %s", what_name(d), text);
            return 0;
        }
        d->output = [[AVAssetReaderTrackOutput alloc] initWithTrack:d->track outputSettings:settings];
        if (d->output == nil) {
            snprintf(why, size, "no AVAssetReaderTrackOutput for the %s", what_name(d));
            release_reader(d);
            return 0;
        }
        d->output.alwaysCopiesSampleData = NO;
        if (![d->reader canAddOutput:d->output]) {
            snprintf(why, size, "the reader does not take the %s' output (canAddOutput: NO)", what_name(d));
            release_reader(d);
            return 0;
        }
        [d->reader addOutput:d->output];
        {
            const CMTime begin = CMTimeMakeWithSeconds(from > 0.0 ? from : 0.0, 90000);
            /* past the part's end by a little: the core cuts at the end itself */
            const CMTime length = d->until > from ? CMTimeMakeWithSeconds(d->until - from + 0.25, 90000)
                                                  : kCMTimePositiveInfinity;
            d->reader.timeRange = CMTimeRangeMake(begin, length);
        }
        if (![d->reader startReading]) {
            error_text(d->reader.error, text, sizeof(text));
            snprintf(why, size, "the reader does not start on the %s (status %s): %s", what_name(d),
                     reader_status_name(d->reader.status), text);
            release_reader(d);
            return 0;
        }
    } @catch (NSException *exception) {
        snprintf(why, size, "AVFoundation refused the %s' reader: %s: %s", what_name(d), exception.name.UTF8String,
                 exception.reason.UTF8String);
        release_reader(d);
        return 0;
    }
    return 1;
}

/* Seconds on a clock that never goes back. */
static double seconds_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec * 1e-9;
}

/* After an interruption: opens the reader again where it stopped. Returns 1 when it reads again,
 * KHDAYS_HD_DECODER_LATER when it could not yet (the next try a little later), -1 with the reason once
 * every try has been spent. */
static int reopen_interrupted(KhdaysHdDecoder *d, char *why, size_t size)
{
    char again[512];
    ++d->interrupted;
    if (start_reader(d, d->given > 0 ? d->next : d->start, again, sizeof(again))) {
        d->waiting = 0;
        fprintf(stderr, "movies: avf: %s: the %s' reader opened again at %.3f s after the interruption (try %d of %d)\n",
                d->name, what_name(d), d->next, d->interrupted, INTERRUPTED_TRIES);
        return 1;
    }
    if (d->interrupted >= INTERRUPTED_TRIES) {
        snprintf(why, size,
                 "the %s' reader was interrupted at %.3f s after %ld given and could not be opened again in %d "
                 "tries: %s",
                 what_name(d), d->next, d->given, d->interrupted, again);
        fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
        return -1;
    }
    fprintf(stderr, "movies: avf: %s: the %s' reader does not open again yet (try %d of %d): %s; again in %.2f s\n",
            d->name, what_name(d), d->interrupted, INTERRUPTED_TRIES, again, INTERRUPTED_APART);
    d->waiting = 1;
    d->retry_at = seconds_now() + INTERRUPTED_APART;
    return KHDAYS_HD_DECODER_LATER;
}

/* The reader gave no sample. Returns 0 at the end of what it was asked for; 1 when it failed but was
 * opened again where it stopped (read on); KHDAYS_HD_DECODER_LATER when it was interrupted and is
 * opened again a little later (ask again); -1 with the reason when it failed for good. */
static int no_sample(KhdaysHdDecoder *d, char *why, size_t size)
{
    const AVAssetReaderStatus status = d->reader != nil ? d->reader.status : AVAssetReaderStatusUnknown;
    char text[512], again[512];
    if (d->waiting) {
        /* interrupted before, its reader let go: the next try when it is due */
        if (seconds_now() < d->retry_at) {
            return KHDAYS_HD_DECODER_LATER;
        }
        return reopen_interrupted(d, why, size);
    }
    if (status == AVAssetReaderStatusCompleted) {
        return 0;
    }
    error_text(d->reader != nil ? d->reader.error : nil, text, sizeof(text));
    if (status == AVAssetReaderStatusFailed) {
        NSError *error = d->reader.error;
        const int interrupted =
            error != nil && [error.domain isEqualToString:AVFoundationErrorDomain] &&
            (error.code == AVErrorOperationInterrupted || error.code == AVErrorMediaServicesWereReset ||
             error.code == AVErrorSessionWasInterrupted);
        if (interrupted) {
            /* not a fault of the file's, and not counted as one: the app was in the background, or the
             * media services were reset. Opened again at once the first time; a reader that opens but
             * is interrupted again before it gives a sample (the app still in the background) waits
             * between its tries, with the call returning meanwhile. */
            if (d->interrupted >= INTERRUPTED_TRIES) {
                snprintf(why, size,
                         "the %s' reader was interrupted at %.3f s after %ld given and again after each of %d "
                         "reopenings: %s",
                         what_name(d), d->next, d->given, d->interrupted, text);
                fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
                return -1;
            }
            fprintf(stderr,
                    "movies: avf: %s: the %s' reader was interrupted at %.3f s after %ld given (the app was in the "
                    "background, or the media services were reset): %s; opening it again there %s (%d of %d tries "
                    "used)\n",
                    d->name, what_name(d), d->next, d->given, text, d->interrupted == 0 ? "at once" : "a little later",
                    d->interrupted, INTERRUPTED_TRIES);
            release_reader(d);
            if (d->interrupted == 0) {
                return reopen_interrupted(d, why, size);
            }
            d->waiting = 1;
            d->retry_at = seconds_now() + INTERRUPTED_APART;
            return KHDAYS_HD_DECODER_LATER;
        }
    }
    if (status != AVAssetReaderStatusFailed || d->reopenings >= REOPENINGS) {
        snprintf(why, size, "the %s' reader stopped (status %s) at %.3f s after %ld given, %d reopenings: %s",
                 what_name(d), reader_status_name(status), d->next, d->given, d->reopenings, text);
        fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
        return -1;
    }
    ++d->reopenings;
    fprintf(stderr,
            "movies: avf: %s: the %s' reader failed at %.3f s after %ld given: %s; opening it again there (try %d of "
            "%d)\n",
            d->name, what_name(d), d->next, d->given, text, d->reopenings, REOPENINGS);
    usleep(150000u * (unsigned)d->reopenings);
    if (!start_reader(d, d->given > 0 ? d->next : d->start, again, sizeof(again))) {
        snprintf(why, size, "the %s' reader failed at %.3f s (%s) and cannot be opened again: %s", what_name(d),
                 d->next, text, again);
        fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
        return -1;
    }
    return 1;
}

/* Reads the asset's tracks and length, waiting for them here (the caller is never the main thread's
 * run loop: the player's own thread). Returns 0 with the reason. */
static int load_asset(AVURLAsset *asset, char *why, size_t size)
{
    NSArray *keys = [NSArray arrayWithObjects:@"tracks", @"duration", nil];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    long late;
    [asset loadValuesAsynchronouslyForKeys:keys
                         completionHandler:^{
                           dispatch_semaphore_signal(done);
                         }];
    late = dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, (int64_t)(LOAD_SECONDS * NSEC_PER_SEC)));
    dispatch_release(done); /* the handler's copy keeps it until it has run */
    if (late != 0) {
        snprintf(why, size, "its tracks were not read within %.0f s", LOAD_SECONDS);
        return 0;
    }
    for (NSString *key in keys) {
        NSError *error = nil;
        const AVKeyValueStatus status = [asset statusOfValueForKey:key error:&error];
        if (status != AVKeyValueStatusLoaded) {
            char text[512];
            error_text(error, text, sizeof(text));
            snprintf(why, size, "its %s cannot be read (status %ld): %s", key.UTF8String, (long)status, text);
            return 0;
        }
    }
    return 1;
}

static void decoder_free(KhdaysHdDecoder *d)
{
    release_held(d);
    release_reader(d);
    [d->track release];
    [d->asset release];
    free(d->samples);
    free(d);
}

KhdaysHdDecoder *khdays_hd_decoder_open(const char *path, int what, double from, double until, KhdaysHdFormat *format,
                                        char *why, size_t size)
{
    KhdaysHdDecoder *d = NULL;
    memset(format, 0, sizeof(*format));
    if (size > 0) {
        why[0] = '\0';
    }
    @autoreleasepool {
        NSString *given = path != NULL ? [NSString stringWithUTF8String:path] : nil;
        NSString *full;
        NSDictionary *attributes;
        NSError *error = nil;
        NSArray *tracks;
        AVURLAsset *asset;
        AVAssetTrack *track;
        CMFormatDescriptionRef description;
        char text[512], code[8];
        unsigned long long bytes;
        if (given == nil) {
            snprintf(why, size, "its path is not UTF-8 text");
            return NULL;
        }
        /* a path from the working folder (the app's Documents) as a whole one */
        full = [given isAbsolutePath]
                   ? given
                   : [[[NSFileManager defaultManager] currentDirectoryPath] stringByAppendingPathComponent:given];
        attributes = [[NSFileManager defaultManager] attributesOfItemAtPath:full error:&error];
        if (attributes == nil) {
            error_text(error, text, sizeof(text));
            snprintf(why, size, "the file %s cannot be found: %s", full.UTF8String, text);
            fprintf(stderr, "movies: avf: %s\n", why);
            return NULL;
        }
        bytes = [attributes fileSize];
        d = (KhdaysHdDecoder *)calloc(1, sizeof(*d));
        if (d == NULL) {
            snprintf(why, size, "no memory for its decoder");
            return NULL;
        }
        d->what = what;
        d->until = until > from ? until : 0.0;
        snprintf(d->name, sizeof(d->name), "%s", full.lastPathComponent.UTF8String);
        asset = [[AVURLAsset alloc]
            initWithURL:[NSURL fileURLWithPath:full isDirectory:NO]
                options:[NSDictionary dictionaryWithObject:[NSNumber numberWithBool:YES]
                                                    forKey:AVURLAssetPreferPreciseDurationAndTimingKey]];
        d->asset = asset;
        if (asset == nil) {
            snprintf(why, size, "no AVURLAsset for %s", full.UTF8String);
            decoder_free(d);
            return NULL;
        }
        if (!load_asset(asset, why, size)) {
            fprintf(stderr, "movies: avf: %s (%llu bytes): %s\n", d->name, bytes, why);
            decoder_free(d);
            return NULL;
        }
        format->duration = CMTIME_IS_NUMERIC(asset.duration) ? CMTimeGetSeconds(asset.duration) : 0.0;
        tracks = [asset tracksWithMediaType:what == KHDAYS_HD_DECODE_PICTURES ? AVMediaTypeVideo : AVMediaTypeAudio];
        if (tracks.count == 0) {
            snprintf(why, size, "the file has no %s track (%lu tracks in all)",
                     what == KHDAYS_HD_DECODE_PICTURES ? "picture" : "sound", (unsigned long)asset.tracks.count);
            if (what == KHDAYS_HD_DECODE_PICTURES) {
                fprintf(stderr, "movies: avf: %s (%llu bytes, %.3f s): %s\n", d->name, bytes, format->duration, why);
            }
            decoder_free(d);
            return NULL;
        }
        track = [tracks objectAtIndex:0];
        d->track = [track retain];
        description = track.formatDescriptions.count > 0
                          ? (CMFormatDescriptionRef)[track.formatDescriptions objectAtIndex:0]
                          : NULL;
        if (what == KHDAYS_HD_DECODE_PICTURES) {
            const CGSize natural = track.naturalSize;
            const CGAffineTransform t = track.preferredTransform;
            CMVideoDimensions coded = {0, 0};
            CGRect clean = CGRectZero;
            const int upright = fabs(t.a - 1.0) < 1e-3 && fabs(t.d - 1.0) < 1e-3 && fabs(t.b) < 1e-3 && fabs(t.c) < 1e-3;
            if (description != NULL) {
                coded = CMVideoFormatDescriptionGetDimensions((CMVideoFormatDescriptionRef)description);
                clean = CMVideoFormatDescriptionGetCleanAperture((CMVideoFormatDescriptionRef)description, false);
            }
            d->fps = track.nominalFrameRate;
            format->fps = d->fps;
            /* the area shown: the clean aperture inside the coded size when it is a sane one; each
             * picture then says its own (khdays_hd_decoder_picture), which is what the core shows */
            format->width = coded.width;
            format->height = coded.height;
            if (clean.size.width >= 2.0 && clean.size.height >= 2.0 && clean.size.width <= coded.width &&
                clean.size.height <= coded.height) {
                format->width = (int)(clean.size.width + 0.5);
                format->height = (int)(clean.size.height + 0.5);
            }
            if (format->width <= 0 || format->height <= 0) {
                format->width = (int)(natural.width + 0.5);
                format->height = (int)(natural.height + 0.5);
            }
            /* from a picture before the part's start: the core passes over what comes before it */
            d->start = from > 0.0 ? from - (d->fps > 0.0 ? 1.0 / d->fps : 0.04) : 0.0;
            if (d->start < 0.0) {
                d->start = 0.0;
            }
            fprintf(stderr,
                    "movies: avf: %s: %llu bytes, %.3f s, %lu tracks; picture track %d: codec '%s', coded %dx%d, "
                    "clean aperture %.0fx%.0f at %.0f,%.0f, natural %.0fx%.0f, %.3f frames a second, %.3f s, "
                    "transform [%.2f %.2f %.2f %.2f %.1f %.1f]%s, decodable %d; read as NV12 (420v) from %.3f s "
                    "to %s%.3f s\n",
                    d->name, bytes, format->duration, (unsigned long)asset.tracks.count, (int)track.trackID,
                    description != NULL ? fourcc(CMFormatDescriptionGetMediaSubType(description), code) : "none",
                    coded.width, coded.height, clean.size.width, clean.size.height, clean.origin.x, clean.origin.y,
                    natural.width, natural.height, d->fps, CMTimeGetSeconds(track.timeRange.duration), t.a, t.b, t.c,
                    t.d, t.tx, t.ty, upright ? "" : " (NOT upright: shown as stored, not turned)",
                    (int)track.isDecodable, d->start, d->until > 0.0 ? "" : "its end, ",
                    d->until > 0.0 ? d->until : format->duration);
        } else {
            const AudioStreamBasicDescription *basic =
                description != NULL ? CMAudioFormatDescriptionGetStreamBasicDescription(
                                          (CMAudioFormatDescriptionRef)description)
                                    : NULL;
            const int file_channels = basic != NULL ? (int)basic->mChannelsPerFrame : 0;
            const double file_rate = basic != NULL ? basic->mSampleRate : 0.0;
            /* as floats at the file's own rate; more than two channels mixed down to two */
            d->rate = file_rate >= 8000.0 && file_rate <= 192000.0 ? (int)(file_rate + 0.5) : 48000;
            d->channels = file_channels == 1 ? 1 : 2;
            d->start = from > 0.0 ? from : 0.0;
            format->channels = d->channels;
            format->rate = d->rate;
            fprintf(stderr,
                    "movies: avf: %s: sound track %d: codec '%s', %d channels, %.0f Hz, %.3f s, decodable %d; read "
                    "as interleaved 32-bit floats, %d channels %d Hz, from %.3f s to %s%.3f s\n",
                    d->name, (int)track.trackID,
                    description != NULL ? fourcc(CMFormatDescriptionGetMediaSubType(description), code) : "none",
                    file_channels, file_rate, CMTimeGetSeconds(track.timeRange.duration), (int)track.isDecodable,
                    d->channels, d->rate, d->start, d->until > 0.0 ? "" : "its end, ",
                    d->until > 0.0 ? d->until : format->duration);
        }
        d->next = d->start;
        if (!start_reader(d, d->start, why, size)) {
            fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
            decoder_free(d);
            return NULL;
        }
    }
    return d;
}

int khdays_hd_decoder_picture(KhdaysHdDecoder *d, KhdaysHdDecoded *out, char *why, size_t size)
{
    release_held(d);
    @autoreleasepool {
        for (;;) {
            CMSampleBufferRef sample = d->reader != nil ? [d->output copyNextSampleBuffer] : NULL;
            CVImageBufferRef image;
            CMTime stamp;
            double time;
            OSType type;
            CVReturn locked;
            size_t wide, high;
            CGRect clean;
            int x = 0, y = 0, w, h;
            if (sample == NULL) {
                const int more = no_sample(d, why, size);
                if (more == 1) {
                    continue;
                }
                return more; /* the end, a failure, or none now (KHDAYS_HD_DECODER_LATER) */
            }
            image = CMSampleBufferGetImageBuffer(sample);
            stamp = CMSampleBufferGetPresentationTimeStamp(sample);
            if (image == NULL || !CMTIME_IS_NUMERIC(stamp)) {
                CFRelease(sample); /* a sample without a picture (a marker): the next one */
                continue;
            }
            time = CMTimeGetSeconds(stamp);
            if (d->given > 0 && time <= d->next + 1e-6) {
                CFRelease(sample); /* given already, before the reader was opened again */
                continue;
            }
            CVPixelBufferRetain(image);
            CFRelease(sample);
            type = CVPixelBufferGetPixelFormatType(image);
            if (type != kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange || !CVPixelBufferIsPlanar(image) ||
                CVPixelBufferGetPlaneCount(image) < 2) {
                char code[8];
                snprintf(why, size, "a picture came as '%s' with %lu planes, not as NV12 (420v)", fourcc(type, code),
                         (unsigned long)CVPixelBufferGetPlaneCount(image));
                CVPixelBufferRelease(image);
                fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
                return -1;
            }
            locked = CVPixelBufferLockBaseAddress(image, kCVPixelBufferLock_ReadOnly);
            if (locked != kCVReturnSuccess) {
                snprintf(why, size, "a picture's memory cannot be read (CVPixelBufferLockBaseAddress %d)", (int)locked);
                CVPixelBufferRelease(image);
                fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
                return -1;
            }
            d->held = image; /* unlocked and released at the next call or the closing */
            wide = CVPixelBufferGetWidthOfPlane(image, 0);
            high = CVPixelBufferGetHeightOfPlane(image, 0);
            /* the area shown, when the buffer holds more (the coded rows of whole macroblocks) */
            clean = CVImageBufferGetCleanRect(image);
            w = (int)wide;
            h = (int)high;
            if (clean.size.width >= 2.0 && clean.size.height >= 2.0 && clean.origin.x >= 0.0 && clean.origin.y >= 0.0 &&
                clean.origin.x + clean.size.width <= (double)wide + 0.5 &&
                clean.origin.y + clean.size.height <= (double)high + 0.5) {
                x = (int)(clean.origin.x + 0.5) & ~1; /* the chroma has one sample per two columns and rows */
                y = (int)(clean.origin.y + 0.5) & ~1;
                w = (int)(clean.size.width + 0.5);
                h = (int)(clean.size.height + 0.5);
                if (x + w > (int)wide) {
                    w = (int)wide - x;
                }
                if (y + h > (int)high) {
                    h = (int)high - y;
                }
            }
            out->luma_pitch = (int)CVPixelBufferGetBytesPerRowOfPlane(image, 0);
            out->chroma_pitch = (int)CVPixelBufferGetBytesPerRowOfPlane(image, 1);
            out->luma = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(image, 0);
            out->chroma = (const uint8_t *)CVPixelBufferGetBaseAddressOfPlane(image, 1);
            if (out->luma == NULL || out->chroma == NULL || w < 2 || h < 2 ||
                CVPixelBufferGetHeightOfPlane(image, 1) * 2 < (size_t)(y + (h & ~1)) ||
                CVPixelBufferGetWidthOfPlane(image, 1) * 2 < (size_t)(x + (w & ~1))) {
                snprintf(why, size,
                         "a picture's planes are not NV12's: luma %lux%lu pitch %d, chroma %lux%lu pitch %d, shown "
                         "%dx%d at %d,%d",
                         (unsigned long)wide, (unsigned long)high, out->luma_pitch,
                         (unsigned long)CVPixelBufferGetWidthOfPlane(image, 1),
                         (unsigned long)CVPixelBufferGetHeightOfPlane(image, 1), out->chroma_pitch, w, h, x, y);
                fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
                return -1;
            }
            out->luma += (size_t)y * (size_t)out->luma_pitch + (size_t)x;
            out->chroma += (size_t)(y / 2) * (size_t)out->chroma_pitch + (size_t)x;
            out->width = w;
            out->height = h;
            out->time = time;
            if (!d->first_said) {
                CFTypeRef matrix = CVBufferCopyAttachment(image, kCVImageBufferYCbCrMatrixKey, NULL);
                char name[64] = "not said";
                if (matrix != NULL) {
                    if (CFGetTypeID(matrix) == CFStringGetTypeID()) {
                        CFStringGetCString((CFStringRef)matrix, name, sizeof(name), kCFStringEncodingUTF8);
                    }
                    CFRelease(matrix);
                }
                d->first_said = 1;
                fprintf(stderr,
                        "movies: avf: %s: first picture at %.3f s: buffer %lux%lu, luma pitch %d, chroma %lux%lu pitch "
                        "%d, shown %dx%d at %d,%d, colour matrix %s (the window shows BT.709 limited range)\n",
                        d->name, time, (unsigned long)wide, (unsigned long)high, out->luma_pitch,
                        (unsigned long)CVPixelBufferGetWidthOfPlane(image, 1),
                        (unsigned long)CVPixelBufferGetHeightOfPlane(image, 1), out->chroma_pitch, w, h, x, y, name);
            }
            d->next = time;
            ++d->given;
            d->reopenings = 0;
            d->interrupted = 0;
            return 1;
        }
    }
}

int khdays_hd_decoder_sound(KhdaysHdDecoder *d, float **samples, int *frames, double *time, char *why, size_t size)
{
    @autoreleasepool {
        for (;;) {
            CMSampleBufferRef sample = d->reader != nil ? [d->output copyNextSampleBuffer] : NULL;
            CMFormatDescriptionRef description;
            const AudioStreamBasicDescription *basic;
            CMBlockBufferRef block;
            CMTime stamp;
            size_t bytes, count, skip = 0;
            double at;
            OSStatus copied;
            if (sample == NULL) {
                const int more = no_sample(d, why, size);
                if (more == 1) {
                    continue;
                }
                return more; /* the end, a failure, or none now (KHDAYS_HD_DECODER_LATER) */
            }
            description = CMSampleBufferGetFormatDescription(sample);
            basic = description != NULL ? CMAudioFormatDescriptionGetStreamBasicDescription(
                                              (CMAudioFormatDescriptionRef)description)
                                        : NULL;
            block = CMSampleBufferGetDataBuffer(sample);
            stamp = CMSampleBufferGetPresentationTimeStamp(sample);
            count = (size_t)CMSampleBufferGetNumSamples(sample);
            if (block == NULL || count == 0) {
                CFRelease(sample); /* a sample without sound (a marker): the next one */
                continue;
            }
            if (basic == NULL || basic->mFormatID != kAudioFormatLinearPCM ||
                !(basic->mFormatFlags & kAudioFormatFlagIsFloat) ||
                (basic->mFormatFlags & (kAudioFormatFlagIsNonInterleaved | kAudioFormatFlagIsBigEndian)) ||
                basic->mBitsPerChannel != 32 || (int)basic->mChannelsPerFrame != d->channels ||
                fabs(basic->mSampleRate - (double)d->rate) > 0.5) {
                char code[8];
                snprintf(why, size,
                         "the sound came as '%s' flags 0x%x, %u bits, %u channels, %.0f Hz, not as interleaved floats "
                         "of %d channels at %d Hz",
                         basic != NULL ? fourcc(basic->mFormatID, code) : "none",
                         basic != NULL ? (unsigned)basic->mFormatFlags : 0u,
                         basic != NULL ? (unsigned)basic->mBitsPerChannel : 0u,
                         basic != NULL ? (unsigned)basic->mChannelsPerFrame : 0u, basic != NULL ? basic->mSampleRate : 0.0,
                         d->channels, d->rate);
                CFRelease(sample);
                fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
                return -1;
            }
            bytes = CMBlockBufferGetDataLength(block);
            if (bytes < count * (size_t)d->channels * sizeof(float)) {
                count = bytes / ((size_t)d->channels * sizeof(float));
            }
            at = CMTIME_IS_NUMERIC(stamp) ? CMTimeGetSeconds(stamp) : d->next;
            if (d->given > 0 && at < d->next - 0.5 / d->rate) {
                /* after the reader was opened again: what was given already is left out, so that the
                 * blocks go on without a gap or a repeat (the core's clock counts their frames) */
                skip = (size_t)((d->next - at) * d->rate + 0.5);
                if (skip >= count) {
                    CFRelease(sample);
                    continue;
                }
            }
            if (d->capacity < count * (size_t)d->channels) {
                float *grown = (float *)realloc(d->samples, count * (size_t)d->channels * sizeof(float));
                if (grown == NULL) {
                    CFRelease(sample);
                    snprintf(why, size, "no memory for %lu frames of sound", (unsigned long)count);
                    return -1;
                }
                d->samples = grown;
                d->capacity = count * (size_t)d->channels;
            }
            copied = CMBlockBufferCopyDataBytes(block, skip * (size_t)d->channels * sizeof(float),
                                                (count - skip) * (size_t)d->channels * sizeof(float), d->samples);
            CFRelease(sample);
            if (copied != kCMBlockBufferNoErr) {
                snprintf(why, size, "a block of sound cannot be copied (CMBlockBufferCopyDataBytes %d)", (int)copied);
                fprintf(stderr, "movies: avf: %s: %s\n", d->name, why);
                return -1;
            }
            if (!d->first_said) {
                d->first_said = 1;
                fprintf(stderr, "movies: avf: %s: first sound at %.6f s: %lu frames a block, %d channels %d Hz floats\n",
                        d->name, at, (unsigned long)count, d->channels, d->rate);
            }
            *samples = d->samples;
            *frames = (int)(count - skip);
            *time = d->given > 0 ? d->next : at + (double)skip / d->rate;
            d->next = *time + (double)(count - skip) / d->rate;
            ++d->given;
            d->reopenings = 0;
            d->interrupted = 0;
            return 1;
        }
    }
}

double khdays_hd_decoder_output_latency(double buffer)
{
    /* SDL's Core Audio device keeps three AudioQueue buffers filled (its default): the one a stream's
     * sound is taken into waits behind two others, so it begins to play two buffers after it would on
     * a device that plays what it takes at once. Then the route's own delay, as the audio session
     * says it (the speaker a few milliseconds; wireless earphones far more). */
    double route = 0.0;
#if TARGET_OS_IPHONE
    @autoreleasepool {
        route = [AVAudioSession sharedInstance].outputLatency;
    }
    if (!(route >= 0.0 && route < 2.0)) {
        route = 0.0;
    }
#endif
    return 2.0 * (buffer > 0.0 ? buffer : 0.0) + route;
}

void khdays_hd_decoder_close(KhdaysHdDecoder *d)
{
    if (d == NULL) {
        return;
    }
    @autoreleasepool {
        fprintf(stderr, "movies: avf: %s: %s decoder closed at %.3f s after %ld %s (reader %s)\n", d->name,
                what_name(d), d->next, d->given, d->what == KHDAYS_HD_DECODE_PICTURES ? "pictures" : "sound blocks",
                d->reader != nil ? reader_status_name(d->reader.status) : "none");
        decoder_free(d);
    }
}
