/* The HD video's decoder for the Linux test build of the phone (CMake's KHDAYS_MOBILE_TEST with FFmpeg's
 * libraries found): movie_hd_decoder.h over libavformat and libavcodec, so that the phone's shared player
 * (movie_hd_core.c) and the window's drawing of its pictures (host/render/gpu_present.c draw_hd_movie, the
 * phone's fill) run on Linux with the player's own videos. The phone itself uses AVFoundation
 * (movie_hd_apple.m); this file is never part of an app.
 *
 * Pictures: decoded as the file has them (H.264's 8-bit 4:2:0, planar), given as NV12 -- the chroma
 * planes interleaved here, row by row. Sound: decoded to floats, planar or not, given interleaved at the
 * file's own rate and channels. Read forward from a place: a seek to the key picture at or before it; the
 * core leaves out what comes before `from`. Every failure says FFmpeg's reason. */
#include "movie_hd_decoder.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct KhdaysHdDecoder {
    int what;
    AVFormatContext *format;
    AVCodecContext *codec;
    int stream;
    AVPacket *packet;
    AVFrame *frame;
    int draining, ended;
    /* the picture given last, as NV12 */
    uint8_t *luma, *chroma;
    size_t luma_size, chroma_size;
    /* the sound given last, interleaved */
    float *samples;
    size_t capacity; /* in floats */
    int channels, rate;
};

static void reason(char *why, size_t size, const char *what, int error)
{
    char text[128];
    av_strerror(error, text, sizeof(text));
    snprintf(why, size, "%s: %s", what, text);
}

KhdaysHdDecoder *khdays_hd_decoder_open(const char *path, int what, double from, double until, KhdaysHdFormat *format,
                                        char *why, size_t size)
{
    KhdaysHdDecoder *d = (KhdaysHdDecoder *)calloc(1, sizeof(*d));
    const AVCodec *decoder = NULL;
    AVStream *st;
    int error;
    (void)until; /* read to the end: the core stops where it wants */
    if (d == NULL) {
        snprintf(why, size, "out of memory");
        return NULL;
    }
    d->what = what;
    if ((error = avformat_open_input(&d->format, path, NULL, NULL)) < 0) {
        reason(why, size, "cannot open the file", error);
        free(d);
        return NULL;
    }
    if ((error = avformat_find_stream_info(d->format, NULL)) < 0) {
        reason(why, size, "cannot read its tracks", error);
        khdays_hd_decoder_close(d);
        return NULL;
    }
    d->stream = av_find_best_stream(d->format, what == KHDAYS_HD_DECODE_PICTURES ? AVMEDIA_TYPE_VIDEO : AVMEDIA_TYPE_AUDIO,
                                    -1, -1, &decoder, 0);
    if (d->stream < 0 || decoder == NULL) {
        snprintf(why, size, what == KHDAYS_HD_DECODE_PICTURES ? "no picture track" : "no sound track");
        khdays_hd_decoder_close(d);
        return NULL;
    }
    st = d->format->streams[d->stream];
    d->codec = avcodec_alloc_context3(decoder);
    if (d->codec == NULL || avcodec_parameters_to_context(d->codec, st->codecpar) < 0) {
        snprintf(why, size, "no decoder context");
        khdays_hd_decoder_close(d);
        return NULL;
    }
    if (what == KHDAYS_HD_DECODE_SOUND) {
        d->codec->request_sample_fmt = AV_SAMPLE_FMT_FLT;
    }
    if ((error = avcodec_open2(d->codec, decoder, NULL)) < 0) {
        reason(why, size, "cannot open the decoder", error);
        khdays_hd_decoder_close(d);
        return NULL;
    }
    d->packet = av_packet_alloc();
    d->frame = av_frame_alloc();
    if (d->packet == NULL || d->frame == NULL) {
        snprintf(why, size, "out of memory");
        khdays_hd_decoder_close(d);
        return NULL;
    }
    if (from > 0.0) {
        const int64_t at = (int64_t)(from / av_q2d(st->time_base));
        if (av_seek_frame(d->format, d->stream, at, AVSEEK_FLAG_BACKWARD) >= 0) {
            avcodec_flush_buffers(d->codec);
        }
    }
    memset(format, 0, sizeof(*format));
    format->duration = d->format->duration > 0 ? (double)d->format->duration / AV_TIME_BASE : 0.0;
    if (what == KHDAYS_HD_DECODE_PICTURES) {
        const AVRational rate = av_guess_frame_rate(d->format, st, NULL);
        format->width = d->codec->width;
        format->height = d->codec->height;
        format->fps = rate.num > 0 && rate.den > 0 ? av_q2d(rate) : 0.0;
    } else {
        d->channels = d->codec->ch_layout.nb_channels;
        d->rate = d->codec->sample_rate;
        format->channels = d->channels;
        format->rate = d->rate;
    }
    fprintf(stderr, "movies: ffmpeg: %s: %s track %d (%s)%s%dx%d %.3f s\n", path,
            what == KHDAYS_HD_DECODE_PICTURES ? "picture" : "sound", d->stream, decoder->name, from > 0 ? " from " : " ",
            format->width, format->height, format->duration);
    return d;
}

/* The next decoded frame of the track: 1, 0 at the end, -1 on a failure. */
static int next_frame(KhdaysHdDecoder *d, char *why, size_t size)
{
    for (;;) {
        int error = avcodec_receive_frame(d->codec, d->frame);
        if (error == 0) {
            return 1;
        }
        if (error == AVERROR_EOF) {
            return 0;
        }
        if (error != AVERROR(EAGAIN)) {
            reason(why, size, "cannot decode", error);
            return -1;
        }
        if (d->draining) {
            return 0;
        }
        for (;;) {
            error = av_read_frame(d->format, d->packet);
            if (error < 0) {
                d->draining = 1;
                avcodec_send_packet(d->codec, NULL); /* the frames still in the decoder */
                break;
            }
            if (d->packet->stream_index == d->stream) {
                error = avcodec_send_packet(d->codec, d->packet);
                av_packet_unref(d->packet);
                if (error < 0 && error != AVERROR(EAGAIN)) {
                    reason(why, size, "cannot decode", error);
                    return -1;
                }
                break;
            }
            av_packet_unref(d->packet);
        }
    }
}

static double frame_time(const KhdaysHdDecoder *d)
{
    const int64_t pts = d->frame->best_effort_timestamp != AV_NOPTS_VALUE ? d->frame->best_effort_timestamp : d->frame->pts;
    const AVStream *st = d->format->streams[d->stream];
    return pts == AV_NOPTS_VALUE ? 0.0 : (double)(pts - (st->start_time != AV_NOPTS_VALUE ? st->start_time : 0)) * av_q2d(st->time_base);
}

int khdays_hd_decoder_picture(KhdaysHdDecoder *d, KhdaysHdDecoded *out, char *why, size_t size)
{
    const int got = next_frame(d, why, size);
    const AVFrame *f = d->frame;
    int w, h;
    if (got <= 0) {
        return got;
    }
    if (f->format != AV_PIX_FMT_YUV420P && f->format != AV_PIX_FMT_YUVJ420P && f->format != AV_PIX_FMT_NV12) {
        snprintf(why, size, "pictures as %s: only 8-bit 4:2:0 is read", av_get_pix_fmt_name((enum AVPixelFormat)f->format));
        return -1;
    }
    w = f->width & ~1;
    h = f->height & ~1;
    if (d->luma_size < (size_t)w * (size_t)h) {
        free(d->luma);
        d->luma_size = (size_t)w * (size_t)h;
        d->luma = (uint8_t *)malloc(d->luma_size);
    }
    if (d->chroma_size < (size_t)w * (size_t)(h / 2)) {
        free(d->chroma);
        d->chroma_size = (size_t)w * (size_t)(h / 2);
        d->chroma = (uint8_t *)malloc(d->chroma_size);
    }
    if (d->luma == NULL || d->chroma == NULL) {
        d->luma_size = d->chroma_size = 0;
        snprintf(why, size, "out of memory");
        return -1;
    }
    for (int y = 0; y < h; ++y) {
        memcpy(d->luma + (size_t)y * (size_t)w, f->data[0] + (size_t)y * (size_t)f->linesize[0], (size_t)w);
    }
    for (int y = 0; y < h / 2; ++y) {
        uint8_t *row = d->chroma + (size_t)y * (size_t)w;
        if (f->format == AV_PIX_FMT_NV12) {
            memcpy(row, f->data[1] + (size_t)y * (size_t)f->linesize[1], (size_t)w);
            continue;
        }
        const uint8_t *u = f->data[1] + (size_t)y * (size_t)f->linesize[1];
        const uint8_t *v = f->data[2] + (size_t)y * (size_t)f->linesize[2];
        for (int x = 0; x < w / 2; ++x) {
            row[2 * x] = u[x];
            row[2 * x + 1] = v[x];
        }
    }
    out->luma = d->luma;
    out->chroma = d->chroma;
    out->luma_pitch = out->chroma_pitch = w;
    out->width = w;
    out->height = h;
    out->time = frame_time(d);
    return 1;
}

int khdays_hd_decoder_sound(KhdaysHdDecoder *d, float **samples, int *frames, double *time, char *why, size_t size)
{
    const int got = next_frame(d, why, size);
    const AVFrame *f = d->frame;
    const int channels = d->channels, n = f->nb_samples;
    if (got <= 0) {
        return got;
    }
    if (f->ch_layout.nb_channels != channels || (f->format != AV_SAMPLE_FMT_FLT && f->format != AV_SAMPLE_FMT_FLTP)) {
        snprintf(why, size, "sound as %s with %d channels: only floats of the first block's channels are read",
                 av_get_sample_fmt_name((enum AVSampleFormat)f->format), f->ch_layout.nb_channels);
        return -1;
    }
    if (d->capacity < (size_t)n * (size_t)channels) {
        free(d->samples);
        d->capacity = (size_t)n * (size_t)channels;
        d->samples = (float *)malloc(d->capacity * sizeof(float));
        if (d->samples == NULL) {
            d->capacity = 0;
            snprintf(why, size, "out of memory");
            return -1;
        }
    }
    if (f->format == AV_SAMPLE_FMT_FLT) {
        memcpy(d->samples, f->data[0], (size_t)n * (size_t)channels * sizeof(float));
    } else {
        for (int c = 0; c < channels; ++c) {
            const float *plane = (const float *)f->extended_data[c];
            for (int i = 0; i < n; ++i) {
                d->samples[(size_t)i * (size_t)channels + (size_t)c] = plane[i];
            }
        }
    }
    *samples = d->samples;
    *frames = n;
    *time = frame_time(d);
    return 1;
}

void khdays_hd_decoder_close(KhdaysHdDecoder *d)
{
    if (d == NULL) {
        return;
    }
    av_frame_free(&d->frame);
    av_packet_free(&d->packet);
    avcodec_free_context(&d->codec);
    avformat_close_input(&d->format);
    free(d->luma);
    free(d->chroma);
    free(d->samples);
    free(d);
}

double khdays_hd_decoder_output_latency(double buffer)
{
    (void)buffer;
    return 0.0; /* (SDL's dummy driver in the tests: nothing is heard) */
}
